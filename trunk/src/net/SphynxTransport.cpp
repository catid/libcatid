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

Transport::Transport()
{
	_rtt = 1500;

	// Receive state
	CAT_OBJCLR(_next_recv_expected_id);

	CAT_OBJCLR(_fragment_length);

	CAT_OBJCLR(_got_reliable);

	CAT_OBJCLR(_recv_queue_head);
	CAT_OBJCLR(_recv_queue_tail);

	// Send state
	_writer_count = 0;

	CAT_OBJCLR(_next_send_id);

	_send_buffer = 0;
	_send_buffer_bytes = 0;

	CAT_OBJCLR(_send_queue_head);
	CAT_OBJCLR(_send_queue_tail);

	CAT_OBJCLR(_sent_list_head);
	CAT_OBJCLR(_sent_list_tail);
}

Transport::~Transport()
{
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

	for (int ii = 0; ii < NUM_STREAMS; ++ii)
	{
		// Release memory for receive queue
		RecvQueue *recv_node = _recv_queue_head[ii];
		while (recv_node)
		{
			RecvQueue *next = recv_node->next;
			RegionAllocator::ii->Release(recv_node);
			recv_node = next;
		}

		// Release memory for send queue
		SendQueue *send_node = _send_queue_head[ii];
		while (send_node)
		{
			SendQueue *next = send_node->next;
			RegionAllocator::ii->Release(send_node);
			send_node = next;
		}

		// Release memory for sent list
		SendQueue *sent_node = _sent_list_head[ii];
		while (sent_node)
		{
			SendQueue *next = sent_node->next;
			RegionAllocator::ii->Release(sent_node);
			sent_node = next;
		}
	}
}

void Transport::InitializePayloadBytes(bool ip6)
{
	u32 overhead = (ip6 ? IPV6_HEADER_BYTES : IPV4_HEADER_BYTES) + UDP_HEADER_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES;

	_max_payload_bytes = MINIMUM_MTU - overhead;
}

