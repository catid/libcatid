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

#include <cat/sphynx/Transport.hpp>
#include <cat/port/EndianNeutral.hpp>
#include <cat/threads/RegionAllocator.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;
using namespace sphynx;

// Convert handshake error string to user-readable error message
const char *cat::sphynx::GetHandshakeErrorString(HandshakeError err)
{
	switch (err)
	{
	case ERR_CLIENT_OUT_OF_MEMORY:	return "Out of memory";
	case ERR_CLIENT_BROKEN_PIPE:	return "Broken pipe";
	case ERR_CLIENT_TIMEOUT:		return "Connect timeout";
	case ERR_CLIENT_ICMP:			return "Server unreachable";

	case ERR_WRONG_KEY:			return "Wrong key";
	case ERR_SERVER_FULL:		return "Server full";
	case ERR_FLOOD_DETECTED:	return "Flood detected";
	case ERR_TAMPERING:			return "Tampering detected";
	case ERR_SERVER_ERROR:		return "Server error";

	default:					return "Unknown error";
	}
}

Transport::Transport()
{
	// Receive state
	CAT_OBJCLR(_got_reliable);

	CAT_OBJCLR(_fragments);

	CAT_OBJCLR(_recv_queue_head);
	CAT_OBJCLR(_recv_queue_tail);

	_recv_trip_time_sum = 0;
	_recv_trip_count = 0;
	_recv_trip_time_avg = 0;

	// Send state
	_send_buffer = 0;
	_send_buffer_bytes = 0;
	_send_buffer_stream = NUM_STREAMS;
	_send_flush_after_processing = false;

	CAT_OBJCLR(_send_queue_head);
	CAT_OBJCLR(_send_queue_tail);

	CAT_OBJCLR(_sent_list_head);
	CAT_OBJCLR(_sent_list_tail);

	// Just clear these for now.  When security is initialized these will be filled in
	CAT_OBJCLR(_next_send_id);
	CAT_OBJCLR(_send_next_remote_expected);
	CAT_OBJCLR(_next_recv_expected_id);

	_disconnected = false;
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
		if (_fragments[stream].length)
			delete []_fragments[stream].buffer;

		// Release memory for sent list
		SendQueue *sent_node = _sent_list_head[stream];
		while (sent_node)
		{
			SendQueue *next = sent_node->next;

			// If node is a fragment,
			if (sent_node->frag_count)
			{
				SendFrag *frag = reinterpret_cast<SendFrag*>( sent_node );

				// Deallocate master node for a series of SendFrags
				if (frag->full_data->frag_count == 1)
					RegionAllocator::ii->Release(frag->full_data);
				else
					frag->full_data->frag_count--;
			}

			RegionAllocator::ii->Release(sent_node);
			sent_node = next;
		}

		// Release memory for send queue
		SendQueue *send_node = _send_queue_head[stream];
		while (send_node)
		{
			SendQueue *next = send_node->next;
			RegionAllocator::ii->Release(send_node);
			send_node = next;
		}
	}
}

void Transport::InitializePayloadBytes(bool ip6)
{
	_overhead_bytes = (ip6 ? IPV6_HEADER_BYTES : IPV4_HEADER_BYTES) + UDP_HEADER_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD;
	_max_payload_bytes = MINIMUM_MTU - _overhead_bytes;
	_send_flow.SetMTU(_max_payload_bytes);
}

bool Transport::InitializeTransportSecurity(bool is_initiator, AuthenticatedEncryption &auth_enc)
{
	/*
		Most protocols just initialize the ACK IDs to zeroes at the start.
		The problem is that this gives attackers known plaintext bytes inside
		an encrypted channel.  I used this to break the WoW encryption without
		knowing the key, for example.  Sure, if a cryptosystem can be broken
		with known plaintext there is a problem ANYWAY but I mean why make it
		easier than it needs to be?  So we derive the initial ACK IDs using a
		key derivation function (KDF) based on the session key.
	*/

	// Randomize next send ack id
	if (!auth_enc.GenerateKey(is_initiator ? "ws2_32.dll" : "winsock.ocx", _next_send_id, sizeof(_next_send_id)))
		return false;
	memcpy(_send_next_remote_expected, _next_send_id, sizeof(_send_next_remote_expected));

	// Randomize next recv ack id
	return auth_enc.GenerateKey(!is_initiator ? "ws2_32.dll" : "winsock.ocx", _next_recv_expected_id, sizeof(_next_recv_expected_id));
}

