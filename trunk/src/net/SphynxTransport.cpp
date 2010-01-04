/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#include <cat/net/SphynxTransport.hpp>
#include <cat/port/EndianNeutral.hpp>
#include <cat/math/BitMath.hpp>
#include <cat/threads/RegionAllocator.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;
using namespace sphynx;


//// sphynx::Transport

Transport::Transport()
{
	// Receive state
	_next_recv_expected_id = 0;

	_fragment_length = 0;

	_recv_queue_head = 0;

	// Send state
	_next_send_id = 0;

	_send_buffer = 0;
	_send_buffer_bytes = 0;

	_send_queue_head = 0;
	_send_queue_tail = 0;

	_sent_list_head = 0;
}

Transport::~Transport()
{
	// Release memory for receive queue
	RecvQueue *recv_node = _recv_queue_head;
	while (recv_node)
	{
		RecvQueue *next = recv_node->next;
		RegionAllocator::ii->Release(recv_node);
		recv_node = next;
	}

	// Release memory for fragment buffer
	if (_fragment_length)
	{
		delete []_fragment_buffer;
	}

	// Release memory for send buffer
	if (_send_buffer_bytes)
	{
		RegionAllocator::ii->Release(_send_buffer);
	}

	// Release memory for send queue
	SendQueue *send_node = _send_queue_head;
	while (send_node)
	{
		SendQueue *next = send_node->next;
		RegionAllocator::ii->Release(send_node);
		send_node = next;
	}

	// Release memory for sent list
	SendQueue *sent_node = _sent_list_head;
	while (sent_node)
	{
		SendQueue *next = sent_node->next;
		RegionAllocator::ii->Release(sent_node);
		sent_node = next;
	}
}

void Transport::InitializePayloadBytes(bool ip6)
{
	u32 overhead = (ip6 ? IPV6_HEADER_BYTES : IPV4_HEADER_BYTES) + UDP_HEADER_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES;

	_max_payload_bytes = MINIMUM_MTU - overhead;
}

void Transport::TickTransport(ThreadPoolLocalStorage *tls, u32 now)
{
	TransmitQueued();
}

