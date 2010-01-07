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
	// Receive state
	CAT_OBJCLR(_next_recv_expected_id);
	CAT_OBJCLR(_fragment_length);
	CAT_OBJCLR(_recv_queue_head);

	// Send state
	_writer_count = 0;

	CAT_OBJCLR(_next_send_id);

	_send_buffer = 0;
	_send_buffer_bytes = 0;

	CAT_OBJCLR(_send_queue_head);
	CAT_OBJCLR(_send_queue_tail);

	CAT_OBJCLR(_sent_list_head);
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

	while (bytes >= 2)
	{
		u16 header = getLE(*reinterpret_cast<u16*>( data ));

		data += 2;
		bytes -= 2;

		u16 data_bytes = header & DATALEN_MASK;
		u32 stream = (header & STM_MASK) >> STM_OFFSET;

		// If this message has an ACK-ID attached,
		if (header & I_MASK)
		{
			if (bytes < 3 + data_bytes)
			{
				WARN("Transport") << "Truncated message ignored(I=1)";
				break;
			}

			ack_id = ((u32)data[0] << 16) | ((u16)data[1] << 8) | data[2];
			ack_id = ReconstructCounter<24>(_next_recv_expected_id[stream], ack_id);

			data += 3;
			bytes -= 3;
		}
		else
		{
			if (bytes < data_bytes)
			{
				WARN("Transport") << "Truncated message ignored(I=0)";
				break;
			}
		}

		// If this message contains data,
		if (header & D_MASK)
		{
			// If unreliable message,
			if (stream == 0 && (header & F_MASK) != 0)
			{
				// Process it immediately
				if (data_bytes > 0)
				{
					OnMessage(data, data_bytes);
				}
				else
				{
					WARN("Transport") << "Zero-length unreliable message ignored";
				}
			}
			else // Reliable message:
			{
				u32 next_expected_id = _next_recv_expected_id[stream];

				s32 diff = (s32)(ack_id - next_expected_id);

				// If message is next expected,
				if (diff == 0)
				{
					// Process it immediately
					if (data_bytes > 0)
					{
						if (header & F_MASK) OnFragment(data, data_bytes);
						else OnMessage(data, data_bytes);
					}
					else
					{
						WARN("Transport") << "Zero-length reliable message ignored";
					}

					// Cache node and ack_id on stack
					RecvQueue *node = _recv_queue_head[stream];
					++ack_id;

					// For each queued message that is now ready to go,
					while (node && node->id == ack_id)
					{
						// Grab the queued message
						bool frag = (node->bytes & RecvQueue::FRAG_FLAG) != 0;
						u32 old_data_bytes = node->bytes & RecvQueue::BYTE_MASK;
						RecvQueue *next = node->next;
						u8 *old_data = GetTrailingBytes(node);

						// Process fragment now
						if (old_data_bytes > 0)
						{
							if (frag) OnFragment(old_data, old_data_bytes);
							else OnMessage(old_data, old_data_bytes);

							// NOTE: Unordered stream writes zero-length messages
							// to the receive queue since it processes immediately
							// and does not need to store the data.
						}

						// Delete queued message
						RegionAllocator::ii->Release(node);

						// And proceed on to next message
						++ack_id;
						node = next;
					}

					// Update receive queue state
					_recv_queue_head[stream] = node;
					_next_recv_expected_id[stream] = ack_id;
				}
				else if (diff > 0) // Message is due to arrive
				{
					RecvQueue *node = _recv_queue_head;
					RecvQueue *last = 0;

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
						else if (diff < 0)
						{
							// Insert before this node
							break;
						}

						// Keep searching for insertion point
						last = node;
						node = node->next;
					}

					u32 stored_bytes;

					if (stream == STREAM_UNORDERED)
					{
						// Process it immediately
						if (data_bytes > 0)
						{
							if (header & F_MASK) OnFragment(data, data_bytes);
							else OnMessage(data, data_bytes);
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

					RecvQueue *new_node = reinterpret_cast<RecvQueue*>( RegionAllocator::ii->Acquire(sizeof(RecvQueue) + stored_bytes) );
					if (!new_node)
					{
						WARN("Transport") << "Out of memory for incoming packet queue";
					}
					else
					{
						// Insert new data into queue
						new_node->bytes = (header & F_MASK) ? (stored_bytes | RecvQueue::FRAG_FLAG) : stored_bytes;
						new_node->id = ack_id;
						new_node->next = node;
						if (last) last->next = new_node;
						else _recv_queue_head = new_node;

						u8 *new_data = GetTrailingBytes(new_node);
						memcpy(new_data, data, bytes);
					}
				}
				else
				{
					WARN("Transport") << "Ignored duplicate rolled reliable message";
				}
			}
		}
		else
		{
			// TODO: Process ACK
		}

		bytes -= data_bytes;
		data += data_bytes;
	}

	EndWrite();
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

u8 *Transport::GetUnreliableBuffer(u32 data_bytes)
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

u8 *Transport::GetReliableBuffer(StreamMode stream, u32 data_bytes)
{
	// Fail on invalid input
	if (stream == STREAM_UNORDERED)
	{
		if (data_bytes + 2 + 3 > _max_payload_bytes) return 0;
	}
	else
	{
		if (data_bytes > MAX_MESSAGE_DATALEN) return 0;
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
	node->mode = 0;
	node->id = 0;
	node->next = 0;
	u8 *data = GetTrailingBytes(node);

	// Lock send lock while adding to the back of the send queue
	AutoMutex lock(_send_lock);

	// Add to back of send queue
	if (_send_queue_tail[stream]) _send_queue_tail[stream]->next = node;
	else _send_queue_head[stream] = node;
	_send_queue_tail[stream] = node;

	// Return pointer to data part
	return data;
}

void Transport::EndWrite()
{
	if (Atomic::Add(&_writer_count, -1) == 1)
	{
		AutoMutex lock(_send_lock);

		u32 send_buffer_bytes = _send_buffer_bytes;
		u8 *send_buffer = _send_buffer;
		u32 max_payload_bytes = _max_payload_bytes;

		for (int stream = 0; stream < NUM_STREAMS; ++stream)
		{
			SendQueue *node = _send_queue_head[stream];
			u32 ack_id = _next_send_id[stream];
			u32 overhead = 3; // for first ack_id

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
							WARN("Transport") << "Out of memory: Lost combined write(1)";
							send_buffer_bytes = 0;
						}
						else
						{
							u32 eat = max_payload_bytes - send_buffer_bytes;
							u32 copy_bytes = eat;

							u8 *msg = send_buffer + send_buffer_bytes;

							if (overhead)
							{
								*reinterpret_cast<u16*>( msg ) = getLE16(copy_bytes | (stream << STM_OFFSET) | I_MASK | F_MASK | D_MASK);
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
								*reinterpret_cast<u16*>( msg ) = getLE16(copy_bytes | (stream << STM_OFFSET) | F_MASK | D_MASK);
								*reinterpret_cast<u16*>( msg + 2 ) = getLE((u16)data_bytes);
								msg += 2 + 2;
								copy_bytes -= 2 + 2;
								send_buffer_bytes += 2 + 2 + copy_bytes;
							}

							// TODO: create fragment object, loop and continue sending fragments

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

							// TODO
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
						WARN("Transport") << "Out of memory: Lost combined write(2)";
						send_buffer_bytes = 0;
					}
					else
					{
						u8 *msg = send_buffer + send_buffer_bytes;

						if (overhead)
						{
							*reinterpret_cast<u16*>( msg ) = getLE16(data_bytes | (stream << STM_OFFSET) | I_MASK | D_MASK);
							msg[2] = (u8)(ack_id >> 16);
							msg[3] = (u8)(ack_id >> 8);
							msg[4] = (u8)ack_id;
							msg += 2 + 3;

							++ack_id;
							overhead = 0;
						}
						else
						{
							*reinterpret_cast<u16*>( msg ) = getLE16(data_bytes | (stream << STM_OFFSET) | D_MASK);
							msg += 2;
						}

						memcpy(msg, GetTrailingBytes(node), data_bytes);
						send_buffer_bytes += msg_bytes;
					}
				}
			}

			_send_queue_head[stream] = node;
			_next_send_id[stream] = ack_id;
		}

		// If data remains to be delivered,
		if (send_buffer_bytes)
		{
			PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES, send_buffer_bytes);

			_send_buffer = 0;
			_send_buffer_bytes = 0;
		}
	}
}

// lock send lock:
// sent list is organized from oldest to youngest packet
// so walk the sent list and determine if any are considered lost yet
// resend lost packets -- all following packets with the same ack id are delivered out of band
void Transport::TickTransport(ThreadPoolLocalStorage *tls, u32 now)
{
	// TODO
}