void Transport::OnDatagram(ThreadPoolLocalStorage *tls,  u32 send_time, u32 recv_time, u8 *data, u32 bytes)
{
	if (_disconnected) return;

	u32 ack_id = 0, stream = 0;

	INANE("Transport") << "Datagram dump " << bytes << ":" << HexDumpString(data, bytes);

	while (bytes >= 1)
	{
		// Decode data_bytes
		u8 hdr = data[0];
		u32 data_bytes = hdr & BLO_MASK;

		INANE("Transport") << " -- Processing subheader " << (int)hdr;

		// If message length requires another byte to represent,
		if (hdr & C_MASK)
		{
			data_bytes |= (u16)data[1] << BHI_SHIFT;
			data += 2;
			bytes -= 2;
		}
		else
		{
			++data;
			--bytes;
		}

		// If this message has an ACK-ID attached,
		if (hdr & I_MASK)
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
					ack_id = ReconstructCounter<12>(_next_recv_expected_id[stream], ack_id);
			}
			else
				ack_id = ReconstructCounter<5>(_next_recv_expected_id[stream], ack_id);
		}
		else if (hdr & R_MASK)
		{
			// Could check for uninitialized ACK-ID here but I do not think that sending
			// this type of malformed packet can hurt the server.
			++ack_id;
		}

		if (bytes < data_bytes)
		{
			WARN("Transport") << "Truncated transport message ignored";
			break;
		}

		// If reliable message,
		if (hdr & R_MASK)
		{
			WARN("Transport") << "Got # " << stream << ":" << ack_id;

			s32 diff = (s32)(ack_id - _next_recv_expected_id[stream]);

			// If message is next expected,
			if (diff == 0)
			{
				// Process it immediately
				if (data_bytes > 0)
				{
					u32 super_opcode = (hdr >> SOP_SHIFT) & SOP_MASK;

					if (super_opcode == SOP_DATA)
						OnMessage(tls, send_time, recv_time, data, data_bytes);
					else if (super_opcode == SOP_FRAG)
						OnFragment(tls, send_time, recv_time, data, data_bytes, stream);
					else if (super_opcode == SOP_INTERNAL)
						OnInternal(tls, send_time, recv_time, data, data_bytes);
					else WARN("Transport") << "Invalid reliable super opcode ignored";

					if (_disconnected) return; // React to message handler
				}
				else WARN("Transport") << "Zero-length reliable message ignored";

				RunQueue(tls, recv_time, ack_id + 1, stream);

				if (_disconnected) return; // React to message handler
			}
			else if (diff > 0) // Message is due to arrive
			{
				QueueRecv(tls, send_time, recv_time, data, data_bytes, ack_id, stream, (hdr >> SOP_SHIFT) & SOP_MASK);

				if (_disconnected) return; // React to message handler (unordered)
			}
			else
			{
				WARN("Transport") << "Ignored duplicate rolled reliable message " << stream << ":" << ack_id;

				INANE("Transport") << "Rel dump " << bytes << ":" << HexDumpString(data, bytes);

				// Don't bother locking here: It's okay if we lose a race with this.
				_got_reliable[stream] = true;
			}
		}
		else if (data_bytes > 0) // Unreliable message:
		{
			u32 super_opcode = (hdr >> SOP_SHIFT) & SOP_MASK;

			if (super_opcode == SOP_DATA)
				OnMessage(tls, send_time, recv_time, data, data_bytes);
			else if (super_opcode == SOP_ACK)
				OnACK(send_time, recv_time, data, data_bytes);
			else if (super_opcode == SOP_INTERNAL)
				OnInternal(tls, send_time, recv_time, data, data_bytes);

			if (_disconnected) return; // React to message handler
		}

		bytes -= data_bytes;
		data += data_bytes;
	}

	// Calculate and accumulate transit time into statistics for flow control
	u32 transit_time = recv_time - send_time;

	// If transit time is in the future, clamp it into sanity
	if ((s32)transit_time < 1) transit_time = 1;

	// If transit time makes sense,
	if (transit_time < TIMEOUT_DISCONNECT)
	{
		// Accumulate latest transit time
		_recv_trip_time_sum += transit_time;

		// Calculate cumulative average
		u32 avg = _recv_trip_time_sum / ++_recv_trip_count;

		// Write it for timer thread
		u32 old = Atomic::Set(&_recv_trip_time_avg, avg);

		// If average was cleared,
		if (!old)
		{
			// Timer thread wants us to reset the accumulator
			_recv_trip_time_sum = 0;
			_recv_trip_count = 0;
		}
	}

	// If flush was requested,
	if (_send_flush_after_processing)
	{
		FlushImmediately();

		_send_flush_after_processing = false;
	}
}

void Transport::RunQueue(ThreadPoolLocalStorage *tls, u32 recv_time, u32 ack_id, u32 stream)
{
	RecvQueue *node = _recv_queue_head[stream];

	// If no queue to run or queue is not ready yet,
	if (!node || node->id != ack_id)
	{
		// TODO: Is locking necessary here?
		CAT_ACK_LOCK.Enter();

		// Just update next expected id and set flag to send acks on next tick
		_next_recv_expected_id[stream] = ack_id;
		_got_reliable[stream] = true;

		CAT_ACK_LOCK.Leave();

		return;
	}

	RecvQueue *kill_node = node;

	// For each queued message that is now ready to go,
	do
	{
		// Grab the queued message
		u32 old_data_bytes = node->bytes;
		u8 *old_data = GetTrailingBytes(node);

		// Process fragment now
		if (old_data_bytes > 0)
		{
			INFO("Transport") << "Running queued message # " << stream << ":" << ack_id;

			u32 super_opcode = node->sop;

			if (super_opcode == SOP_DATA)
				OnMessage(tls, node->send_time, recv_time, old_data, old_data_bytes);
			else if (super_opcode == SOP_FRAG)
				OnFragment(tls, node->send_time, recv_time, old_data, old_data_bytes, stream);
			else if (super_opcode == SOP_INTERNAL)
				OnInternal(tls, node->send_time, recv_time, old_data, old_data_bytes);

			if (_disconnected) return; // React to message handler

			// NOTE: Unordered stream writes zero-length messages
			// to the receive queue since it processes immediately
			// and does not need to store the data.
		}

		// And proceed on to next message
		++ack_id;
		node = node->next;
	} while (node && node->id == ack_id);

	CAT_ACK_LOCK.Enter();

	// Update receive queue state
	_recv_queue_head[stream] = node;
	if (!node) _recv_queue_tail[stream] = 0;
	else node->prev = 0;
	_next_recv_expected_id[stream] = ack_id;
	_got_reliable[stream] = true;

	CAT_ACK_LOCK.Leave();

	// Split deletion from processing to reduce lock contention
	while (kill_node != node)
	{
		RecvQueue *next = kill_node->next;

		// Delete queued message
		RegionAllocator::ii->Release(kill_node);

		kill_node = next;
	}
}

void Transport::QueueRecv(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, u8 *data, u32 data_bytes, u32 ack_id, u32 stream, u32 super_opcode)
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

	INANE("Transport") << "Out-of-order message " << data_bytes << ":" << HexDumpString(data, data_bytes);

	u32 stored_bytes;

	if (stream == STREAM_UNORDERED)
	{
		// Process it immediately
		if (data_bytes > 0)
		{
			if (super_opcode == SOP_DATA)
				OnMessage(tls, send_time, recv_time, data, data_bytes);
			else if (super_opcode == SOP_FRAG)
				OnFragment(tls, send_time, recv_time, data, data_bytes, stream);
			else if (super_opcode == SOP_INTERNAL)
				OnInternal(tls, send_time, recv_time, data, data_bytes);

			if (_disconnected) return; // React to message handler
		}
		else WARN("Transport") << "Zero-length reliable message ignored";

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
		return;
	}

	// Insert new data into queue
	new_node->bytes = stored_bytes;
	new_node->sop = super_opcode;
	new_node->id = ack_id;
	new_node->prev = node;
	new_node->next = next;
	new_node->send_time = send_time;

	// Just need to protect writes to the list linkages
	CAT_ACK_LOCK.Enter();

	if (next) next->prev = new_node;
	else _recv_queue_tail[stream] = new_node;
	if (node) node->next = new_node;
	else _recv_queue_head[stream] = new_node;
	_got_reliable[stream] = true;

	CAT_ACK_LOCK.Leave();

	u8 *new_data = GetTrailingBytes(new_node);
	memcpy(new_data, data, data_bytes);
}