void Transport::OnPacket(u8 *data, u32 bytes)
{
	while (bytes >= 2)
	{
		/*
			Packet: Decrypted buffer from UDP payload.
			Message: One message from the buffer, 0..2047 bytes.

			All multi-byte fields are in little-endian byte order.

			--- Message Header  (16 bits) ---
			 0 1 2 3 4 5 6 7 8 9 a b c d e f
			<-- LSB ----------------- MSB -->
			|    Msg.Bytes(11)    | Type(5) |
			---------------------------------

			Msg.Bytes includes all data after this header related
			to the message, such as acknowledgment identifiers.
			This is done to allow the receiver to ignore message
			types that it does not recognize.

			Types: See enum MessageTypes
		*/

		u16 header = getLE(*reinterpret_cast<u16*>( data ));
		bytes -= 2;
		data += 2;

		u16 msg_bytes = header & 0x7ff;
		if (bytes < msg_bytes) break;
		u16 msg_type = header >> 11;

		switch (msg_type)
		{
		case TYPE_MTU_PROBE:
			if (msg_bytes + 2 > _max_payload_bytes)
			{
				u16 payload_bytes = msg_bytes + 2;

				u8 msg[3];

				msg[0] = OP_MTU_CHANGE;
				*reinterpret_cast<u16*>( msg + 1 ) = getLE(payload_bytes);

				PostMsg(MODE_RELIABLE, msg, sizeof(msg));

				_max_payload_bytes = payload_bytes;
			}
			break;

		case TYPE_UNRELIABLE:
			// Message data continues directly after header
			if (msg_bytes > 0) OnMessage(data, msg_bytes);
			break;

		case TYPE_RELIABLE:
		case TYPE_RELIABLE_FRAG:
			if (msg_bytes > 2)
			{
				// 16-bit acknowledgment identifier after header
				u32 ack_id = ReconstructCounter<16>(_next_recv_expected_id, getLE(*reinterpret_cast<u16*>( data )));

				s32 diff = (s32)(_next_recv_expected_id - ack_id);

				// If message is next expected,
				if (diff == 0)
				{
					// Process fragment immediately
					OnFragment(data+2, msg_bytes-2, msg_type == TYPE_RELIABLE_FRAG);

					// Cache node and ack_id on stack
					RecvQueue *node = _recv_queue_head;
					++ack_id;

					// For each queued message that is now ready to go,
					while (node && node->id == ack_id)
					{
						// Grab the queued message
						bool frag = (node->bytes & RecvQueue::FRAG_FLAG) != 0;
						u32 bytes = node->bytes & RecvQueue::BYTE_MASK;
						RecvQueue *next = node->next;
						u8 *old_data = GetTrailingBytes(node);

						// Process fragment now
						OnFragment(old_data, bytes, frag);

						// Delete queued message
						RegionAllocator::ii->Release(node);

						// And proceed on to next message
						++ack_id;
						node = next;
					}

					// Update receive queue state
					_recv_queue_head = node;
					_next_recv_expected_id = ack_id;
				}
				else if (diff > 0) // Message is due to arrive
				{
					QueueRecv(data+2, msg_bytes-2, ack_id, msg_type == TYPE_RELIABLE_FRAG);
				}
			}
			break;

		case TYPE_RELIABLE_ACK:
			if (msg_bytes >= 4 + 2)
			{
				/*
					--- Acknowledgment (48+ bits) ---
					 0 1 2 3 4 5 6 7 8 9 a b c d e f
					<-- LSB ----------------- MSB -->
					|  Receiver Bandwidth High (16) |
					---------------------------------
					|  Receiver Bandwidth Low (16)  |
					---------------------------------
					|     Next Expected ID (16)     |
					---------------------------------
					|      Start Of Range 1 (16)    | <-- Optional
					---------------------------------
					|       End Of Range 1 (16)     |
					---------------------------------
					|      Start Of Range 2 (16)    | <-- Optional
					---------------------------------
					|       End Of Range 2 (16)     |
					---------------------------------
					|              ...              | <-- Optional
					---------------------------------

					Receiver Bandwidth: Bytes per second that the sender
					should limit itself to.

					Next Expected ID: Next AckId that the receiver expects to see.
					Implicitly acknowledges all messages up to this one.

					Start Of Range, End Of Range: Inclusive ranges of IDs to acknowledge.
				*/

				u32 receiver_bandwidth_limit = getLE(*reinterpret_cast<u32*>( data ));

				// Ack ID list follows header
				u16 *ids = reinterpret_cast<u16*>( data + 4 );

				// First ID is the next sequenced ID the remote host is expecting
				u32 next_expected_id = ReconstructCounter<16>(_next_send_id, ids[0]);

				// If the next expected ID is ahead of the next send ID,
				if ((s32)(_next_send_id - next_expected_id) < 0)
				{
					WARN("Transport") << "Synchronization lost: Remote host is acknowledging a packet we haven't sent yet";
				}

				// TODO: Acknowledge up to expected id

				// For each remaining ID pair,
				u16 id_count = ((msg_bytes - 6) >> 2);
				while (id_count--)
				{
					u32 range_start = ReconstructCounter<16>(next_expected_id, getLE(*++ids));
					u32 range_end = ReconstructCounter<16>(next_expected_id, getLE(*++ids));

					// If the ranges are out of order,
					if ((s32)(range_end - range_start) < 0)
					{
						WARN("Transport") << "Synchronization lost: Remote host is acknowledging too large a range";
					}
					else
					{
						// TODO: Acknowledge range
					}
				}
			}
			break;
		}

		bytes -= msg_bytes;
		data += msg_bytes;
	}

	TransmitQueued();
}

void Transport::QueueRecv(u8 *data, u32 bytes, u32 ack_id, bool frag)
{
	RecvQueue *node = _recv_queue_head;
	RecvQueue *last = 0;

	while (node)
	{
		s32 diff = (s32)(ack_id - node->id);

		if (diff == 0)
		{
			// Ignore duplicate message
			return;
		}
		else if (diff < 0)
		{
			// Insert before this node
			break;
		}

		// Keep searching for insertion point
		node = node->next;
	}

	RecvQueue *new_node = reinterpret_cast<RecvQueue*>( RegionAllocator::ii->Acquire(sizeof(RecvQueue) + bytes) );

	if (!new_node)
	{
		WARN("Transport") << "Out of memory for incoming packet queue";
	}
	else
	{
		// Insert new data into queue
		new_node->bytes = frag ? (bytes | RecvQueue::FRAG_FLAG) : bytes;
		new_node->id = ack_id;
		new_node->next = node;
		if (last) last->next = new_node;
		else _recv_queue_head = new_node;

		u8 *new_data = GetTrailingBytes(new_node);
		memcpy(new_data, data, bytes);
	}
}

