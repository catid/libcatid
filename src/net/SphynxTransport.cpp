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
	_writer_count = 0;

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

void Transport::OnSuperMessage(u16 super_opcode, u8 *data, u32 data_bytes)
{
	switch (super_opcode)
	{
	case SOP_MTU_PROBE:		// MTU probe packet (unreliable)
		if (data_bytes + 2 > _max_payload_bytes)
		{
			u16 payload_bytes = data_bytes + 2;

			// Write MTU Update message
			u16 *out_data = reinterpret_cast<u16*>( GetReliableBuffer(2, SOP_MTU_UPDATE) );
			if (out_data) *out_data = getLE(payload_bytes);
			_max_payload_bytes = payload_bytes;
		}
		break;

	case SOP_MTU_UPDATE:	// MTU update (reliable)
		if (data_bytes == 2)
		{
			u16 max_payload_bytes = getLE(*reinterpret_cast<u16*>( data ));

			// Accept the new max payload bytes if it is larger
			if (_max_payload_bytes < max_payload_bytes)
				_max_payload_bytes = max_payload_bytes;
		}
		break;

	case SOP_TIME_PING:		// Time synchronization ping (unreliable)
		if (data_bytes == 4)
		{
			// Parameter is endian-agnostic
			PostTimePong(*reinterpret_cast<u32*>( data ));
		}
		break;

	case SOP_TIME_PONG:		// Time synchronization pong (unreliable)
		if (data_bytes == 8)
		{
			u32 client_now = Clock::msec();

			u32 *ts = reinterpret_cast<u32*>( data );
			u32 client_ts = getLE(ts[0]);
			u32 server_ts = getLE(ts[1]);

			u32 rtt = client_now - client_ts;

			// If RTT is not impossible,
			if (rtt < TIMEOUT_DISCONNECT)
			{
				s32 delta = (server_ts - (rtt >> 1)) - client_ts;

				OnTimestampDeltaUpdate(rtt, delta);
			}
		}
		break;

	case SOP_DATA:			// Data (reliable or unreliable)
		if (data_bytes > 0) OnMessage(data, data_bytes);
		break;

	case SOP_FRAG:			// Data fragment (reliable); initial fragment begins with 32-bit total length
		{
			// TODO: Handle fragment
		}
		break;

	case SOP_ACK:			// Acknowledgment of reliable, ordered messages (unreliable)
		if (data_bytes >= 3)
		{
			// TODO: Handle ACK
		}
		break;

	case SOP_NAK:			// Negative-acknowledgment of reliable, ordered messages (unreliable)
		if (data_bytes >= 6)
		{
			// TODO: Handle NAK
		}
		break;
	}

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
}