void Transport::OnFragment(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, u8 *data, u32 bytes, u32 stream)
{
	INANE("Transport") << "OnFragment " << bytes << ":" << HexDumpString(data, bytes);

	// If fragment is starting,
	if (!_fragments[stream].length)
	{
		if (bytes < 2)
		{
			WARN("Transport") << "Truncated message fragment head ignored";
			return;
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
			_fragments[stream].buffer = new u8[frag_length];
			if (!_fragments[stream].buffer)
			{
				WARN("Transport") << "Out of memory: Unable to allocate fragment buffer";
				return;
			}
			else
			{
				_fragments[stream].length = frag_length;
				_fragments[stream].offset = 0;
				_fragments[stream].send_time = send_time;
			}
		}

		// Fall-thru to processing data part of fragment message:
	}

	u32 fragment_remaining = _fragments[stream].length - _fragments[stream].offset;

	// If the fragment is now complete,
	if (bytes >= fragment_remaining)
	{
		if (bytes > fragment_remaining)
		{
			WARN("Transport") << "Message fragment overflow truncated";
		}

		memcpy(_fragments[stream].buffer + _fragments[stream].offset, data, fragment_remaining);

		OnMessage(tls, send_time, recv_time, _fragments[stream].buffer, _fragments[stream].length);

		delete []_fragments[stream].buffer;
		_fragments[stream].length = 0;
	}
	else
	{
		memcpy(_fragments[stream].buffer + _fragments[stream].offset, data, bytes);
		_fragments[stream].offset += bytes;
	}
}

bool Transport::WriteUnreliableOOB(u8 msg_opcode, const void *vmsg_data, u32 data_bytes, SuperOpcode super_opcode)
{
	const u8 *msg_data = reinterpret_cast<const u8*>( vmsg_data );

	++data_bytes;
	u32 max_payload_bytes = _max_payload_bytes;
	u32 header_bytes = data_bytes > BLO_MASK ? 2 : 1;
	u32 msg_bytes = header_bytes + data_bytes;

	// Fail on invalid input
	if (msg_bytes > max_payload_bytes)
	{
		WARN("Transport") << "Invalid input: Unreliable OOB buffer size request too large";
		return false;
	}

	u8 *pkt = AsyncBuffer::Acquire(msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
	if (!pkt)
	{
		WARN("Transport") << "Out of memory: Unable to allocate unreliable OOB post buffer";
		return false;
	}

	u8 *msg_buffer = pkt;

	// Write header
	if (data_bytes > BLO_MASK)
	{
		msg_buffer[0] = (u8)(data_bytes & BLO_MASK) | (super_opcode << SOP_SHIFT) | C_MASK;
		msg_buffer[1] = (u8)(data_bytes >> BHI_SHIFT);
	}
	else
	{
		msg_buffer[0] = (u8)data_bytes | (super_opcode << SOP_SHIFT);
	}
	msg_buffer += header_bytes;

	// Write message
	msg_buffer[0] = msg_opcode;
	memcpy(msg_buffer + 1, msg_data, data_bytes - 1);

	if (PostPacket(pkt, msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD, msg_bytes))
	{
		_send_flow.OnPacketSend(msg_bytes + _overhead_bytes);
		return true;
	}

	return false;
}

bool Transport::WriteUnreliable(u8 msg_opcode, const void *vmsg_data, u32 data_bytes, SuperOpcode super_opcode)
{
	const u8 *msg_data = reinterpret_cast<const u8*>( vmsg_data );

	++data_bytes;
	u32 max_payload_bytes = _max_payload_bytes;
	u32 header_bytes = data_bytes > BLO_MASK ? 2 : 1;
	u32 msg_bytes = header_bytes + data_bytes;

	// Fail on invalid input
	if (msg_bytes > max_payload_bytes)
	{
		WARN("Transport") << "Invalid input: Unreliable buffer size request too large";
		return false;
	}

	AutoMutex lock(_big_lock);

	u32 send_buffer_bytes = _send_buffer_bytes;

	// If the growing send buffer cannot contain the new message,
	if (send_buffer_bytes + msg_bytes > max_payload_bytes)
	{
		u8 *old_send_buffer = _send_buffer;

		u8 *msg_buffer = AsyncBuffer::Acquire(msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!msg_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to allocate unreliable post buffer";
			return false;
		}

		// Update send buffer state
		_send_buffer = msg_buffer;
		_send_buffer_bytes = msg_bytes;
		_send_buffer_stream = NUM_STREAMS;

		// Write header
		if (data_bytes > BLO_MASK)
		{
			msg_buffer[0] = (u8)(data_bytes & BLO_MASK) | (super_opcode << SOP_SHIFT) | C_MASK;
			msg_buffer[1] = (u8)(data_bytes >> BHI_SHIFT);
		}
		else
		{
			msg_buffer[0] = (u8)data_bytes | (super_opcode << SOP_SHIFT);
		}
		msg_buffer += header_bytes;

		// Write message
		msg_buffer[0] = msg_opcode;
		memcpy(msg_buffer + 1, msg_data, data_bytes - 1);

		lock.Release();

		if (PostPacket(old_send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD, send_buffer_bytes))
			_send_flow.OnPacketSend(send_buffer_bytes + _overhead_bytes);
		else WARN("Transport") << "Packet post failure during unreliable overflow";
	}
	else
	{
		// Create or grow buffer and write into it
		_send_buffer = AsyncBuffer::Resize(_send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!_send_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to resize unreliable post buffer";
			_send_buffer_bytes = 0;
			_send_buffer_stream = NUM_STREAMS;
			return false;
		}

		u8 *msg_buffer = _send_buffer + send_buffer_bytes;

		// Write header
		if (data_bytes > BLO_MASK)
		{
			msg_buffer[0] = (u8)(data_bytes & BLO_MASK) | (super_opcode << SOP_SHIFT) | C_MASK;
			msg_buffer[1] = (u8)(data_bytes >> BHI_SHIFT);
		}
		else
		{
			msg_buffer[0] = (u8)data_bytes | (super_opcode << SOP_SHIFT);
		}
		msg_buffer += header_bytes;

		// Write message
		msg_buffer[0] = msg_opcode;
		memcpy(msg_buffer + 1, msg_data, data_bytes - 1);

		// Update send buffer state
		_send_buffer_bytes = send_buffer_bytes + msg_bytes;
	}

	return true;
}

void Transport::FlushImmediately()
{
	// Avoid locking to transmit queued if no queued exist
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_send_queue_head[stream])
		{
			TransmitQueued();
			break;
		}
	}

	// Post whatever is left in the send buffer
	PostSendBuffer();
}