void Transport::OnDatagram(u8 *data, u32 bytes)
{
	BeginWrite();

	u32 ack_id = 0;
	u32 stream = 0;

	while (bytes >= 2)
	{
		u16 header = getLE(*reinterpret_cast<u16*>( data ));

		data += 2;
		bytes -= 2;

		u16 data_bytes = header & DATALEN_MASK;

		// If this message has an ACK-ID attached,
		if (header & I_MASK)
		{
			if (bytes < 1)
			{
				WARN("Transport") << "Truncated message ignored";
				break;
			}

			// Decode variable-length ACK-ID into ack_id and stream:

			u8 ida = *data++;
			--bytes;
			stream = ida & 3;
			ack_id = (ida >> 2) & 0x1f;

			if (ida & 0x80)
			{
				if (bytes < 1)
				{
					WARN("Transport") << "Truncated message ignored";
					break;
				}

				u8 idb = *data++;
				--bytes;
				ack_id |= (u32)(idb & 0x7f) << 5;

				if (idb & 0x80)
				{
					if (bytes < 1)
					{
						WARN("Transport") << "Truncated message ignored";
						break;
					}

					u8 idc = *data++;
					--bytes;
					ack_id |= (u32)(idc & 0x7f) << 12;

					if (idb & 0x80)
					{
						if (bytes < 1)
						{
							WARN("Transport") << "Truncated message ignored";
							break;
						}

						u8 idd = *data++;
						--bytes;
						ack_id |= (u32)idd << 19;

						ack_id = ReconstructCounter<27>(_next_recv_expected_id[stream], ack_id);
					}
					else
					{
						ack_id = ReconstructCounter<19>(_next_recv_expected_id[stream], ack_id);
					}
				}
				else
				{
					ack_id = ReconstructCounter<12>(_next_recv_expected_id[stream], ack_id);
				}
			}
			else
			{
				ack_id = ReconstructCounter<5>(_next_recv_expected_id[stream], ack_id);
			}
		}

		// If reliable message,
		if (header & R_MASK)
		{
			s32 diff = (s32)(ack_id - _next_recv_expected_id[stream]);

			// If message is next expected,
			if (diff == 0)
			{
				// Process it immediately
				if (data_bytes > 0)
				{
					u32 super_opcode = (header & SOP_MASK) >> SOP_SHIFT;

					if (super_opcode == SOP_DATA)
						OnMessage(data, data_bytes);
					else if (super_opcode == SOP_FRAG)
						OnFragment(data, data_bytes);
					else if (super_opcode == SOP_MTU_SET)
						OnMTUSet(data, data_bytes);
					else
						WARN("Transport") << "Invalid reliable super opcode ignored";
				}
				else
				{
					WARN("Transport") << "Zero-length reliable message ignored";
				}

				RunQueue(ack_id, stream);
			}
			else if (diff > 0) // Message is due to arrive
			{
				u32 super_opcode = (header & SOP_MASK) >> SOP_SHIFT;

				QueueRecv(data, data_bytes, ack_id, stream, super_opcode);
			}
			else
			{
				WARN("Transport") << "Ignored duplicate rolled reliable message";
			}
		}
		else // Unreliable message:
		{
			u32 super_opcode = (header & SOP_MASK) >> SOP_SHIFT;

			switch (super_opcode)
			{
			default:
				WARN("Transport") << "Invalid unreliable super opcode ignored";
				break;

			case SOP_DATA:
				// Process it immediately
				if (data_bytes > 0)
				{
					OnMessage(data, data_bytes);
				}
				else
				{
					WARN("Transport") << "Zero-length unreliable message ignored";
				}
				break;

			case SOP_ACK:
				OnACK(data, data_bytes);
				break;

			case SOP_MTU_PROBE:
				if (2 + data_bytes > _max_payload_bytes)
				{
					u16 payload_bytes = 2 + data_bytes;

					_max_payload_bytes = payload_bytes;

					u16 msg = getLE(payload_bytes);
					WriteReliable(STREAM_UNORDERED, &msg, sizeof(msg), SOP_MTU_SET);
				}
				break;

			case SOP_TIME_PING:
				if (data_bytes == 4)
				{
					// Parameter is endian-agnostic
					PostTimePong(*reinterpret_cast<u32*>( data ));
				}
				break;

			case SOP_TIME_PONG:
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

			case SOP_DISCO:
				OnDisconnect();
				break;
			}
		}

		bytes -= data_bytes;
		data += data_bytes;
	}

	EndWrite();
}

void Transport::RunQueue(u32 ack_id, u32 stream)
{
	// Cache node and ack_id on stack
	RecvQueue *node = _recv_queue_head[stream];
	RecvQueue *kill_node = node;
	++ack_id;

	// For each queued message that is now ready to go,
	while (node && node->id == ack_id)
	{
		// Grab the queued message
		u32 old_data_bytes = node->bytes & RecvQueue::BYTE_MASK;
		u8 *old_data = GetTrailingBytes(node);

		// Process fragment now
		if (old_data_bytes > 0)
		{
			if (node->bytes & RecvQueue::FRAG_FLAG)
				OnFragment(old_data, old_data_bytes);
			else
				OnMessage(old_data, old_data_bytes);

			// NOTE: Unordered stream writes zero-length messages
			// to the receive queue since it processes immediately
			// and does not need to store the data.
		}

		// And proceed on to next message
		++ack_id;
		node = node->next;
	}

	_recv_lock.Enter();

	// Update receive queue state
	_recv_queue_head[stream] = node;
	if (!node) _recv_queue_tail[stream] = 0;
	_next_recv_expected_id[stream] = ack_id;
	_got_reliable = true;

	_recv_lock.Leave();

	// Split deletion from processing to reduce lock contention
	while (kill_node != node)
	{
		RecvQueue *next = kill_node->next;

		// Delete queued message
		RegionAllocator::ii->Release(kill_node);

		kill_node = next;
	}
}