void Transport::OnDatagram(u8 *data, u32 bytes)
{
	BeginWrite();

	u32 ack_id = 0;

	while (bytes >= 2)
	{
		u16 header = getLE(*reinterpret_cast<u16*>( data ));

		data += 2;
		bytes -= 2;

		u16 data_bytes = header & 0x7ff;
		if (bytes < data_bytes) break;
		u16 is_reliable = (header >> 11) & 1;
		u16 update_ack_id = (header >> 12) & 1;
		u16 super_opcode = header >> 13;

		if (update_ack_id)
		{
			if (data_bytes < 3) break;

			ack_id = ((u32)data[0] << 16) | ((u16)data[1] << 8) | data[2];

			data += 3;
			bytes -= 3;
			data_bytes -= 3;
		}

		if (is_reliable)
		{
			// TODO: Check if ack_id is due
		}

		OnSuperMessage(super_opcode, data, data_bytes);

		bytes -= data_bytes;
		data += data_bytes;
	}

	EndWrite();
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

/*
bool Transport::PostMTUProbe(ThreadPoolLocalStorage *tls, u32 payload_bytes)
{
	if (payload_bytes < MINIMUM_MTU - IPV6_HEADER_BYTES - UDP_HEADER_BYTES)
		return false;

	u32 buffer_bytes = payload_bytes + AuthenticatedEncryption::OVERHEAD_BYTES;

	u8 *buffer = GetPostBuffer(buffer_bytes);
	if (!buffer) return false;

	// Write header
	*reinterpret_cast<u16*>( buffer ) = getLE16((payload_bytes - 2) | (SOP_MTU_PROBE << 13));

	// Fill contents with random data
	tls->csprng->Generate(buffer + 2, payload_bytes - 2);

	// Encrypt and send buffer
	return PostPacket(buffer, buffer_bytes, payload_bytes);
}

bool Transport::PostTimePing()
{
	const u32 DATA_BYTES = 4;
	const u32 PAYLOAD_BYTES = 2 + DATA_BYTES;
	const u32 BUFFER_BYTES = PAYLOAD_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES;

	u8 *buffer = GetPostBuffer(BUFFER_BYTES);
	if (!buffer) return false;

	// Write Time Ping
	*reinterpret_cast<u16*>( buffer ) = getLE16(DATA_BYTES | (SOP_TIME_PING << 13));
	*reinterpret_cast<u32*>( buffer + 2 ) = getLE32(Clock::msec());

	// Encrypt and send buffer
	return PostPacket(buffer, BUFFER_BYTES, PAYLOAD_BYTES);
}

bool Transport::PostTimePong(u32 client_ts)
{
	const u32 DATA_BYTES = 8;
	const u32 PAYLOAD_BYTES = 2 + DATA_BYTES;
	const u32 BUFFER_BYTES = PAYLOAD_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES;

	u8 *buffer = GetPostBuffer(BUFFER_BYTES);
	if (!buffer) return false;

	// Write Time Pong
	*reinterpret_cast<u16*>( buffer ) = getLE16(DATA_BYTES | (SOP_TIME_PONG << 13));
	*reinterpret_cast<u32*>( buffer + 2 ) = client_ts;
	*reinterpret_cast<u32*>( buffer + 6 ) = getLE32(Clock::msec());

	// Encrypt and send buffer
	return PostPacket(buffer, BUFFER_BYTES, PAYLOAD_BYTES);
}
*/

void Transport::BeginWrite()
{
	// Atomically increment writer count
	Atomic::Add(&_writer_count, 1);
}

u8 *Transport::GetReliableBuffer(u32 data_bytes, SuperOpCode sop)
{
	// Fail on invalid input
	if (data_bytes > MAX_MESSAGE_DATALEN) return 0;

	// Allocate SendQueue object
	SendQueue *node = reinterpret_cast<SendQueue*>(
		RegionAllocator::ii->Acquire(sizeof(SendQueue) + data_bytes) );
	if (!node) return 0; // Returns null on failure

	// Fill the object
	node->bytes = data_bytes;
	node->header = sop;
	node->id = 0;
	node->next = 0;
	u8 *data = GetTrailingBytes(node);

	// Lock send lock while adding to the back of the send queue
	AutoMutex lock(_send_lock);

	// Add to back of send queue
	if (_send_queue_tail) _send_queue_tail->next = node;
	else _send_queue_head = node;
	_send_queue_tail = node;

	// Return pointer to data part
	return data;
}

u8 *Transport::GetUnreliableBuffer(u32 data_bytes, SuperOpCode sop)
{
	u32 max_payload_bytes = _max_payload_bytes;
	u32 msg_bytes = 2 + data_bytes;

	// Fail on invalid input
	if (msg_bytes > max_payload_bytes) return 0;

	AutoMutex lock(_send_lock);

	u32 send_buffer_bytes = _send_buffer_bytes;

	// If the growing send buffer cannot contain the new message,
	if (send_buffer_bytes + msg_bytes > max_payload_bytes)
	{
		u8 *old_send_buffer = _send_buffer;

		u8 *msg_buffer = GetPostBuffer(msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!msg_buffer) return 0;

		*reinterpret_cast<u16*>( msg_buffer ) = getLE16(data_bytes | (sop << 13));

		_send_buffer = msg_buffer;
		_send_buffer_bytes = msg_bytes;

		lock.Release();

		// Post packet without checking return value
		PostPacket(old_send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes);

		return msg_buffer + 2;
	}
	else
	{
		// Create or grow buffer and write into it
		_send_buffer = ResizePostBuffer(_send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!_send_buffer)
		{
			_send_buffer_bytes = 0;
			return 0;
		}

		u8 *msg_buffer = _send_buffer + send_buffer_bytes;

		*reinterpret_cast<u16*>( msg_buffer ) = getLE16(data_bytes | (sop << 13));

		_send_buffer_bytes = send_buffer_bytes + msg_bytes;

		return msg_buffer + 2;
	}
}

// if decrement writer count == 0,
// lock send lock:
// first fill existing packet with resizing, setting I flag,
// then dq sets of reliable messages into packets, setting I flag for each new packet
// after whole SendQueue object is transmitted, move it to sent list
// if a SendQueue object is fragmented, allocate new SendQueue objects for sent list, with reference to the original
// assign ACK-IDs to the sent list version as they go out and mark when they were sent
void Transport::EndWrite()
{
	if (Atomic::Add(&_writer_count, -1) == 1)
	{
		AutoMutex lock(_send_lock);

		SendQueue *node = _send_queue_head;
		u32 send_buffer_bytes = _send_buffer_bytes;
		u8 *send_buffer = _send_buffer;
		u32 ack_id = _next_send_id;
		u32 overhead = 3; // for first ack_id
		u32 max_payload_bytes = _max_payload_bytes;

		const u32 R_MASK = 1 << 11;
		const u32 I_MASK = 1 << 12;

		while (node)
		{
			u32 offset = node->id;
			u32 data_bytes = node->bytes - offset;
			u32 msg_bytes = 2 + overhead + data_bytes;

			// If the growing send buffer cannot contain the new message,
			if (send_buffer_bytes + msg_bytes > max_payload_bytes)
			{
				// If fragmentation is worthwhile, leaving a decent number of bytes in both packets,
				if (max_payload_bytes >= send_buffer_bytes + FRAG_THRESHOLD &&
					send_buffer_bytes + msg_bytes - max_payload_bytes >= FRAG_THRESHOLD)
				{
					// Start fragmenting message
					send_buffer = ResizePostBuffer(send_buffer, max_payload_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
					if (!send_buffer)
					{
						// Failure and we lost the old buffer!
						send_buffer_bytes = 0;
					}
					else
					{
						u32 eat = max_payload_bytes - send_buffer_bytes;
						u32 copy_bytes = eat;

						u8 *msg = send_buffer + send_buffer_bytes;

						if (overhead)
						{
							*reinterpret_cast<u16*>( msg ) = getLE16(copy_bytes | R_MASK | I_MASK | (SOP_FRAG << 13));
							msg[2] = (u8)(ack_id >> 16);
							msg[3] = (u8)(ack_id >> 8);
							msg[4] = (u8)ack_id;
							*reinterpret_cast<u16*>( msg + 2 + 3 ) = getLE((u16)data_bytes);
							msg += 2 + 3 + 2;
							copy_bytes -= 2 + 3 + 2;

							++ack_id;
							overhead = 0;
							send_buffer_bytes += 2 + 3 + 2 + copy_bytes;
						}
						else
						{
							*reinterpret_cast<u16*>( msg ) = getLE16(copy_bytes | R_MASK | (SOP_FRAG << 13));
							*reinterpret_cast<u16*>( msg + 2 + 2 ) = getLE((u16)data_bytes);
							msg += 2 + 2;
							copy_bytes -= 2 + 2;
							send_buffer_bytes += 2 + 2 + copy_bytes;
						}

						node->id = copy_bytes;
						memcpy(msg, GetTrailingBytes(node), copy_bytes);
					}
				}
				else
				{
					// Post send buffer and start a new one
					PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes);

					if (msg_bytes > max_payload_bytes)
					{
						// Message will need to be fragmented starting with the new buffer

						send_buffer = GetPostBuffer(max_payload_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);


					}
					else
					{
						// Copy whole message into buffer without fragmentation
					}
				}
			}
			else // Growing send buffer can contain the new message
			{
				send_buffer = ResizePostBuffer(send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
				if (!send_buffer)
				{
					// Failure and we lost the old buffer!
					send_buffer_bytes = 0;
				}
				else
				{
					u8 *msg = send_buffer + send_buffer_bytes;

					if (overhead)
					{
						*reinterpret_cast<u16*>( msg ) = getLE16(data_bytes | R_MASK | I_MASK | (sop << 13));
						msg[2] = (u8)(ack_id >> 16);
						msg[3] = (u8)(ack_id >> 8);
						msg[4] = (u8)ack_id;
						msg += 2 + 3;

						++ack_id;
						overhead = 0;
					}
					else
					{
						*reinterpret_cast<u16*>( msg ) = getLE16(data_bytes | R_MASK | (sop << 13));
						msg += 2;
					}

					memcpy(msg, GetTrailingBytes(node), data_bytes);
					send_buffer_bytes += msg_bytes;
				}
			}
		}

		// If data remains to be delivered,
		if (send_buffer_bytes)
		{
			PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes);

			_send_buffer = 0;
			_send_buffer_bytes = 0;
			_next_send_id = ack_id;
		}
	}
}

// lock send lock:
// sent list is organized from oldest to youngest packet
// so walk the sent list and determine if any are considered lost yet
// resend lost packets -- all following packets with the same ack id are delivered out of band
void Transport::TickTransport(ThreadPoolLocalStorage *tls, u32 now)
{
}