void Transport::PostSendBuffer()
{
	// Avoid locking if we can help it
	if (!_send_buffer) return;

	AutoMutex lock(_big_lock);

	u8 *send_buffer = _send_buffer;

	if (send_buffer)
	{
		u32 send_buffer_bytes = _send_buffer_bytes;

		// Reset send buffer state
		_send_buffer = 0;
		_send_buffer_bytes = 0;
		_send_buffer_stream = NUM_STREAMS;

		// Release lock for actual posting
		lock.Release();

		if (PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD, send_buffer_bytes))
			_send_flow.OnPacketSend(send_buffer_bytes + _overhead_bytes);
		else WARN("Transport") << "Packet post failure during flush write";
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
	u8 hdr = R_MASK;
	u32 frag_overhead = 0;
	u32 frag_total_bytes;

	// If node is a fragment,
	if (node->frag_count)
	{
		SendFrag *frag = reinterpret_cast<SendFrag*>( node );

		data = GetTrailingBytes(frag->full_data) + frag->offset;
		hdr |= SOP_FRAG << SOP_SHIFT;

		// If this is the first fragment of the message,
		if (frag->offset == 0)
		{
			// Prepare to insert overhead
			frag_overhead = 2;
			frag_total_bytes = frag->full_data->bytes;
		}
	}
	else
	{
		data = GetTrailingBytes(node);
		hdr |= node->sop << SOP_SHIFT;
	}

	// Include fragment header length in data bytes field
	u32 data_bytes_with_overhead = data_bytes + frag_overhead;
	u32 header_bytes = data_bytes_with_overhead > BLO_MASK ? 2 : 1;
	u32 msg_bytes = header_bytes + data_bytes_with_overhead;
	u32 ack_id = node->id;

	// If ACK-ID needs to be written again,
	if (_send_buffer_stream != stream ||
		_send_buffer_ack_id != ack_id)
	{
		hdr |= I_MASK;
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
		// TODO: Eventually I should move this PostPacket() outside of the caller's send lock

		if (PostPacket(send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD, send_buffer_bytes))
			_send_flow.OnPacketSend(send_buffer_bytes + _overhead_bytes);
		else WARN("Transport") << "Packet post failure during retransmit overflow";

		send_buffer = 0;
		send_buffer_bytes = 0;

		// Set ACK-ID bit if it would now need to be sent
		if (!(hdr & I_MASK))
		{
			hdr |= I_MASK;
			msg_bytes += 3;
		}
	}

	// Create or grow buffer and write into it
	_send_buffer = AsyncBuffer::Resize(send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
	if (!_send_buffer)
	{
		WARN("Transport") << "Out of memory: Unable to resize post buffer";
		_send_buffer_bytes = 0;
		_send_buffer_stream = NUM_STREAMS;
		return;
	}

	u8 *msg = _send_buffer + send_buffer_bytes;

	if (data_bytes_with_overhead > BLO_MASK)
	{
		msg[0] = (u8)(data_bytes_with_overhead & BLO_MASK) | C_MASK | hdr;
		msg[1] = (u8)(data_bytes_with_overhead >> BHI_SHIFT);
		msg += 2;
	}
	else
	{
		msg[0] = (u8)data_bytes_with_overhead | hdr;
		++msg;
	}

	// If ACK-ID needs to be written, do not use compression since we
	// cannot predict receiver state on retransmission
	if (hdr & I_MASK)
	{
		msg[0] = (u8)(stream | ((ack_id & 31) << 2) | 0x80);
		msg[1] = (u8)((ack_id >> 5) | 0x80);
		msg[2] = (u8)(ack_id >> 12);
		msg += 3;

		// Next reliable message by default is one ACK-ID ahead
		_send_buffer_ack_id = ack_id + 1;
	}

	// If fragment header needs to be written,
	if (frag_overhead)
	{
		*reinterpret_cast<u16*>( msg ) = getLE16((u16)frag_total_bytes);
		msg += 2;
	}

	memcpy(msg, data, data_bytes);

	_send_buffer_bytes = send_buffer_bytes + msg_bytes;

	node->ts_lastsend = now;

	WARN("Transport") << "Retransmitted # " << stream << ":" << ack_id;
}

void Transport::WriteACK()
{
	u8 packet[MAXIMUM_MTU];
	u8 *offset = packet + 2 + 2; // 2 for header field
	u32 max_payload_bytes = _max_payload_bytes;
	u32 remaining = max_payload_bytes - 2 - 2;

	// Set the average trip time to zero to indicate statistics reset
	u32 trip_time_avg = Atomic::Set(&_recv_trip_time_avg, 0);

	// Write average trip time
	if (trip_time_avg > 0x7f)
	{
		packet[2] = (u8)(trip_time_avg | C_MASK);
		packet[3] = (u8)(trip_time_avg >> 7);
	}
	else
	{
		packet[2] = (u8)trip_time_avg;
		--offset;
		++remaining;
	}

	CAT_ACK_LOCK.Enter();

	// Prioritizes ACKs for unordered stream, then 1, 2 and 3 in that order.
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_got_reliable[stream])
		{
			// Truncates ACK message if needed.
			// This is mitigated by not resetting _got_reliable, so
			// next tick perhaps the rest of the ACK list can be sent.
			if (remaining < 3)
			{
				WARN("Transport") << "ACK packet truncated due to lack of space(1)";
				break;
			}

			u32 rollup_ack_id = _next_recv_expected_id[stream];

			// Write ROLLUP
			offset[0] = (u8)(1 | (stream << 1) | ((rollup_ack_id & 31) << 3));
			offset[1] = (u8)(rollup_ack_id >> 5);
			offset[2] = (u8)(rollup_ack_id >> 13);
			offset += 3;
			remaining -= 3;

			INFO("Transport") << "Acknowledging rollup # " << stream << ":" << rollup_ack_id;

			RecvQueue *node = _recv_queue_head[stream];

			if (node)
			{
				u32 start_id = node->id;
				u32 end_id = start_id;
				u32 last_id = rollup_ack_id;

				node = node->next;

				for (;;)
				{
					// If range continues,
					if (node && node->id == end_id + 1)
					{
						++end_id;
					}
					else // New range or end of ranges
					{
						// Encode RANGE: START(3) || END(3)
						if (remaining < 6)
						{
							WARN("Transport") << "ACK packet truncated due to lack of space(2)";
							break;
						}

						// ACK messages transmits ids relative to the previous one in the datagram
						u32 start_offset = start_id - last_id;
						u32 end_offset = end_id - start_id;
						last_id = end_id;

						INFO("Transport") << "Acknowledging range # " << stream << ":" << start_id << " - " << end_id;

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

						// Exit condition: node is null
						if (!node) break;

						// Begin new range
						start_id = node->id;
						end_id = start_id;

					} // End of range

					node = node->next;
				} // Looping until node is null (see exit condition above)
			} // If node is not null

			// If we exhausted all in the list, unset flag
			if (!node) _got_reliable[stream] = false;
		}
	}

	CAT_ACK_LOCK.Leave();

	u32 msg_bytes = max_payload_bytes - remaining;
	u8 *packet_copy_source = packet;

	// Write header
	u32 data_bytes = msg_bytes - 2;
	if (data_bytes > BLO_MASK)
	{
		packet[0] = (u8)((data_bytes & BLO_MASK) | (SOP_ACK << SOP_SHIFT) | C_MASK);
		packet[1] = (u8)(data_bytes >> BHI_SHIFT);
	}
	else
	{
		// Eat first byte and skip sending BHI if possible
		packet[1] = (u8)((data_bytes & BLO_MASK) | (SOP_ACK << SOP_SHIFT));
		++packet_copy_source;
		--msg_bytes;
	}

	// Post message:

	AutoMutex lock(_big_lock);

	u32 send_buffer_bytes = _send_buffer_bytes;

	// If the growing send buffer cannot contain the new message,
	if (send_buffer_bytes + msg_bytes > max_payload_bytes)
	{
		u8 *old_send_buffer = _send_buffer;

		u8 *msg_buffer = AsyncBuffer::Acquire(msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!msg_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to allocate ACK post buffer";
		}
		else
		{
			memcpy(msg_buffer, packet_copy_source, msg_bytes);
			_send_buffer = msg_buffer;
			_send_buffer_bytes = msg_bytes;
			_send_buffer_stream = NUM_STREAMS;

			lock.Release();

			if (PostPacket(old_send_buffer, send_buffer_bytes + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD, send_buffer_bytes))
				_send_flow.OnPacketSend(send_buffer_bytes + _overhead_bytes);
			else WARN("Transport") << "Packet post failure during ACK send buffer overflow";
		}
	}
	else
	{
		// Create or grow buffer and write into it
		_send_buffer = AsyncBuffer::Resize(_send_buffer, send_buffer_bytes + msg_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
		if (!_send_buffer)
		{
			WARN("Transport") << "Out of memory: Unable to resize ACK post buffer";
			_send_buffer_bytes = 0;
			_send_buffer_stream = NUM_STREAMS;
		}
		else
		{
			memcpy(_send_buffer + send_buffer_bytes, packet_copy_source, msg_bytes);
			_send_buffer_bytes = send_buffer_bytes + msg_bytes;
		}
	}
}

