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
#include <cat/time/Clock.hpp>
using namespace cat;
using namespace sphynx;

Transport::Transport()
{
	// Receive state
	_next_recv_expected_id[STREAM_UNORDERED] = 0x11223344;
	_next_recv_expected_id[STREAM_1] = 0x55667788;
	_next_recv_expected_id[STREAM_2] = 0x99aabbcc;
	_next_recv_expected_id[STREAM_3] = 0xddeeff00;

	CAT_OBJCLR(_got_reliable);

	CAT_OBJCLR(_fragment_length);

	CAT_OBJCLR(_recv_queue_head);
	CAT_OBJCLR(_recv_queue_tail);

	// Send state
	_next_send_id[STREAM_UNORDERED] = 0x11223344;
	_next_send_id[STREAM_1] = 0x55667788;
	_next_send_id[STREAM_2] = 0x99aabbcc;
	_next_send_id[STREAM_3] = 0xddeeff00;

	_send_next_remote_expected[STREAM_UNORDERED] = 0x11223344;
	_send_next_remote_expected[STREAM_1] = 0x55667788;
	_send_next_remote_expected[STREAM_2] = 0x99aabbcc;
	_send_next_remote_expected[STREAM_3] = 0xddeeff00;

	_rtt = 1500;

	_send_buffer = 0;
	_send_buffer_bytes = 0;
	_send_buffer_stream = NUM_STREAMS;

	CAT_OBJCLR(_send_queue_head);
	CAT_OBJCLR(_send_queue_tail);

	CAT_OBJCLR(_sent_list_head);
	CAT_OBJCLR(_sent_list_tail);
}