void Transport::QueueRecv(u8 *data, u32 data_bytes, u32 ack_id, u32 stream, u32 super_opcode)
{
	// Walk backwards from the end because we're probably receiving
	// a blast of messages after a drop.
	RecvQueue *node = _recv_queue_tail;
	RecvQueue *next = 0;

	// Search for queue insertion point
	while (node)
	{
		s32 diff = (s32)(ack_id - node->id);

		if (diff == 0)
		{
			// Ignore duplicate message
			WARN("Transport") << "Ignored duplicate queued reliable message";
			return;
		}
		else if (diff > 0)
		{
			// Insert after this node
			break;
		}

		// Keep searching for insertion point
		next = node;
		node = node->prev;
	}

	u32 stored_bytes;

	if (stream == STREAM_UNORDERED)
	{
		// Process it immediately
		if (data_bytes > 0)
		{
			if (super_opcode == SOP_DATA)
				OnMessage(data, data_bytes);
			else if (super_opcode == SOP_FRAG)
				OnFragment(data, data_bytes);
			else if (super_opcode == SOP_MTU_SET)
				OnMTUSet(data, data_bytes);
		}
		else
		{
			WARN("Transport") << "Zero-length reliable message ignored";
		}

		stored_bytes = 0;
	}
	else
	{
		stored_bytes = data_bytes;
	}

	RecvQueue *new_node = reinterpret_cast<RecvQueue*>(
		RegionAllocator::ii->Acquire(sizeof(RecvQueue) + stored_bytes) );
	if (!new_node)
	{
		WARN("Transport") << "Out of memory for incoming packet queue";
	}
	else
	{
		// Insert new data into queue
		new_node->bytes = (super_opcode == SOP_FRAG) ? (stored_bytes | RecvQueue::FRAG_FLAG) : stored_bytes;
		new_node->id = ack_id;
		new_node->prev = node;
		new_node->next = next;

		// Just need to protect writes to the list linkages
		_recv_lock.Enter();

		if (next) next->prev = new_node;
		else _recv_queue_tail[stream] = new_node;
		if (node) node->next = new_node;
		else _recv_queue_head[stream] = new_node;
		_got_reliable = true;

		_recv_lock.Leave();

		u8 *new_data = GetTrailingBytes(new_node);
		memcpy(new_data, data, data_bytes);
	}
}

void Transport::OnFragment(u8 *data, u32 bytes)
{
	// If fragment is starting,
	if (!_fragment_length)
	{
		if (bytes < 2)
		{
			WARN("Transport") << "Truncated message fragment head ignored";
		}
		else
		{
			u32 frag_length = getLE(*reinterpret_cast<u16*>( data ));

			if (frag_length == 0)
			{
				WARN("Transport") << "Zero-length fragmented message ignored";
				return;
			}

			data += 2;
			bytes -= 2;

			// Allocate fragment buffer
			_fragment_buffer = new u8[frag_length];
			if (!_fragment_buffer)
			{
				WARN("Transport") << "Out of memory: Unable to allocate fragment buffer";
				return;
			}
			else
			{
				_fragment_length = frag_length;
				_fragment_offset = 0;
			}
		}

		// Fall-thru to processing data part of fragment message:
	}

	u32 fragment_remaining = _fragment_length - _fragment_offset;

	// If the fragment is now complete,
	if (bytes >= fragment_remaining)
	{
		if (bytes > fragment_remaining)
		{
			WARN("Transport") << "Message fragment overflow truncated";
		}

		memcpy(_fragment_buffer + _fragment_offset, data, fragment_remaining);

		OnMessage(_fragment_buffer, _fragment_length);

		delete []_fragment_buffer;
		_fragment_length = 0;
	}
	else
	{
		memcpy(_fragment_buffer + _fragment_offset, data, bytes);
		_fragment_offset += bytes;
	}
}