void Transport::TickTransport(ThreadPoolLocalStorage *tls, u32 now)
{
	if (_disconnected) return;

	// Acknowledge recent reliable messages
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_got_reliable[stream])
		{
			WriteACK();
			break;
		}
	}

	u32 loss_count = 0;

	// Retransmit lost messages
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_sent_list_head[stream])
		{
			loss_count = RetransmitLost(now);
			break;
		}
	}

	_big_lock.Enter();
	_send_flow.OnTick(now, loss_count);
	_big_lock.Leave();

	// Avoid locking to transmit queued if no queued exist
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		if (_send_queue_head[stream])
		{
			TransmitQueued();
			break;
		}
	}

	// Post whatever is left in the send buffer
	PostSendBuffer();
}

u32 Transport::RetransmitLost(u32 now)
{
	u32 timeout = _send_flow.GetLossTimeout();
	u32 loss_count = 0, last_mia_time = 0;

	AutoMutex lock(_big_lock);

	// Retransmit lost packets
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		SendQueue *node = _sent_list_head[stream];

		// For each node that might be ready for a retransmission,
		while (node)
		{
			u32 mia_time = now - node->ts_lastsend;

			if (mia_time >= timeout + (node->ts_lastsend - node->ts_firstsend))
			{
				Retransmit(stream, node, now);

				// Only record one loss per millisecond
				if (mia_time != last_mia_time) ++loss_count;
				last_mia_time = mia_time;
			}
			else if (now - node->ts_firstsend < timeout)
			{
				// Nodes are added to the end of the sent list, so as soon as it
				// finds one that cannot possibly be retransmitted it is done
				break;
			}

			node = node->next;
		}
	}

	return loss_count;
}

