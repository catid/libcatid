/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#include <cat/sphynx/Server.hpp>
#include <cat/mem/AlignedAllocator.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <cat/io/Settings.hpp>
#include <cat/io/Base64.hpp>
#include <cat/time/Clock.hpp>
#include <cat/hash/Murmur.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/tunnel/KeyMaker.hpp>
using namespace cat;
using namespace sphynx;

void Connexion::OnShutdownRequest()
{
}

bool Connexion::OnZeroReferences()
{
}

Connexion::Connexion()
{
	_destroyed = 0;
	_seen_encrypted = false;
}

void Connexion::Disconnect(u8 reason, bool notify)
{
	if (Atomic::Set(&_destroyed, 1) == 0)
	{
		if (notify)
			PostDisconnect(reason);

		TransportDisconnected();

		OnDisconnect(reason);
	}
}

void Connexion::OnWorkerRead(RecvBuffer *buffer_list_head)
{
	u32 buf_bytes = bytes;

	// If packet is valid,
	if (_auth_enc.Decrypt(data, buf_bytes))
	{
		u32 recv_time = Clock::msec();

		_last_recv_tsc = recv_time;

		if (buf_bytes >= 2)
		{
			// Read timestamp for transmission
			buf_bytes -= 2;
			u32 send_time = DecodeClientTimestamp(recv_time, getLE(*(u16 *)(data + buf_bytes)));

			// Pass it to the transport layer
			OnDatagram(tls, send_time, recv_time, data, buf_bytes);
			return;
		}
	}

	WARN("Server") << "Ignoring invalid encrypted data";
}

void Connexion::OnWorkerTick(u32 now)
{
	// If no packets have been received,
	if ((s32)(now - _last_recv_tsc) >= TIMEOUT_DISCONNECT)
	{
		Disconnect(DISCO_TIMEOUT, true);

		RequestShutdown();
	}

	TickTransport(tls, now);

	OnTick(tls, now);
}

bool Connexion::PostPacket(u8 *buffer, u32 buf_bytes, u32 msg_bytes)
{
	// Write timestamp for transmission
	*(u16 *)(buffer + msg_bytes) = getLE(EncodeServerTimestamp(GetLocalTime()));
	msg_bytes += 2;

	if (!_auth_enc.Encrypt(buffer, buf_bytes, msg_bytes))
	{
		WARN("Server") << "Encryption failure while sending packet";

		AsyncBuffer::Release(buffer);

		return false;
	}

	return _server_worker->Post(_client_addr, buffer, msg_bytes);
}

void Connexion::OnInternal(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, BufferStream data, u32 bytes)
{
	switch (data[0])
	{
	case IOP_C2S_MTU_PROBE:
		if (bytes >= IOP_C2S_MTU_TEST_MINLEN)
		{
			// MTU test payload includes a 2 byte header on top of data bytes
			u32 payload_bytes = bytes + TRANSPORT_HEADER_BYTES;

			//WARN("Server") << "Got IOP_C2S_MTU_PROBE.  Max payload bytes = " << payload_bytes;

			// If new maximum payload is greater than the previous one,
			if (payload_bytes > _max_payload_bytes)
			{
				// Set max payload bytes
				_max_payload_bytes = payload_bytes;
				_send_flow.SetMTU(payload_bytes);

				// Notify client that the packet made it through
				u16 mtu = getLE((u16)payload_bytes);
				WriteReliable(STREAM_UNORDERED, IOP_S2C_MTU_SET, &mtu, sizeof(mtu), SOP_INTERNAL);
			}
		}
		break;

	case IOP_C2S_TIME_PING:
		if (bytes == IOP_C2S_TIME_PING_LEN)
		{
			u32 *client_timestamp = reinterpret_cast<u32*>( data + 1 );

			// Construct message data
			u32 stamps[2] = { *client_timestamp, getLE(Clock::msec()) };

			// Write it out-of-band to avoid delays in transmission
			WriteUnreliableOOB(IOP_S2C_TIME_PONG, &stamps, sizeof(stamps), SOP_INTERNAL);

			//WARN("Server") << "Got IOP_C2S_TIME_PING.  Stamp = " << *client_timestamp;
		}
		break;

	case IOP_DISCO:
		if (bytes == IOP_DISCO_LEN)
		{
			//WARN("Server") << "Got IOP_DISCO reason = " << (int)data[1];

			Disconnect(data[1], false);
		}
		break;
	}
}
