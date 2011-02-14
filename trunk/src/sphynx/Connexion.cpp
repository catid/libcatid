/*
	Copyright (c) 2009-2011 Christopher A. Taylor.  All rights reserved.

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
#include <cat/time/Clock.hpp>
#include <cat/hash/Murmur.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/tunnel/Keys.hpp>
using namespace cat;
using namespace sphynx;

void Connexion::OnShutdownRequest()
{
}

bool Connexion::OnZeroReferences()
{
	return true;
}

void Connexion::OnDisconnectComplete()
{
	RequestShutdown();
}

void Connexion::OnWorkerRead(IWorkerTLS *itls, const BatchSet &buffers)
{
	SphynxTLS *tls = reinterpret_cast<SphynxTLS*>( itls );
	u32 buffer_count = 0;

	BatchSet delivery;
	delivery.Clear();

	// For each connected datagram,
	for (BatchHead *next, *node = buffers.head; node; node = next)
	{
		next = node->batch_next;
		++buffer_count;
		RecvBuffer *buffer = reinterpret_cast<RecvBuffer*>( node );
		u8 *data = GetTrailingBytes(buffer);
		u32 data_bytes = buffer->data_bytes;

		// If the data could be decrypted,
		if (data_bytes > (AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD) &&
			_auth_enc.Decrypt(data, data_bytes))
		{
			data_bytes -= AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD;
			buffer->send_time = decodeClientTimestamp(buffer->event_msec, getLE(*(u16*)(data + data_bytes)));
			buffer->data_bytes = data_bytes;
			delivery.PushBack(buffer);
		}
		else if (buffer_count <= 1 && !_seen_encrypted)
		{
			// Handle lost s2c answer by retransmitting it
			// And only do this for the first packet we get
			u32 bytes = buffer->data_bytes;

			if (bytes == C2S_CHALLENGE_LEN && data[0] == C2S_CHALLENGE)
			{
				u8 *challenge = data + 1 + 4 + 4;

				// Only need to check that the challenge is the same, since we
				// have already validated the cookie and protocol magic to get here
				if (!SecureEqual(_first_challenge, challenge, CHALLENGE_BYTES))
				{
					WARN("Connexion") << "Ignoring challenge: Replay challenge in bad state";
					continue;
				}

				u8 *pkt = SendBuffer::Acquire(S2C_ANSWER_LEN);
				if (!pkt)
				{
					WARN("Connexion") << "Ignoring challenge: Unable to allocate post buffer";
					continue;
				}

				// Construct packet
				pkt[0] = S2C_ANSWER;

				memcpy(pkt + 1, _cached_answer, ANSWER_BYTES);

				_parent->Write(pkt, S2C_ANSWER_LEN, buffer->addr);

				INANE("Connexion") << "Replayed lost answer to client challenge";
			}
		}
	}

	// Process all datagrams that decrypted properly
	if (delivery.head)
	{
		OnTransportDatagrams(tls, delivery);
		_seen_encrypted = true;
		_last_recv_tsc = Clock::msec_fast();
	}

	_parent->ReleaseRecvBuffers(buffers, buffer_count);
}

void Connexion::OnWorkerTick(IWorkerTLS *itls, u32 now)
{
	SphynxTLS *tls = reinterpret_cast<SphynxTLS*>( itls );

	// If in graceful disconnect,
	if (IsDisconnected())
	{
		// Still tick transport layer because it is delivering IOP_DISCO messages
		TickTransport(tls, now);
	}
	else
	{
		// Do derived class tick event so any messages posted do not need to wait 20 ms
		OnTick(tls, now);

		TickTransport(tls, now);

		// If no packets have been received,
		if ((s32)(now - _last_recv_tsc) >= TIMEOUT_DISCONNECT)
		{
			Disconnect(DISCO_TIMEOUT);
		}
	}
}

Connexion::Connexion()
{
	_seen_encrypted = false;
	InitializeWorkerCallbacks(this);
}

bool Connexion::WriteDatagrams(const BatchSet &buffers)
{
	u32 now = getLocalTime();
	u16 timestamp = getLE(encodeServerTimestamp(now));

	/*
		The format of each buffer:

		[TRANSPORT(X)] [TIMESTAMP(2)] [ENCRYPTION(11)]

		At this point, the timestamp has not been written.
		The encryption overhead is also not filled in yet.

		Each buffer's data_bytes is the transport layer data length.
		We need to add the 13 bytes of overhead to this before writing it.
	*/

	// For each datagram to send,
	for (BatchHead *node = buffers.head; node; node = node->batch_next)
	{
		// Unwrap the message data
		SendBuffer *buffer = reinterpret_cast<SendBuffer*>( node );
		u8 *msg_data = GetTrailingBytes(buffer);
		u32 msg_bytes = buffer->bytes;

		// Write timestamp right before the encryption overhead
		*(u16*)(msg_data + msg_bytes) = timestamp;

		msg_bytes += Transport::TRANSPORT_OVERHEAD + AuthenticatedEncryption::OVERHEAD_BYTES;

		// Encrypt the message
		_auth_enc.Encrypt(msg_data, msg_bytes);
		buffer->bytes = msg_bytes;
	}

	// Do not need to update a "last send" timestamp here because the client is responsible for sending keep-alives
	return _parent->Write(buffers, _client_addr);
}

void Connexion::OnInternal(SphynxTLS *tls, u32 send_time, u32 recv_time, BufferStream data, u32 bytes)
{
	switch (data[0])
	{
	case IOP_C2S_MTU_PROBE:
		if (bytes >= IOP_C2S_MTU_TEST_MINLEN)
		{
			// The byte count does not include the 2 byte header
			bytes += 2;

			WARN("Server") << "Got IOP_C2S_MTU_PROBE.  Max payload bytes = " << bytes;

			// If new maximum payload is greater than the previous one,
			if (bytes > _max_payload_bytes)
			{
				// Set max payload bytes
				_max_payload_bytes = bytes;
				_send_flow.SetMTU(bytes);

				u16 mtu = getLE((u16)bytes);
				WriteReliable(STREAM_UNORDERED, IOP_S2C_MTU_SET, &mtu, sizeof(mtu), SOP_INTERNAL);
			}
		}
		break;

	case IOP_C2S_TIME_PING:
		if (bytes == IOP_C2S_TIME_PING_LEN)
		{
			u32 *client_timestamp = reinterpret_cast<u32*>( data + 1 );

			u32 stamps[3] = { *client_timestamp, getLE(recv_time), getLE(Clock::msec()) };

			WriteOOB(IOP_S2C_TIME_PONG, stamps, sizeof(stamps), SOP_INTERNAL);

			WARN("Server") << "Got IOP_C2S_TIME_PING.  Stamp = " << *client_timestamp;
		}
		break;

	case IOP_DISCO:
		if (bytes == IOP_DISCO_LEN)
		{
			WARN("Server") << "Got IOP_DISCO reason = " << (int)data[1];

			Disconnect(data[1]);
		}
		break;
	}
}