bool Transport::PostMTUProbe(ThreadPoolLocalStorage *tls, u32 mtu)
{
	INANE("Transport") << "Posting MTU Probe";

	if (mtu < MINIMUM_MTU || mtu > MAXIMUM_MTU)
		return false;

	u32 payload_bytes = mtu - _overhead_bytes;
	u32 buffer_bytes = payload_bytes + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD;
	u32 data_bytes = payload_bytes - TRANSPORT_HEADER_BYTES;

	u8 *pkt = AsyncBuffer::Acquire(buffer_bytes);
	if (!pkt) return false;

	// Write header
	//	I = 0 (no ack id follows)
	//	R = 0 (unreliable)
	//	C = 1 (large packet size)
	//	SOP = IOP_C2S_MTU_PROBE
	pkt[0] = (u8)((IOP_C2S_MTU_PROBE << SOP_SHIFT) | C_MASK | (data_bytes & BLO_MASK));
	pkt[1] = (u8)(data_bytes >> BHI_SHIFT);

	// Write message type
	pkt[2] = IOP_C2S_MTU_PROBE;

	// Fill with random data
	tls->csprng->Generate(pkt + 3, data_bytes - 1);

	// Encrypt and send buffer
	if (PostPacket(pkt, buffer_bytes, payload_bytes))
	{
		_send_flow.OnPacketSend(mtu);
		return true;
	}

	return false;
}

void Transport::OnACK(u32 send_time, u32 recv_time, u8 *data, u32 data_bytes)
{
	if (data_bytes < 2) return;

	u32 avg_trip_time = data[0] & 0x7f;

	// If trip time is two bytes,
	if (data[0] & C_MASK)
	{
		// Bring in the high byte
		avg_trip_time |= (u32)data[1] << 7;

		data += 2;
		data_bytes -= 2;
	}
	else
	{
		++data;
		--data_bytes;
	}

	u32 stream = NUM_STREAMS, last_ack_id = 0;
	SendQueue *node = 0, *kill_list = 0;
	u32 now = Clock::msec();
	u32 loss_count = 0, last_mia_time = 0;
	u32 timeout = _send_flow.GetLossTimeout();

	INFO("Transport") << "Got ACK with " << data_bytes << " bytes of data to decode ----";

	AutoMutex lock(_big_lock);

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

					SendQueue *rnode = _sent_list_head[stream];

					if (rnode)
					{
						while ((s32)(last_ack_id - rnode->id) > 0)
						{
							u32 mia_time = now - rnode->ts_lastsend;

							if (mia_time >= timeout + (rnode->ts_lastsend - rnode->ts_firstsend))
							{
								Retransmit(stream, rnode, now);

								// Only record one loss per millisecond
								if (mia_time != last_mia_time) ++loss_count;
								last_mia_time = mia_time;
							}

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

					INFO("Transport") << "Got acknowledgment for rollup # " << stream << ":" << ack_id;

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

			INFO("Transport") << "Got acknowledgment for range # " << stream << ":" << start_ack_id << " - " << end_ack_id;

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

				// If next node is within the range,
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
					if (node) node->prev = prev;
					else _sent_list_tail[stream] = prev;
				}

				// Next range start is offset from the end of this range
				last_ack_id = end_ack_id;

			} // nodes remain to check
		} // field is range
	} // while data bytes > 0

	// Retransmit lost packets
	if (stream < NUM_STREAMS)
	{
		SendQueue *rnode = _sent_list_head[stream];

		if (rnode)
		{
			// While node ACK-IDs are under the final END range,
			while ((s32)(last_ack_id - rnode->id) > 0)
			{
				u32 mia_time = now - rnode->ts_lastsend;

				if (mia_time >= timeout + (rnode->ts_lastsend - rnode->ts_firstsend))
				{
					Retransmit(stream, rnode, now);

					// Only record one loss per millisecond
					if (mia_time != last_mia_time) ++loss_count;
					last_mia_time = mia_time;
				}

				rnode = rnode->next;
				if (!rnode) break;
			}
		}
	}

	// Inform the flow control algorithm
	_send_flow.OnACK(now, avg_trip_time, loss_count);

	lock.Release();

	// Release kill list after lock is released
	while (kill_list)
	{
		SendQueue *next = kill_list->next;
		RegionAllocator::ii->Release(kill_list);
		kill_list = next;
	}
}