void Transport::BeginWrite()
{
	// Atomically increment writer count
	Atomic::Add(&_writer_count, 1);
}

bool WriteUnreliable(u8 *msg, u32 bytes)
{
	u32 max_payload_bytes = _max_payload_bytes;
	u32 msg_bytes = 2 + data_bytes;

	// Fail on invalid input
	if (msg_bytes > max_payload_bytes)
	{
		WARN("Transport") << "Invalid input: Unreliable buffer size request too large";
		return 0;
	}

	AutoMutex lock(_send_lock);

	u32 send_buffer_bytes = _send_buffer_bytes;

	// If the growing send buffer cannot contain the new message,
	if (send_buffer_bytes + msg_bytes > max_payload_bytes)
	{
		u8 *old_send_buffer = _send_buffer;

		u8 *msg_buffer = GetPostBuffer(msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!msg_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to allocate unreliable post buffer";
			return 0;
		}

		*reinterpret_cast<u16*>( msg_buffer ) = getLE(header);

		_send_buffer = msg_buffer;
		_send_buffer_bytes = msg_bytes;

		lock.Release();

		if (!PostPacket(old_send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
		{
			WARN("Transport") << "Packet post failure during unreliable overflow";
		}

		return msg_buffer + 2;
	}
	else
	{
		// Create or grow buffer and write into it
		_send_buffer = ResizePostBuffer(_send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!_send_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to resize unreliable post buffer";
			_send_buffer_bytes = 0;
			return 0;
		}

		u8 *msg_buffer = _send_buffer + send_buffer_bytes;

		*reinterpret_cast<u16*>( msg_buffer ) = getLE(header);

		_send_buffer_bytes = send_buffer_bytes + msg_bytes;

		return msg_buffer + 2;
	}
}

bool WriteReliable(StreamMode, u8 *msg, u32 bytes, SuperOpcode super_opcode)
{

}

u8 *Transport::GetUnreliableBuffer(u32 data_bytes)
{
	u32 header = data_bytes | F_MASK | D_MASK;

	return GetUnreliableRawBuffer((u16)header, data_bytes);
}

u8 *Transport::GetReliableBuffer(StreamMode stream, u32 data_bytes)
{
	// Fail on invalid input
	if (stream == STREAM_UNORDERED)
	{
		if (data_bytes + 2 + 3 > _max_payload_bytes)
		{
			WARN("Transport") << "Invalid input: Unordered buffer size request too large";
			return 0;
		}
	}
	else
	{
		if (data_bytes > MAX_MESSAGE_DATALEN)
		{
			WARN("Transport") << "Invalid input: Stream buffer size request too large";
			return 0;
		}
	}

	// Allocate SendQueue object
	SendQueue *node = reinterpret_cast<SendQueue*>(
		RegionAllocator::ii->Acquire(sizeof(SendQueue) + data_bytes) );
	if (!node)
	{
		WARN("Transport") << "Out of memory: Unable to allocate sendqueue object"
		return 0;
	}

	// Fill the object
	node->bytes = data_bytes;
	node->offset = 0;
	node->id = 0;
	node->next = 0;
	u8 *data = GetTrailingBytes(node);

	// Lock send lock while adding to the back of the send queue
	AutoMutex lock(_send_lock);

	// Add to back of send queue
	SendQueue *tail = _send_queue_tail[stream];
	node->prev = tail;
	if (tail) tail->next = node;
	else _send_queue_head[stream] = node;
	_send_queue_tail[stream] = node;

	// Return pointer to data part
	return data;
}

void Transport::EndWrite()
{
	// If this is the last writer,
	if (Atomic::Add(&_writer_count, -1) == 1)
	{
		AutoMutex lock(_send_lock);

		// Use the same ts_firstsend for all messages delivered now, to insure they are clustered on retransmission
		u32 now = Clock::msec();

		// Cache send buffer
		u32 send_buffer_bytes = _send_buffer_bytes;
		u8 *send_buffer = _send_buffer;
		u32 max_payload_bytes = _max_payload_bytes;

		// For each reliable stream,
		for (int stream = 0; stream < NUM_STREAMS; ++stream)
		{
			// Cache stream
			SendQueue *node = _send_queue_head[stream];
			u32 ack_id = _next_send_id[stream];

			// Start sending ACK-ID for this stream
			u32 ack_id_overhead = 3;

			// For each message ready to go,
			while (node)
			{
				// Cache next pointer since node will be modified
				SendQueue *next = node->next;

				bool fragmented = node->offset != 0;

				do {
					u32 remaining_data_bytes = node->bytes - node->offset;
					u32 remaining_send_buffer = max_payload_bytes - send_buffer_bytes;
					u32 frag_overhead = 0;

					// If message would be fragmented,
					if (2 + ack_id_overhead + remaining_data_bytes > remaining_send_buffer)
					{
						// If it is worth fragmentation,
						if (remaining_send_buffer >= FRAG_THRESHOLD &&
							2 + 3 + remaining_data_bytes - remaining_send_buffer >= FRAG_THRESHOLD)
						{
							if (!fragmented)
							{
								frag_overhead = 2;
								fragmented = true;
							}
						}
						else // Not worth fragmentation
						{
							// Post accumulated send buffer
							if (!PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
							{
								WARN("Transport") << "Unable to post send buffer";
								continue; // Retry
							}

							// Reset state for empty send buffer
							send_buffer = 0;
							send_buffer_bytes = 0;
							remaining_send_buffer = max_payload_bytes;
							ack_id_overhead = 3;

							// If the message is still fragmented after emptying the send buffer,
							if (!fragmented && 2 + 3 + remaining_data_bytes > remaining_send_buffer)
							{
								frag_overhead = 2;
								fragmented = true;
							}
						}
					}

					// Calculate total bytes to write to the send buffer on this pass
					u32 overhead = 2 + ack_id_overhead + frag_overhead;
					u32 msg_bytes = overhead + remaining_data_bytes;
					u32 write_bytes = remaining_send_buffer;
					if (write_bytes > msg_bytes) write_bytes = msg_bytes;
					u32 data_bytes_to_copy = write_bytes - overhead;

					// Link to the end of the sent list (Expectation is that acks will be received for nodes close to the head first)
					SendQueue *tail = _sent_list_tail[stream];
					if (fragmented)
					{
						SendFrag *proxy = new (RegionAllocator::ii) SendFrag;
						if (!proxy)
						{
							WARN("Transport") << "Out of memory: Unable to allocate fragment node";
							continue; // Retry
						}
						else
						{
							proxy->id = ack_id;
							proxy->next = 0;
							proxy->prev = tail;
							proxy->bytes = data_bytes_to_copy;
							proxy->offset = node->offset;
							proxy->ts_firstsend = now;
							proxy->ts_lastsend = now;
							proxy->full_data = node;
							if (tail) tail->next = reinterpret_cast<SendQueue*>( proxy );
							else _sent_list_head[stream] = reinterpret_cast<SendQueue*>( proxy );
							_sent_list_tail[stream] = reinterpret_cast<SendQueue*>( proxy );
						}
					}
					else
					{
						node->id = ack_id;
						node->next = 0;
						node->prev = tail;
						node->ts_firstsend = now;
						node->ts_lastsend = now;
						if (tail) tail->next = node;
						else _sent_list_head[stream] = node;
						_sent_list_tail[stream] = node;
					}

					// Resize post buffer to contain the bytes that will be written
					send_buffer = ResizePostBuffer(send_buffer, send_buffer_bytes + write_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
					if (!send_buffer)
					{
						WARN("Transport") << "Out of memory: Unable to allocate send buffer";
						send_buffer_bytes = 0;
						continue; // Retry
					}

					// Generate header word
					u16 header = (write_bytes - ack_id_overhead) | (stream << STM_OFFSET) | D_MASK;
					if (ack_id_overhead) header |= I_MASK;
					if (fragmented) header |= F_MASK;

					// Write header
					u8 *msg = send_buffer;
					*reinterpret_cast<u16*>( msg ) = getLE16(header);
					msg += 2;

					// Write optional ACK-ID
					if (ack_id_overhead)
					{
						msg[0] = (u8)(ack_id >> 16);
						msg[1] = (u8)(ack_id >> 8);
						msg[2] = (u8)ack_id;
						++ack_id;
						ack_id_overhead = 0;
						msg += 3;
					}

					// Write optional fragment word
					if (frag_overhead)
					{
						*reinterpret_cast<u16*>( msg ) = getLE((u16)node->bytes);
						frag_overhead = 0;
						msg += 2;
					}

					// Copy data bytes
					memcpy(msg, GetTrailingBytes(node) + node->offset, data_bytes_to_copy);
					send_buffer_bytes += data_bytes_to_copy;
					node->offset += data_bytes_to_copy;

				} while (node->offset < node->bytes);

				if (node->offset > node->bytes)
				{
					WARN("Transport") << "Node offset somehow escaped";
				}

				node = next;
			} // walking send queue

			// Update send queue state for this stream
			_send_queue_tail[stream] = 0;
			_send_queue_head[stream] = 0;
			_next_send_id[stream] = ack_id;
		} // walking streams

		// If data remains to be delivered,
		if (send_buffer_bytes)
		{
			if (!PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
			{
				WARN("Transport") << "Unable to post final send buffer";
			}

			_send_buffer = 0;
			_send_buffer_bytes = 0;
		}
	}
}

void Transport::Retransmit(SendQueue *node)
{
	u32 max_payload_bytes = _max_payload_bytes;
	u32 msg_bytes = 2 + data_bytes;

	// Fail on invalid input
	if (msg_bytes > max_payload_bytes)
	{
		WARN("Transport") << "Invalid input: Unreliable buffer size request too large";
		return 0;
	}

	AutoMutex lock(_send_lock);

	u32 send_buffer_bytes = _send_buffer_bytes;

	// If the growing send buffer cannot contain the new message,
	if (send_buffer_bytes + msg_bytes > max_payload_bytes)
	{
		u8 *old_send_buffer = _send_buffer;

		u8 *msg_buffer = GetPostBuffer(msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!msg_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to allocate unreliable post buffer";
			return 0;
		}

		*reinterpret_cast<u16*>( msg_buffer ) = getLE(header);

		_send_buffer = msg_buffer;
		_send_buffer_bytes = msg_bytes;

		lock.Release();

		if (!PostPacket(old_send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
		{
			WARN("Transport") << "Packet post failure during unreliable overflow";
		}

		return msg_buffer + 2;
	}
	else
	{
		// Create or grow buffer and write into it
		_send_buffer = ResizePostBuffer(_send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!_send_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to resize unreliable post buffer";
			_send_buffer_bytes = 0;
			return 0;
		}

		u8 *msg_buffer = _send_buffer + send_buffer_bytes;

		*reinterpret_cast<u16*>( msg_buffer ) = getLE(header);

		_send_buffer_bytes = send_buffer_bytes + msg_bytes;

		return msg_buffer + 2;
	}
}

void Transport::TickTransport(ThreadPoolLocalStorage *tls, u32 now)
{
	u8 ack_packet[MAXIMUM_MTU];
	u32 packet_used = 0;
	u32 max_payload_bytes = _max_payload_bytes;

	BeginWrite();

	// Retransmit lost packets
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		_send_lock.Enter();

		SendQueue *node = _sent_list_head[stream];

		// For each sendqueue node that might be ready for a retransmission,
		while (node && (now - node->ts_firstsend) >= 4 * _rtt)
		{
			// If this node actually needs to be resent,
			if ((now - node->ts_lastsend) >= 4 * _rtt)
			{
			}
		}

		_send_lock.Leave();
	}

	// Acknowledge recent reliable packets
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_got_reliable[stream])
		{
			u8 *msg = &ack_packet[3];

			_recv_lock.Enter();

			u32 rollup = _next_recv_expected_id[stream];
			ack_packet[0] = (u8)(rollup >> 16);
			ack_packet[1] = (u8)((u16)rollup >> 8);
			ack_packet[2] = (u8)rollup;
			packet_used = 3;

			RecvQueue *node = _recv_queue_head[stream];
			if (!node)
				_recv_lock.Leave();
			else
			{
				u32 range_start = node->id;
				u32 range_end = node->id;
				node = node->next;

				while (node)
				{
					u32 id = node->id;

					if (id == range_end + 1)
					{
						range_end = id;
					}
					else
					{
						if (2 + packet_used + 6 > max_payload_bytes)
							break;

						// Inside range
						msg[0] = (u8)(range_start >> 16);
						msg[1] = (u8)((u16)range_start >> 8);
						msg[2] = (u8)range_start;
						msg[3] = (u8)(range_end >> 16);
						msg[4] = (u8)((u16)range_end >> 8);
						msg[5] = (u8)range_end;
						packet_used += 6;

						range_start = id;
						range_end = id;
					}

					node = node->next;
				}

				_recv_lock.Leave();

				if (2 + packet_used + 6 <= max_payload_bytes)
				{
					// Final range
					msg[0] = (u8)(range_start >> 16);
					msg[1] = (u8)((u16)range_start >> 8);
					msg[2] = (u8)range_start;
					packet_used += 3;
					if (range_start == range_end)
					{
						msg[3] = (u8)(range_end >> 16);
						msg[4] = (u8)((u16)range_end >> 8);
						msg[5] = (u8)range_end;
						packet_used += 3;
					}
				}
			}

			u8 *ack_msg = GetUnreliableRawBuffer((u16)(packet_used | (stream << STM_OFFSET)), packet_used);

			memcpy(ack_msg, msg, packet_used);
		}
	}

	EndWrite();
}

void Transport::OnMTUSet(u8 *data, u32 data_bytes)
{
	if (data_bytes == 2)
	{
		u16 max_payload_bytes = getLE(*reinterpret_cast<u16*>( data ));

		// Accept the new max payload bytes if it is larger
		if (_max_payload_bytes < max_payload_bytes)
			_max_payload_bytes = max_payload_bytes;
	}
	else
	{
		WARN("Transport") << "Truncated MTU Set";
	}
}

bool Transport::PostMTUProbe(ThreadPoolLocalStorage *tls, u16 payload_bytes)
{
	// Just an approximate lower bound -- not exact
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
	*reinterpret_cast<u16*>( buffer ) = getLE16(DATA_BYTES | (SOP_TIME_PING << SOP_SHIFT));
	*reinterpret_cast<u32*>( buffer + 2 ) = getLE32(Clock::msec());

	// Encrypt and send buffer
	return PostPacket(buffer, BUFFER_BYTES, PAYLOAD_BYTES);
}

bool Transport::PostTimePong(u32 client_ts)
{
	const u32 DATA_BYTES = 4 + 4;
	const u32 PAYLOAD_BYTES = 2 + DATA_BYTES;
	const u32 BUFFER_BYTES = PAYLOAD_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES;

	u8 *buffer = GetPostBuffer(BUFFER_BYTES);
	if (!buffer) return false;

	// Write Time Pong
	*reinterpret_cast<u16*>( buffer ) = getLE16(DATA_BYTES | (SOP_TIME_PONG << SOP_SHIFT));
	*reinterpret_cast<u32*>( buffer + 2 ) = client_ts;
	*reinterpret_cast<u32*>( buffer + 6 ) = getLE32(Clock::msec());

	// Encrypt and send buffer
	return PostPacket(buffer, BUFFER_BYTES, PAYLOAD_BYTES);
}