void Transport::OnFragment(u8 *data, u32 bytes, bool frag)
{
	if (frag)
	{
		if (_fragment_length)
		{
			// Fragment body

			if (bytes < _fragment_length - _fragment_offset)
			{
				memcpy(_fragment_buffer + _fragment_offset, data, bytes);
				_fragment_offset += bytes;
			}
		}
		else
		{
			// Fragment head

			// First 4 bytes of fragment will be overall fragment length

			if (bytes > 4)
			{
				u32 frag_length = getLE(*reinterpret_cast<u32*>( data ));
				data += 4;
				bytes -= 4;

				if (frag_length >= FRAG_MIN && frag_length <= FRAG_MAX && bytes < frag_length)
				{
					_fragment_buffer = new u8[frag_length];

					if (_fragment_buffer)
					{
						_fragment_length = frag_length;
						memcpy(_fragment_buffer, data, bytes);
						_fragment_offset = bytes;
					}
				}
			}
		}
	}
	else
	{
		if (_fragment_length)
		{
			// Fragment tail

			if (bytes == _fragment_length - _fragment_offset)
			{
				memcpy(_fragment_buffer + _fragment_offset, data, bytes);

				OnMessage(_fragment_buffer, _fragment_length);
			}

			delete []_fragment_buffer;
			_fragment_length = 0;
		}
		else
		{
			// Not fragmented

			OnMessage(data, bytes);
		}
	}
}

bool Transport::PostMTUDiscoveryRequest(ThreadPoolLocalStorage *tls, u32 payload_bytes)
{
	if (payload_bytes < MINIMUM_MTU - IPV6_HEADER_BYTES - UDP_HEADER_BYTES)
		return false;

	u32 buffer_bytes = payload_bytes + AuthenticatedEncryption::OVERHEAD_BYTES;

	u8 *buffer = GetPostBuffer(buffer_bytes);
	if (!buffer) return false;

	// Write header
	*reinterpret_cast<u16*>( buffer ) = getLE16((payload_bytes - 2) | (TYPE_MTU_PROBE << 11));

	// Fill contents with random data
	tls->csprng->Generate(buffer + 2, payload_bytes - 2);

	// Encrypt and send buffer
	return PostPacket(buffer, buffer_bytes, payload_bytes);
}

bool Transport::WriteMessage(TransportMode mode, u8 *msg, u32 bytes)
{
	bool success = false;

	switch (mode)
	{
	case MODE_UNRELIABLE:
		{
			// Post the packet immediately
			u32 msg_bytes = 2 + bytes;
			if (msg_bytes <= _max_payload_bytes)
			{
				u32 buf_bytes = msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES;
				u8 *buffer = GetPostBuffer(buf_bytes);
				if (buffer)
				{
					*reinterpret_cast<u16*>( buffer ) = getLE16(bytes | (TYPE_UNRELIABLE << 11));
					memcpy(buffer + 2, msg, bytes);

					AutoMutex lock(_send_lock);
					success = PostPacket(buffer, buf_bytes, msg_bytes);
				}
			}
		}
		break;
	case MODE_RELIABLE:
		{
			u32 msg_bytes = 2 + bytes;
			if (msg_bytes <= _max_payload_bytes)
			{
				// No need to fragment
				SendQueue *node = RegionAllocator::ii->Acquire(sizeof(SendQueue) + msg_bytes);
				if (node)
				{
					node->next = 0;
					node->bytes = msg_bytes;

					u8 *buffer = GetTrailingBytes(node);
					*reinterpret_cast<u16*>( buffer ) = getLE16(bytes | (TYPE_UNRELIABLE << 11));
					memcpy(buffer + 2, msg, bytes);

					AutoMutex lock(_send_lock);

					if (!_send_queue_head) _send_queue_head = node;
					else _send_queue_tail->next = node;
					_send_queue_tail = node;
				}
			}
		}
		break;
	}

	TransmitQueued();

	return success;
}

void Transport::WriteFlush()
{
}

// Called whenever a connection-related event occurs to simulate smooth
// and consistent transmission of messages queued for delivery
void Transport::TransmitQueued()
{
	// TODO: Retransmit messages that are lost

	// TODO: Transmit messages that are now ready to go

	// TODO: Transmit ACKs
}