Transport::~Transport()
{
	// Release memory for send buffer
	if (_send_buffer_bytes)
	{
		RegionAllocator::ii->Release(_send_buffer);
	}

	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		// Release memory for receive queue
		RecvQueue *recv_node = _recv_queue_head[stream];
		while (recv_node)
		{
			RecvQueue *next = recv_node->next;
			RegionAllocator::ii->Release(recv_node);
			recv_node = next;
		}

		// Release memory for fragment buffer
		if (_fragment_length[stream])
		{
			delete []_fragment_buffer[stream];
		}

		// Release memory for send queue
		SendQueue *send_node = _send_queue_head[stream];
		while (send_node)
		{
			SendQueue *next = send_node->next;
			RegionAllocator::ii->Release(send_node);
			send_node = next;
		}

		// Release memory for sent list
		SendQueue *sent_node = _sent_list_head[stream];
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
	u32 ack_id = 0;
	u32 stream = 0;

	INANE("Transport") << "Datagram dump " << bytes << ":" << HexDumpString(data, bytes);

	while (bytes >= 2)
	{
		u16 header = getLE(*reinterpret_cast<u16*>( data ));

		data += 2;
		bytes -= 2;

		// If this message has an ACK-ID attached,
		if (header & I_MASK)
		{
			if (bytes < 1)
			{
				WARN("Transport") << "Truncated message ignored (1)";
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
					WARN("Transport") << "Truncated message ignored (2)";
					break;
				}

				u8 idb = *data++;
				--bytes;
				ack_id |= (u32)(idb & 0x7f) << 5;

				if (idb & 0x80)
				{
					if (bytes < 1)
					{
						WARN("Transport") << "Truncated message ignored (3)";
						break;
					}

					u8 idc = *data++;
					--bytes;
					ack_id |= (u32)idc << 12;

					ack_id = ReconstructCounter<20>(_next_recv_expected_id[stream], ack_id);
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

		u32 data_bytes = header & DATALEN_MASK;
		if (bytes < data_bytes)
		{
			WARN("Transport") << "Truncated transport message ignored";
			break;
		}

		// If reliable message,
		if (header & R_MASK)
		{
			s32 diff = (s32)(ack_id - _next_recv_expected_id[stream]);

			// TODO: Don't update next expected id or run queue until after datagram is completed
			// TODO: While writing datagram, use better compression for interleaved streams -- should just be one byte -- and make sure they are all using the same ack id
			// TODO: QueueRecv() should accept messages with the same ack id from the same datagram

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
						OnFragment(data, data_bytes, stream);
					else if (super_opcode == SOP_MTU_SET)
						OnMTUSet(data, data_bytes);
					else
						WARN("Transport") << "Invalid reliable super opcode ignored";
				}
				else
				{
					WARN("Transport") << "Zero-length reliable message ignored";
				}

				RunQueue(ack_id + 1, stream);
			}
			else if (diff > 0) // Message is due to arrive
			{
				u32 super_opcode = (header & SOP_MASK) >> SOP_SHIFT;

				QueueRecv(data, data_bytes, ack_id, stream, super_opcode);
			}
			else
			{
				WARN("Transport") << "Ignored duplicate rolled reliable message";

				// Don't bother locking here: It's okay if we lose a race with this.
				_got_reliable[stream] = true;
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
				INFO("Transport") << "Got ACK";
				OnACK(data, data_bytes);
				break;

			case SOP_MTU_PROBE:
				INFO("Transport") << "Got MTU Probe.  Max payload bytes = " << 2 + data_bytes;
				if (2 + data_bytes > _max_payload_bytes)
				{
					u16 payload_bytes = 2 + data_bytes;

					_max_payload_bytes = payload_bytes;

					u16 msg = getLE(payload_bytes);
					WriteReliable(STREAM_UNORDERED, reinterpret_cast<u8*>( &msg ), sizeof(msg), SOP_MTU_SET);
				}
				break;

			case SOP_TIME_PING:
				INFO("Transport") << "Got Time Ping";
				if (data_bytes == 4)
				{
					// Parameter is endian-agnostic
					PostTimePong(*reinterpret_cast<u32*>( data ));
				}
				break;

			case SOP_TIME_PONG:
				INFO("Transport") << "Got Time Pong";
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

	FlushWrite();
}

void Transport::RunQueue(u32 ack_id, u32 stream)
{
	RecvQueue *node = _recv_queue_head[stream];

	if (!node || node->id != ack_id)
	{
		_recv_lock.Enter();

		_next_recv_expected_id[stream] = ack_id;
		_got_reliable[stream] = true;

		_recv_lock.Leave();

		return;
	}

	RecvQueue *kill_node = node;

	// For each queued message that is now ready to go,
	do
	{
		// Grab the queued message
		u32 old_data_bytes = node->bytes & RecvQueue::BYTE_MASK;
		u8 *old_data = GetTrailingBytes(node);

		// Process fragment now
		if (old_data_bytes > 0)
		{
			INFO("Transport") << "Running queued message # " << stream << ":" << ack_id;

			if (node->bytes & RecvQueue::FRAG_FLAG)
				OnFragment(old_data, old_data_bytes, stream);
			else
				OnMessage(old_data, old_data_bytes);

			// NOTE: Unordered stream writes zero-length messages
			// to the receive queue since it processes immediately
			// and does not need to store the data.
		}

		// And proceed on to next message
		++ack_id;
		node = node->next;
	} while (node && node->id == ack_id);

	_recv_lock.Enter();

	// Update receive queue state
	_recv_queue_head[stream] = node;
	if (!node) _recv_queue_tail[stream] = 0;
	_next_recv_expected_id[stream] = ack_id;
	_got_reliable[stream] = true;

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
	RecvQueue *node = _recv_queue_tail[stream];
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

	INFO("Transport") << "Queued out-of-order message # " << stream << ":" << ack_id;

	u32 stored_bytes;

	if (stream == STREAM_UNORDERED)
	{
		// Process it immediately
		if (data_bytes > 0)
		{
			if (super_opcode == SOP_DATA)
				OnMessage(data, data_bytes);
			else if (super_opcode == SOP_FRAG)
				OnFragment(data, data_bytes, stream);
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
		_got_reliable[stream] = true;

		_recv_lock.Leave();

		u8 *new_data = GetTrailingBytes(new_node);
		memcpy(new_data, data, data_bytes);
	}
}

void Transport::OnFragment(u8 *data, u32 bytes, u32 stream)
{
	// If fragment is starting,
	if (!_fragment_length[stream])
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
			_fragment_buffer[stream] = new u8[frag_length];
			if (!_fragment_buffer[stream])
			{
				WARN("Transport") << "Out of memory: Unable to allocate fragment buffer";
				return;
			}
			else
			{
				_fragment_length[stream] = frag_length;
				_fragment_offset[stream] = 0;
			}
		}

		// Fall-thru to processing data part of fragment message:
	}

	u32 fragment_remaining = _fragment_length[stream] - _fragment_offset[stream];

	// If the fragment is now complete,
	if (bytes >= fragment_remaining)
	{
		if (bytes > fragment_remaining)
		{
			WARN("Transport") << "Message fragment overflow truncated";
		}

		memcpy(_fragment_buffer[stream] + _fragment_offset[stream], data, fragment_remaining);

		OnMessage(_fragment_buffer[stream], _fragment_length[stream]);

		delete []_fragment_buffer[stream];
		_fragment_length[stream] = 0;
	}
	else
	{
		memcpy(_fragment_buffer[stream] + _fragment_offset[stream], data, bytes);
		_fragment_offset[stream] += bytes;
	}
}

bool Transport::WriteUnreliable(u8 *data, u32 data_bytes)
{
	u32 max_payload_bytes = _max_payload_bytes;
	u32 msg_bytes = 2 + data_bytes;

	// Fail on invalid input
	if (msg_bytes > max_payload_bytes)
	{
		WARN("Transport") << "Invalid input: Unreliable buffer size request too large";
		return false;
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
			return false;
		}

		*reinterpret_cast<u16*>( msg_buffer ) = getLE16(data_bytes | (SOP_DATA << SOP_SHIFT));
		memcpy(msg_buffer + 2, data, data_bytes);
		_send_buffer = msg_buffer;
		_send_buffer_bytes = msg_bytes;
		_send_buffer_stream = NUM_STREAMS;

		lock.Release();

		if (!PostPacket(old_send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
		{
			WARN("Transport") << "Packet post failure during unreliable overflow";
		}
	}
	else
	{
		// Create or grow buffer and write into it
		_send_buffer = ResizePostBuffer(_send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!_send_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to resize unreliable post buffer";
			_send_buffer_bytes = 0;
			return false;
		}

		u8 *msg_buffer = _send_buffer + send_buffer_bytes;

		*reinterpret_cast<u16*>( msg_buffer ) = getLE16(data_bytes | (SOP_DATA << SOP_SHIFT));
		memcpy(msg_buffer + 2, data, data_bytes);
		_send_buffer_bytes = send_buffer_bytes + msg_bytes;
	}

	return true;
}

void Transport::FlushWrite()
{
	TransmitQueued();

	AutoMutex lock(_send_lock);

	u8 *send_buffer = _send_buffer;

	if (send_buffer)
	{
		u32 send_buffer_bytes = _send_buffer_bytes;

		_send_buffer = 0;
		_send_buffer_bytes = 0;
		_send_buffer_stream = NUM_STREAMS;

		lock.Release();

		if (!PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
		{
			WARN("Transport") << "Packet post failure during flush write";
		}
	}
}

void Transport::Retransmit(u32 stream, SendQueue *node, u32 now)
{
	/*
		On retransmission we cannot use ACK-ID compression
		because we do not have any bound on the next
		expected id on the receiver.

		This means that messages that were under MTU on the
		initial transmission might be larger than MTU on
		retransmission.  To avoid this potential issue,
		copy 2 fewer bytes on initial transmission.
	*/

	u8 *data;
	u16 data_bytes = node->bytes;
	u16 header = data_bytes | R_MASK;

	// If node is a fragment,
	if (node->frag_count)
	{
		SendFrag *frag = reinterpret_cast<SendFrag*>( node );

		data = GetTrailingBytes(frag->full_data) + frag->offset;
		header |= SOP_FRAG << SOP_SHIFT;
	}
	else
	{
		data = GetTrailingBytes(node);
		header |= node->sop << SOP_SHIFT;
	}

	u32 ack_id = node->id;
	u32 msg_bytes = 2 + data_bytes;

	// If ACK-ID needs to be written again,
	if (_send_buffer_stream != stream ||
		_send_buffer_ack_id != ack_id)
	{
		header |= I_MASK;
		_send_buffer_ack_id = ack_id;
		_send_buffer_stream = stream;
		msg_bytes += 3;
	}

	u32 max_payload_bytes = _max_payload_bytes;

	// Fail on invalid input
	if (msg_bytes > max_payload_bytes)
	{
		WARN("Transport") << "Retransmit failure: Reliable message too large";
		return;
	}

	u8 *send_buffer = _send_buffer;
	u32 send_buffer_bytes = _send_buffer_bytes;

	// If the growing send buffer cannot contain the new message,
	if (send_buffer_bytes + msg_bytes > max_payload_bytes)
	{
		if (!PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
		{
			WARN("Transport") << "Packet post failure during retransmit overflow";
		}

		send_buffer = 0;
		send_buffer_bytes = 0;

		if (!(header & I_MASK))
		{
			header |= I_MASK;
			msg_bytes += 3;
		}
	}

	// Create or grow buffer and write into it
	_send_buffer = ResizePostBuffer(send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
	if (!_send_buffer)
	{
		WARN("Transport") << "Out of memory: Unable to resize post buffer";
		_send_buffer_bytes = 0;
		_send_buffer_stream = NUM_STREAMS;
		return;
	}

	u8 *msg = _send_buffer + send_buffer_bytes;

	*reinterpret_cast<u16*>( msg ) = getLE16(header);
	msg += 2;

	if (header & I_MASK)
	{
		msg[0] = (u8)(stream | ((ack_id & 31) << 2) | 0x80);
		msg[1] = (u8)((ack_id >> 5) | 0x80);
		msg[2] = (u8)(ack_id >> 12);
		msg += 3;
	}

	memcpy(msg, data, data_bytes);

	_send_buffer_bytes = send_buffer_bytes + msg_bytes;

	node->ts_lastsend = now;
}

void Transport::WriteACK()
{
	u8 packet[MAXIMUM_MTU];
	u16 *header = reinterpret_cast<u16*>( packet );
	u8 *offset = packet + 2;
	u32 max_payload_bytes = _max_payload_bytes;
	u32 remaining = max_payload_bytes - 2;

	_recv_lock.Enter();

	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_got_reliable[stream])
		{
			// Truncates ACK message if needed.
			// This is mitigated by not unsetting _got_reliable, so
			// next tick perhaps the rest of the ACK list can be sent.
			if (remaining < 3) break;

			u32 rollup_ack_id = _next_recv_expected_id[stream];

			// Write ROLLUP
			offset[0] = (u8)(1 | (stream << 1) | ((rollup_ack_id & 31) << 3));
			offset[1] = (u8)(rollup_ack_id >> 5);
			offset[2] = (u8)(rollup_ack_id >> 13);
			offset += 3;
			remaining -= 3;

			INFO("Transport") << "Acknowledging rollup # " << rollup_ack_id;

			RecvQueue *node = _recv_queue_head[stream];

			if (node)
			{
				u32 start_id = node->id;
				u32 end_id = start_id;
				u32 last_id = rollup_ack_id;

				node = node->next;

				while (node)
				{
					// If range continues,
					if (node->id == end_id + 1)
					{
						++end_id;
					}
					else // New range
					{
						// Encode RANGE: START(3) || END(3)
						if (remaining < 6) break;

						u32 start_offset = start_id - last_id;
						u32 end_offset = end_id - start_id;
						last_id = end_id;

						INFO("Transport") << "Acknowledging range # " << start_id << " - " << end_id;

						// Write START
						if (start_offset & ~0x1f)
						{
							offset[0] = (u8)((end_offset ? 2 : 0) | (start_offset << 2) | 0x80);

							if (start_offset & ~0xfff)
							{
								offset[1] = (u8)((start_offset >> 5) | 0x80);
								offset[2] = (u8)(start_offset >> 12);
								offset += 3;
								remaining -= 3;
							}
							else
							{
								offset[1] = (u8)(start_offset >> 5);
								offset += 2;
								remaining -= 2;
							}
						}
						else
						{
							*offset++ = (u8)((end_offset ? 2 : 0) | (start_offset << 2));
							--remaining;
						}

						// Write END
						if (end_offset)
						{
							if (end_offset & ~0x7f)
							{
								offset[0] = (u8)(end_offset | 0x80);

								if (end_offset & ~0x3fff)
								{
									offset[1] = (u8)((end_offset >> 7) | 0x80);
									offset[2] = (u8)(end_offset >> 14);
									offset += 3;
									remaining -= 3;
								}
								else
								{
									offset[1] = (u8)(end_offset >> 7);
									offset += 2;
									remaining -= 2;
								}
							}
							else
							{
								*offset++ = (u8)end_offset;
								--remaining;
							}
						}

						// Begin new range
						start_id = node->id;
						end_id = start_id;
					}

					node = node->next;
				}
			}

			// If we exhausted all in the list, unset flag
			if (!node) _got_reliable[stream] = false;
		}
	}

	_recv_lock.Leave();

	// Write header
	u32 msg_bytes = max_payload_bytes - remaining;
	*header = getLE16((msg_bytes - 2) | (SOP_ACK << SOP_SHIFT));

	// Post message:

	AutoMutex lock(_send_lock);

	u32 send_buffer_bytes = _send_buffer_bytes;

	// If the growing send buffer cannot contain the new message,
	if (send_buffer_bytes + msg_bytes > max_payload_bytes)
	{
		u8 *old_send_buffer = _send_buffer;

		u8 *msg_buffer = GetPostBuffer(msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!msg_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to allocate ACK post buffer";
		}
		else
		{
			memcpy(msg_buffer, packet, msg_bytes);
			_send_buffer = msg_buffer;
			_send_buffer_bytes = msg_bytes;

			lock.Release();

			if (!PostPacket(old_send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes))
			{
				WARN("Transport") << "Packet post failure during ACK send buffer overflow";
			}
		}
	}
	else
	{
		// Create or grow buffer and write into it
		_send_buffer = ResizePostBuffer(_send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!_send_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to resize ACK post buffer";
			_send_buffer_bytes = 0;
		}
		else
		{
			memcpy(_send_buffer + send_buffer_bytes, packet, msg_bytes);
			_send_buffer_bytes = send_buffer_bytes + msg_bytes;
		}
	}
}

void Transport::TickTransport(ThreadPoolLocalStorage *tls, u32 now)
{
	// Acknowledge recent reliable packets
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_got_reliable[stream])
		{
			WriteACK();
			break;
		}
	}

	_send_lock.Enter();

	// Retransmit lost packets
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		SendQueue *node = _sent_list_head[stream];

		// For each sendqueue node that might be ready for a retransmission,
		while (node && (now - node->ts_firstsend) >= 4 * _rtt)
		{
			// If this node actually needs to be resent,
			if ((now - node->ts_lastsend) >= 4 * _rtt)
				Retransmit(stream, node, now);

			node = node->next;
		}
	}

	_send_lock.Leave();

	// Implies that send buffer will get flushed at least once every tick period
	// This allows writers to be lazy about transmission!
	FlushWrite();
}

void Transport::OnMTUSet(u8 *data, u32 data_bytes)
{
	INFO("Transport") << "Got MTU Set";

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
	INANE("Transport") << "Posting MTU Probe";

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
	INANE("Transport") << "Posting Time Ping";

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
	INANE("Transport") << "Posting Time Pong";

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

bool Transport::PostDisconnect()
{
	INANE("Transport") << "Posting Disconnect";

	const u32 DATA_BYTES = 0;
	const u32 PAYLOAD_BYTES = 2 + DATA_BYTES;
	const u32 BUFFER_BYTES = PAYLOAD_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES;

	u8 *buffer = GetPostBuffer(BUFFER_BYTES);
	if (!buffer) return false;

	// Write Disconnect
	*reinterpret_cast<u16*>( buffer ) = getLE16(DATA_BYTES | (SOP_DISCO << SOP_SHIFT));

	// Encrypt and send buffer
	return PostPacket(buffer, BUFFER_BYTES, PAYLOAD_BYTES);
}

void Transport::OnACK(u8 *data, u32 data_bytes)
{
	u32 stream = NUM_STREAMS, last_ack_id = 0;
	SendQueue *node = 0, *kill_list = 0;
	u32 now = Clock::msec();

	AutoMutex lock(_send_lock);

	while (data_bytes > 0)
	{
		u8 ida = *data++;
		--data_bytes;

		// If field is ROLLUP,
		if (ida & 1)
		{
			if (data_bytes >= 2)
			{
				u8 idb = data[0];
				u8 idc = data[1];
				data += 2;
				data_bytes -= 2;

				// Retransmit lost packets
				if (stream < NUM_STREAMS)
				{
					// Just saw the end of a stream's ACK list.
					// We can now detect losses: Any node that is under
					// last_ack_id that still remains in the sent list
					// is probably lost.
					// TODO: Check if it causes too many retransmissions

					SendQueue *rnode = _sent_list_head[stream];

					if (rnode)
					{
						while ((s32)(last_ack_id - rnode->id) > 0)
						{
							if (now - rnode->ts_lastsend > _rtt)
								Retransmit(stream, rnode, now);

							rnode = rnode->next;
							if (!rnode) break;
						}
					}
				}

				stream = (ida >> 1) & 3;
				u32 ack_id = ((u32)idc << 13) | ((u16)idb << 5) | (ida >> 3);

				node = _sent_list_head[stream];

				if (node)
				{
					ack_id = ReconstructCounter<21>(node->id, ack_id);

					// Update the send next remote expected ack id
					_send_next_remote_expected[stream] = ack_id;

					last_ack_id = ack_id;

					INFO("Transport") << "Got acknowledgment for rollup # " << ack_id;

					// If the id got rolled,
					if ((s32)(ack_id - node->id) > 0)
					{
						// For each rolled node,
						do
						{
							SendQueue *next = node->next;

							// If this node is just a fragment of a larger message,
							if (node->frag_count)
							{
								SendQueue *full_data_node = (reinterpret_cast<SendFrag*>( node ))->full_data;
								u16 frag_count = full_data_node->frag_count;

								// Release the larger message after all fragments are released
								if (frag_count == 1)
								{
									// Insert into kill list
									full_data_node->next = kill_list;
									kill_list = full_data_node;
								}
								else
									full_data_node->frag_count = frag_count - 1;
							}

							// Insert into kill list
							node->next = kill_list;
							kill_list = node;

							node = next;
						} while (node && (s32)(ack_id - node->id) > 0);

						// Update list
						if (node) node->prev = 0;
						else _sent_list_tail[stream] = 0;
						_sent_list_head[stream] = node;
					}
				}
			}
			else
			{
				WARN("Transport") << "Truncated ACK ignored(1)";
				break;
			}
		}
		else // Field is RANGE
		{
			// Parse START:
			bool has_end = (ida & 2) != 0;
			u32 start_ack_id = last_ack_id + ((ida >> 2) & 31);

			if (ida & 0x80)
			{
				if (data_bytes >= 1)
				{
					u8 idb = *data++;
					--data_bytes;

					start_ack_id += (u16)(idb & 0x7f) << 5;

					if (idb & 0x80)
					{
						if (data_bytes >= 1)
						{
							u8 idc = *data++;
							--data_bytes;

							start_ack_id += (u32)idc << 12;
						}
						else
						{
							WARN("Transport") << "Truncated ACK ignored(2)";
							break;
						}
					}
				}
				else
				{
					WARN("Transport") << "Truncated ACK ignored(3)";
					break;
				}
			}

			// Parse END:
			u32 end_ack_id = start_ack_id;

			if (has_end)
			{
				if (data_bytes >= 1)
				{
					u8 ida1 = *data++;
					--data_bytes;

					end_ack_id += ida1 & 0x7f;

					if (ida1 & 0x80)
					{
						if (data_bytes >= 1)
						{
							u8 idb = *data++;
							--data_bytes;

							end_ack_id += (u16)(idb & 0x7f) << 7;

							if (idb & 0x80)
							{
								if (data_bytes >= 1)
								{
									u8 idc = *data++;
									--data_bytes;

									end_ack_id += (u32)idc << 14;
								}
								else
								{
									WARN("Transport") << "Truncated ACK ignored(4)";
									break;
								}
							}
						}
						else
						{
							WARN("Transport") << "Truncated ACK ignored(5)";
							break;
						}
					}
				}
				else
				{
					WARN("Transport") << "Truncated ACK ignored(6)";
					break;
				}
			}

			INFO("Transport") << "Got acknowledgment for range # " << start_ack_id << " - " << end_ack_id;

			// Handle range:
			if (node)
			{
				u32 ack_id = node->id;

				// Skip through sent list under range start
				while ((s32)(ack_id - start_ack_id) < 0)
				{
					node = node->next;
					if (!node) break;
					ack_id = node->id;
				}

				// Remaining nodes are in or over the range
				if (node && (s32)(end_ack_id - ack_id) >= 0)
				{
					SendQueue *prev = node->prev;

					// While nodes are in range,
					do 
					{
						SendQueue *next = node->next;

						// Insert into kill list
						node->next = kill_list;
						kill_list = node;

						node = next;
						if (!node) break;
						ack_id = node->id;
					} while ((s32)(end_ack_id - ack_id) >= 0);

					// Remove killed from sent list
					if (prev) prev->next = node;
					else _sent_list_head[stream] = node;
					if (!node) _sent_list_tail[stream] = prev;
				}

				// Next range start is offset from the end of this range
				last_ack_id = end_ack_id;
			}
		}
	}

	// Retransmit lost packets
	if (stream < NUM_STREAMS)
	{
		SendQueue *rnode = _sent_list_head[stream];

		if (rnode)
		{
			while ((s32)(last_ack_id - rnode->id) > 0)
			{
				if (now - rnode->ts_lastsend > _rtt)
					Retransmit(stream, rnode, now);

				rnode = rnode->next;
				if (!rnode) break;
			}
		}
	}

	lock.Release();

	// Release kill list after lock is released
	while (kill_list)
	{
		SendQueue *next = kill_list->next;
		RegionAllocator::ii->Release(kill_list);
		kill_list = next;
	}
}

bool Transport::WriteReliable(StreamMode stream, u8 *data, u32 data_bytes, SuperOpcode super_opcode)
{
	// Fail on invalid input
	if (stream == STREAM_UNORDERED)
	{
		if (2 + 3 + data_bytes > _max_payload_bytes)
		{
			WARN("Transport") << "Invalid input: Unordered buffer size request too large";
			return false;
		}
	}
	else
	{
		if (data_bytes > MAX_MESSAGE_DATALEN)
		{
			WARN("Transport") << "Invalid input: Stream buffer size request too large";
			return false;
		}
	}

	// Allocate SendQueue object
	SendQueue *node = reinterpret_cast<SendQueue*>(
		RegionAllocator::ii->Acquire(sizeof(SendQueue) + data_bytes) );
	if (!node)
	{
		WARN("Transport") << "Out of memory: Unable to allocate sendqueue object";
		return false;
	}

	// Fill the object
	node->bytes = data_bytes;
	node->frag_count = 0;
	node->sop = super_opcode;
	node->sent_bytes = 0;
	node->next = 0;
	memcpy(GetTrailingBytes(node), data, data_bytes);

	_send_lock.Enter();

	// Add to back of send queue
	SendQueue *tail = _send_queue_tail[stream];
	node->prev = tail;
	if (tail) tail->next = node;
	else _send_queue_head[stream] = node;
	_send_queue_tail[stream] = node;

	_send_lock.Leave();

	return true;
}

void Transport::TransmitQueued()
{
	// Use the same ts_firstsend for all messages delivered now, to insure they are clustered on retransmission
	u32 now = Clock::msec();
	u32 max_payload_bytes = _max_payload_bytes;

	// List of packets to send after done with send lock
	TempSendNode *packet_send_head = 0, *packet_send_tail = 0;

	AutoMutex lock(_send_lock);

	// Cache send buffer
	u32 send_buffer_bytes = _send_buffer_bytes;
	u8 *send_buffer = _send_buffer;

	// For each reliable stream,
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		SendQueue *node = _send_queue_head[stream];
		if (!node) continue;

		u32 ack_id = _next_send_id[stream];
		u32 remote_expected = _send_next_remote_expected[stream];
		u32 ack_id_overhead = 0, ack_id_bytes;

		u32 diff = ack_id - remote_expected;
		if (diff < 16)			ack_id_bytes = 1;
		else if (diff < 2048)	ack_id_bytes = 2;
		else					ack_id_bytes = 3;

		// If ACK-ID needs to be sent,
		if (_send_buffer_ack_id != ack_id ||
			_send_buffer_stream != stream)
		{
			ack_id_overhead = ack_id_bytes;
		}

		// For each message ready to go,
		while (node)
		{
			INFO("Transport") << "Delivering queued message # " << ack_id;

			// Cache next pointer since node will be modified
			SendQueue *next = node->next;
			bool fragmented = node->sop == SOP_FRAG;
			u32 sent_bytes = node->sent_bytes, total_bytes = node->bytes;

			do {
				u32 remaining_data_bytes = total_bytes - sent_bytes;
				u32 remaining_send_buffer = max_payload_bytes - send_buffer_bytes;
				u32 frag_overhead = 0;

				// If message would be fragmented,
				if (2 + ack_id_overhead + remaining_data_bytes > remaining_send_buffer)
				{
					// If it is worth fragmentation,
					if (remaining_send_buffer >= FRAG_THRESHOLD &&
						2 + ack_id_bytes + remaining_data_bytes - remaining_send_buffer >= FRAG_THRESHOLD)
					{
						if (!fragmented)
						{
							frag_overhead = 2;
							fragmented = true;
						}
					}
					else // Not worth fragmentation
					{
						// Prepare a temp send node for the old packet send buffer
						TempSendNode *node = reinterpret_cast<TempSendNode*>( send_buffer + send_buffer_bytes );
						node->next = 0;
						node->negative_offset = send_buffer_bytes;

						// Insert old packet send buffer at the end of the temp send list
						if (packet_send_tail) packet_send_tail->next = node;
						else packet_send_head = node;
						packet_send_tail = node;

						// Reset state for empty send buffer
						send_buffer = 0;
						send_buffer_bytes = 0;
						remaining_send_buffer = max_payload_bytes;
						ack_id_overhead = ack_id_bytes;

						// If the message is still fragmented after emptying the send buffer,
						if (!fragmented && 2 + ack_id_bytes + remaining_data_bytes > remaining_send_buffer)
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

				// Limit size to allow ACK-ID decompression during retransmission
				u32 retransmit_limit = max_payload_bytes - (3 - ack_id_overhead);
				if (write_bytes > retransmit_limit) write_bytes = retransmit_limit;

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
						proxy->offset = sent_bytes;
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
				u16 header = (data_bytes_to_copy + frag_overhead) | R_MASK;
				if (ack_id_overhead) header |= I_MASK;
				if (fragmented) header |= SOP_FRAG << SOP_SHIFT;
				else header |= node->sop << SOP_SHIFT;

				// Write header
				u8 *msg = send_buffer + send_buffer_bytes;
				*reinterpret_cast<u16*>( msg ) = getLE16(header);
				msg += 2;

				// Write optional ACK-ID
				if (ack_id_overhead)
				{
					// ACK-ID compression
					if (ack_id_bytes == 3)
					{
						msg[2] = (u8)(ack_id >> 12);
						msg[1] = (u8)((ack_id >> 5) | 0x80);
						msg[0] = (u8)((ack_id << 2) | 0x80 | stream);
						msg += 3;
					}
					else if (ack_id_bytes == 2)
					{
						msg[1] = (u8)((ack_id >> 5) & 0x7f);
						msg[0] = (u8)((ack_id << 2) | 0x80 | stream);
						msg += 2;
					}
					else
					{
						*msg++ = (u8)(((ack_id & 31) << 2) | stream);
					}

					ack_id_overhead = 0; // Don't write ACK-ID next time around

					// Set stream and ack_id for remainder of this packet
					_send_buffer_stream = stream;
					_send_buffer_ack_id = ack_id;

					// Increment ack id
					++ack_id;

					// Recalculate how many bytes it would take to represent
					u32 diff = ack_id - remote_expected;
					if (diff < 16)			ack_id_bytes = 1;
					else if (diff < 2048)	ack_id_bytes = 2;
					else					ack_id_bytes = 3;
				}

				// Write optional fragment word
				if (frag_overhead)
				{
					*reinterpret_cast<u16*>( msg ) = getLE((u16)node->bytes);
					frag_overhead = 0;
					msg += 2;
				}

				// Copy data bytes
				memcpy(msg, GetTrailingBytes(node) + sent_bytes, data_bytes_to_copy);

				sent_bytes += data_bytes_to_copy;
				send_buffer_bytes += write_bytes;

			} while (sent_bytes < total_bytes);

			if (sent_bytes > total_bytes)
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

	_send_buffer = send_buffer;
	_send_buffer_bytes = send_buffer_bytes;

	lock.Release();

	// Send packets:
	while (packet_send_head)
	{
		TempSendNode *next = packet_send_head->next;

		u32 bytes = packet_send_head->negative_offset;
		u8 *data = reinterpret_cast<u8*>( packet_send_head ) - bytes;

		WARN("Transport") << "Sending packet with " << bytes;

		if (!PostPacket(data, bytes + AuthenticatedEncryption::OVERHEAD_BYTES, bytes))
		{
			WARN("Transport") << "Unable to post send buffer";
			continue; // Retry
		}

		packet_send_head = next;
	}
}
