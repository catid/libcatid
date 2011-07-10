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

#include <cat/sphynx/Connexion.hpp>
#include <cat/sphynx/Server.hpp>
#include <cat/io/Logging.hpp>
#include <cat/crypt/SecureEqual.hpp>
using namespace cat;
using namespace sphynx;

void Connexion::OnShutdownRequest()
{
	_parent->_conn_map.Remove(this);
}

bool Connexion::OnZeroReferences()
{
	_parent->ReleaseRef();
	return true;
}

void Connexion::OnDisconnectComplete()
{
	RequestShutdown();
}

void Connexion::OnWorkerRecv(IWorkerTLS *itls, const BatchSet &buffers)
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

		CAT_INFO("Connexion") << "Decrypting " << data_bytes << " bytes";

		// If the data could be decrypted,
		if (data_bytes > SPHYNX_OVERHEAD &&
			_auth_enc.Decrypt(data, data_bytes))
		{
			data_bytes -= SPHYNX_OVERHEAD;
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
					CAT_WARN("Connexion") << "Ignoring challenge: Replay challenge in bad state";
					continue;
				}

				u8 *pkt = SendBuffer::Acquire(S2C_ANSWER_LEN);
				if (!pkt)
				{
					CAT_WARN("Connexion") << "Ignoring challenge: Unable to allocate post buffer";
					continue;
				}

				// Construct packet
				pkt[0] = S2C_ANSWER;

				memcpy(pkt + 1, _cached_answer, ANSWER_BYTES);

				_parent->Write(pkt, S2C_ANSWER_LEN, buffer->GetAddr());

				CAT_INANE("Connexion") << "Replayed lost answer to client challenge";
			}
		}
	}

	// Process all datagrams that decrypted properly
	if (delivery.head)
	{
		// TODO: Simulating out of order packets
		if (delivery.tail && delivery.tail != delivery.head)
		{
			BatchHead *old_head = delivery.head;
			BatchHead *old_next = old_head->batch_next;

			old_head->batch_next = old_next->batch_next;
			old_next->batch_next = old_head;

			delivery.head = old_next;

			if (old_next == delivery.tail)
				delivery.tail = old_head;
		}

		OnTransportDatagrams(tls, delivery);
		_seen_encrypted = true;
		_last_recv_tsc = Clock::msec_fast();
	}

	_parent->ReleaseRecvBuffers(buffers, buffer_count);

	ReleaseRef(buffer_count);
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
		// Do derived class tick event so any messages posted do not need to wait for the next tick
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
	_key = ConnexionMap::INVALID_KEY;
	_seen_encrypted = false;

	_worker_id = INVALID_WORKER_ID;
}

bool Connexion::WriteDatagrams(const BatchSet &buffers, u32 count)
{
	u64 iv = _auth_enc.GrabIVRange(count);

	/*
		The format of each buffer:

		[TRANSPORT(X)] [ENCRYPTION(11)]

		The encryption overhead is not filled in yet.
		Each buffer's data_bytes is the transport layer data length.
		We need to add the 11 bytes of overhead to this before writing it.
	*/

	// For each datagram to send,
	for (BatchHead *node = buffers.head; node; node = node->batch_next)
	{
		// Unwrap the message data
		SendBuffer *buffer = reinterpret_cast<SendBuffer*>( node );
		u8 *msg_data = GetTrailingBytes(buffer);
		u32 msg_bytes = buffer->GetBytes();

		// Encrypt the message
		_auth_enc.Encrypt(iv, msg_data, msg_bytes);
	}

	// Do not need to update a "last send" timestamp here because the client is responsible for sending keep-alives
	return _parent->Write(buffers, count, _client_addr);
}

void Connexion::OnInternal(SphynxTLS *tls, u32 recv_time, BufferStream data, u32 bytes)
{
	switch (data[0])
	{
	case IOP_C2S_MTU_PROBE:
		if (bytes >= IOP_C2S_MTU_TEST_MINLEN)
		{
			// The byte count does not include the 2 byte header
			bytes += 2;

			CAT_WARN("Server") << "Got IOP_C2S_MTU_PROBE.  Max payload bytes = " << bytes;

			// If new maximum payload is greater than the previous one,
			if (bytes > _max_payload_bytes)
			{
				// Set max payload bytes
				_max_payload_bytes = bytes;

				u16 mtu = getLE((u16)bytes);
				WriteReliable(STREAM_UNORDERED, IOP_S2C_MTU_SET, &mtu, 2, SOP_INTERNAL);
			}
		}
		break;

	case IOP_C2S_TIME_PING:
		if (bytes == IOP_C2S_TIME_PING_LEN)
		{
			u32 *client_timestamp = reinterpret_cast<u32*>( data + 1 );

			u32 stamps[3] = { *client_timestamp, getLE(recv_time), getLE(Clock::msec()) };

			WriteOOB(IOP_S2C_TIME_PONG, stamps, sizeof(stamps), SOP_INTERNAL);

			CAT_WARN("Server") << "Got IOP_C2S_TIME_PING.  Stamp = " << *client_timestamp;
		}
		break;

	case IOP_DISCO:
		if (bytes == IOP_DISCO_LEN)
		{
			CAT_WARN("Server") << "Got IOP_DISCO reason = " << (int)data[1];

			Disconnect(data[1]);
		}
		break;
	}
}