bool Transport::WriteReliable(StreamMode stream, u8 msg_opcode, const void *vmsg_data, u32 data_bytes, SuperOpcode super_opcode)
{
	const u8 *msg_data = reinterpret_cast<const u8*>( vmsg_data );

	// Fail on invalid input
	if (stream == STREAM_UNORDERED)
	{
		u32 header_bytes = data_bytes > BLO_MASK ? 2 : 1;

		if (header_bytes + 3 + 1 + data_bytes > _max_payload_bytes)
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
	SendQueue *node = RegionAllocator::ii->AcquireBuffer<SendQueue>(1 + data_bytes);
	if (!node)
	{
		WARN("Transport") << "Out of memory: Unable to allocate sendqueue object";
		return false;
	}

	// Fill the object
	node->bytes = 1 + data_bytes;
	node->frag_count = 0;
	node->sop = super_opcode;
	node->sent_bytes = 0;
	node->next = 0;

	u8 *node_data = GetTrailingBytes(node);
	node_data[0] = msg_opcode;
	memcpy(node_data+1, msg_data, data_bytes);

	_big_lock.Enter();

	// Add to back of send queue
	SendQueue *tail = _send_queue_tail[stream];
	node->prev = tail;
	if (tail) tail->next = node;
	else _send_queue_head[stream] = node;
	_send_queue_tail[stream] = node;

	_big_lock.Leave();

	return true;
}

void Transport::TransmitQueued()
{
	// ACK-ID compression thresholds
	const u32 ACK_ID_1_THRESH = 8;
	const u32 ACK_ID_2_THRESH = 1024;

	// If there is no more room in the channel,
	s32 send_epoch_bytes = _send_flow.GetSentBytes();
	if (send_epoch_bytes >= _send_flow.GetMaxSentBytes())
	{
		// Stop sending here
		return;
	}

	// Use the same ts_firstsend for all messages delivered now, to insure they are clustered on retransmission
	u32 now = Clock::msec();
	u32 max_payload_bytes = _max_payload_bytes;

	// List of packets to send after done with send lock
	TempSendNode *packet_send_head = 0, *packet_send_tail = 0;

	AutoMutex lock(_big_lock);

	// Cache send buffer
	u32 send_buffer_bytes = _send_buffer_bytes;
	u8 *send_buffer = _send_buffer;

	// For each round,
	int stream;
	do
	{
		// For each reliable stream,
		for (stream = 0; stream < NUM_STREAMS; ++stream)
		{
			SendQueue *node = _send_queue_head[stream];
			if (!node) continue;

			u32 stream_sent = 0; // Used to limit the number of bytes sent by a stream per round

			u32 ack_id = _next_send_id[stream];
			u32 remote_expected = _send_next_remote_expected[stream];
			u32 ack_id_overhead = 0, ack_id_bytes;

			u32 diff = ack_id - remote_expected;
			if (diff < ACK_ID_1_THRESH)			ack_id_bytes = 1;
			else if (diff < ACK_ID_2_THRESH)	ack_id_bytes = 2;
			else								ack_id_bytes = 3;

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
				bool fragmented = (node->frag_count != 0);
				u32 sent_bytes = node->sent_bytes, total_bytes = node->bytes;

				do {
					u32 remaining_data_bytes = total_bytes - sent_bytes;
					u32 remaining_send_buffer = max_payload_bytes - send_buffer_bytes;
					u32 frag_overhead = 0;

					// If message would be fragmented,
					if (2 + ack_id_overhead + remaining_data_bytes > remaining_send_buffer)
					{
						/*
							When to Fragment?

							The goal of the transport protocol is to deliver data as quickly and reliably as possible.
							Fragmentation has additional processing overhead that should be avoided at the expense of
							about FRAG_THRESHOLD bytes of lost bandwidth per packet.  If more bytes than that can fit
							in a packet then it is time to fragment.
						*/
						// If it is worth fragmentation,
						if (remaining_send_buffer >= FRAG_THRESHOLD)
						{
							if (!fragmented)
							{
								frag_overhead = 2;
								fragmented = true;
							}
						}
						else if (send_buffer_bytes > 0) // Not worth fragmentation, dump current send buffer
						{
							// Prepare a temp send node for the old packet send buffer
							TempSendNode *old_node = reinterpret_cast<TempSendNode*>( send_buffer + send_buffer_bytes );
							old_node->next = 0;
							old_node->negative_offset = send_buffer_bytes;

							// Insert old packet send buffer at the end of the temp send list
							if (packet_send_tail) packet_send_tail->next = old_node;
							else packet_send_head = old_node;
							packet_send_tail = old_node;

							// Update send epoch bytes
							send_epoch_bytes += send_buffer_bytes + _overhead_bytes;

							// If there is no more room in the channel,
							if (send_epoch_bytes >= _send_flow.GetMaxSentBytes())
							{
								// Does not need to update _send_buffer_ack_id and _send_buffer_stream
								// since those members are updated whenever the send buffer is appended

								// Update node sent bytes
								node->sent_bytes = sent_bytes;

								// Next time we send a message in this stream, use the same ack id
								_next_send_id[stream] = ack_id;

								// Unlink previous node from node and leave the rest on the send queue
								_send_queue_head[stream] = node;
								node->prev = 0;

								// Clear send buffer state since the send buffer is now being emptied out
								_send_buffer = 0;
								_send_buffer_bytes = 0;
								_send_buffer_stream = NUM_STREAMS;

								lock.Release();

								// Post any outstanding packets
								PostPacketList(packet_send_head);

								return;
							}

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
					u32 write_bytes = min(msg_bytes, remaining_send_buffer);

					// Limit size to allow ACK-ID decompression during retransmission
					u32 retransmit_limit = max_payload_bytes - (3 - ack_id_overhead);
					if (write_bytes > retransmit_limit) write_bytes = retransmit_limit;

					u32 data_bytes_to_copy = write_bytes - overhead;

					// Link to the end of the sent list (Expectation is that acks will be received for nodes close to the head first)
					SendQueue *tail = _sent_list_tail[stream];
					if (fragmented)
					{
						SendFrag *frag = reinterpret_cast<SendFrag*>( 
							RegionAllocator::ii->Acquire(sizeof(SendFrag)) );

						if (!frag)
						{
							WARN("Transport") << "Out of memory: Unable to allocate fragment node";
							continue; // Retry
						}
						else
						{
							// Fill fragment object
							frag->id = ack_id;
							frag->next = 0;
							frag->prev = tail;
							frag->bytes = data_bytes_to_copy;
							frag->offset = sent_bytes;
							frag->ts_firstsend = now;
							frag->ts_lastsend = now;
							frag->full_data = node;
							frag->frag_count = 1;

							// Link fragment at the end of the sent list
							if (tail) tail->next = reinterpret_cast<SendQueue*>( frag );
							else _sent_list_head[stream] = reinterpret_cast<SendQueue*>( frag );
							_sent_list_tail[stream] = reinterpret_cast<SendQueue*>( frag );

							/*
								How do we know when to deallocate the master node for fragments?

								node->frag_count is used to know when to deallocate a master node.
								When all of the fragments are acknowledged, the master node can be
								deallocated.  Each fragment decreases frag_count by 1 until it
								reaches zero, signaling that all fragments have been acknowledged.

								Since we're sending fragments while they're being received, and the
								rate may be very low, node->frag_count could conceivably be reduced
								to zero before all fragments are transmitted.  This would cause the
								trigger condition for deleting the master node prematurely.

								To avoid the above problem, I begin by setting node->frag_count to
								to 2 instead of 1 for the first fragment.  For the final fragment,
								node->frag_count is not incremented, allowing the master node to
								be deallocated at the correct time.
							*/

							// For first fragment,
							if (sent_bytes == 0)
							{
								// Set master node frag count to 2
								node->frag_count = 2;

								// Mark node as a master node so that it can be ignored for
								// RTT determination OnACK()
								node->ts_firstsend = 1;
								node->ts_lastsend = 0;
							}
							// And for all other fragments until the final one,
							else if (sent_bytes + data_bytes_to_copy < total_bytes)
							{
								// Increment the frag count
								node->frag_count++;
							}
						}
					}
					else
					{
						// Fill message node
						node->id = ack_id;
						node->next = 0;
						node->prev = tail;
						node->ts_firstsend = now;
						node->ts_lastsend = now;
						//node->frag_count = 0;  Set during reliable write

						// Link message at the end of the sent list
						if (tail) tail->next = node;
						else _sent_list_head[stream] = node;
						_sent_list_tail[stream] = node;
					}

					// Resize post buffer to contain the bytes that will be written
					send_buffer = AsyncBuffer::Resize(send_buffer, send_buffer_bytes + write_bytes + AuthenticatedEncryption::OVERHEAD_BYTES);
					if (!send_buffer)
					{
						WARN("Transport") << "Out of memory: Unable to allocate send buffer";
						send_buffer_bytes = 0;
						continue; // Retry
					}

					// Write header
					u8 *msg = send_buffer + send_buffer_bytes;
					u32 data_bytes = data_bytes_to_copy + frag_overhead;

					u8 hdr = R_MASK;
					hdr |= (fragmented ? SOP_FRAG : node->sop) << SOP_SHIFT;
					if (ack_id_overhead) hdr |= I_MASK;

					if (data_bytes > BLO_MASK)
					{
						msg[0] = (u8)(data_bytes & BLO_MASK) | C_MASK | hdr;
						msg[1] = (u8)(data_bytes >> BHI_SHIFT);
						msg += 2;
						send_buffer_bytes += write_bytes;
					}
					else
					{
						msg[0] = (u8)data_bytes | hdr;
						++msg;
						send_buffer_bytes += write_bytes - 1; // Turns out we can cut out a byte

						// This could have been taken into account above but I don't think it's worth the processing time.
						// The purpose of cutting out the second byte is to help in the case of many small messages,
						// and this case is handled well as it is written now.
					}

					// Write optional ACK-ID
					if (ack_id_overhead)
					{
						// ACK-ID compression
						if (ack_id_bytes == 1)
						{
							*msg++ = (u8)(((ack_id & 31) << 2) | stream);
						}
						else if (ack_id_bytes == 2)
						{
							msg[1] = (u8)((ack_id >> 5) & 0x7f);
							msg[0] = (u8)((ack_id << 2) | 0x80 | stream);
							msg += 2;
						}
						else // if (ack_id_bytes == 3)
						{
							msg[2] = (u8)(ack_id >> 12);
							msg[1] = (u8)((ack_id >> 5) | 0x80);
							msg[0] = (u8)((ack_id << 2) | 0x80 | stream);
							msg += 3;
						}

						ack_id_overhead = 0; // Don't write ACK-ID next time around
					}

					// Increment ACK-ID
					++ack_id;

					// Recalculate how many bytes it would take to represent
					u32 diff = ack_id - remote_expected;
					if (diff < ACK_ID_1_THRESH)			ack_id_bytes = 1;
					else if (diff < ACK_ID_2_THRESH)	ack_id_bytes = 2;
					else								ack_id_bytes = 3;

					// Write optional fragment word
					if (frag_overhead)
					{
						*reinterpret_cast<u16*>( msg ) = getLE((u16)node->bytes);
						frag_overhead = 0;
						msg += 2;
					}

					// Copy data bytes
					memcpy(msg, GetTrailingBytes(node) + sent_bytes, data_bytes_to_copy);

					stream_sent += data_bytes_to_copy;
					sent_bytes += data_bytes_to_copy;

					// Update send buffer ack id and stream to minimize overhead
					_send_buffer_ack_id = ack_id;
					_send_buffer_stream = stream;

					WARN("Transport") << "Wrote " << stream << ":" << sent_bytes << " / " << total_bytes;

					// If it is time to stripe the next stream,
					if (stream_sent >= max_payload_bytes)
					{
						// Update node sent bytes
						node->sent_bytes = sent_bytes;

						// If node hasn't completed sending yet,
						if (sent_bytes < total_bytes)
						{
							// Keep it as the head
							_send_queue_head[stream] = node;
							node->prev = 0;
						}
						else
						{
							// Unlink previous node from node and leave the rest on the send queue
							_send_queue_head[stream] = next;
							if (!next) _send_queue_tail[stream] = 0;
							else next->prev = 0;
						}

						// Break out of walking send queue
						goto BreakStreamEarly;
					}
				} while (sent_bytes < total_bytes);

				if (sent_bytes > total_bytes)
				{
					WARN("Transport") << "Node offset somehow escaped";
				}

				node = next;
			} // walking send queue

			// Update send queue state for this stream
			_send_queue_head[stream] = 0;
			_send_queue_tail[stream] = 0;
BreakStreamEarly:
			_next_send_id[stream] = ack_id;
		} // walking streams
	} while (stream < NUM_STREAMS); // end of round

	_send_buffer = send_buffer;
	_send_buffer_bytes = send_buffer_bytes;

	lock.Release();

	PostPacketList(packet_send_head);
}

void Transport::PostPacketList(TempSendNode *packet_send_head)
{
	// Send packets after lock is released:
	while (packet_send_head)
	{
		TempSendNode *next = packet_send_head->next;

		u32 bytes = packet_send_head->negative_offset;
		u8 *data = reinterpret_cast<u8*>( packet_send_head ) - bytes;

		WARN("Transport") << "Sending packet with " << bytes;

		if (PostPacket(data, bytes + AuthenticatedEncryption::OVERHEAD_BYTES + TRANSPORT_OVERHEAD, bytes))
			_send_flow.OnPacketSend(bytes);
		else
		{
			WARN("Transport") << "Unable to post send buffer";
			break;
		}

		packet_send_head = next;
	}
}