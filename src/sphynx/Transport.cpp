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
#include <cat/io/Log.hpp>
using namespace std;
using namespace cat;
using namespace sphynx;

static StdAllocator *m_std_allocator = 0;
static Clock *m_clock = 0;

static CAT_TLS TransportTLS m_transport_tls;


//// Transport Random Padding

bool Transport::InitializeRandPad(AuthenticatedEncryption &auth_enc)
{
	// Key the random length generator
	u8 material[32];
	if (!auth_enc.GenerateKey("kernel32.dll", material, sizeof(material)))
		return false;

	ChaChaKey key;
	key.Set(material, sizeof(material));

	_rand_pad_csprng.ReKey(key, 7);

	_rand_pad_index = 64; // indicate that regeneration is needed

	return true;
}

#if defined(CAT_TRANSPORT_RANDOMIZE_LENGTH)

// LUT for unif->exp RV with a mean of 8 bytes
static const u8 CAT_RAND_PAD_EXP[256] = {
	44, 38, 35, 33, 31, 30, 28, 27, 26, 25, 25, 24, 23, 23, 22, 22, 21, 21, 20,
	20, 20, 19, 19, 18, 18, 18, 17, 17, 17, 17, 16, 16, 16, 16, 15, 15, 15, 15,
	15, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12,
	11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

bool Transport::RandPadDatagram(SendBuffer *&buffer, u32 &data_bytes)
{
	// If it is time to generate more random length padding numbers,
	if (_rand_pad_index >= sizeof(_rand_pad_source))
	{
		_rand_pad_index = 0;

		// Generate the next keystream
		_rand_pad_csprng.GenerateNeutralKeyStream((u32*)&_rand_pad_source);

		// Convert keystream bytes into exponentially-distributed lengths with mean of 8
		for (int ii = 0; ii < 64; ii += 2)
			_rand_pad_source[ii] = CAT_RAND_PAD_EXP[_rand_pad_source[ii]];
	}

	// Determine desired padding
	u8 pad = _rand_pad_source[_rand_pad_index++];
	u8 rval = _rand_pad_source[_rand_pad_index++];
	if (pad + data_bytes > _max_payload_bytes)
	{
		if (data_bytes >= _max_payload_bytes)
			return true;

		pad = _max_payload_bytes - data_bytes;
	}

	// If any padding is to be added,
	if (pad > 0)
	{
		// Resize packet to include space for padding
		u8 *pkt = SendBuffer::Resize(buffer, data_bytes + pad + SPHYNX_OVERHEAD);
		if (!pkt)
		{
			CAT_WARN("Transport") << "Ran out of memory resizing message for rand pad";
			return false;
		}

		pkt[data_bytes] = HDR_NOP;

		// Write random value to remaining bytes
		memset(pkt + data_bytes + 1, rval, pad - 1);

		// Update parameters
		buffer = SendBuffer::Promote(pkt);
		data_bytes += pad;
	}

	return true;
}

#endif // CAT_TRANSPORT_RANDOMIZE_LENGTH


//// SendQueue

CAT_INLINE void SendQueue::FreeMemory()
{
	for (OutgoingMessage *node = head, *next; node; node = next)
	{
		next = node->next;
		m_std_allocator->Release(node);
	}
}

CAT_INLINE void SendQueue::Append(OutgoingMessage *node)
{
	if (node)
	{
		if (tail) tail->next = node;
		else head = node;

		node->next = 0;

		tail = node;
	}
}

CAT_INLINE void SendQueue::Steal(SendQueue &queue)
{
	if (queue.head)
	{
		if (tail) tail->next = queue.head;
		else head = queue.head;

		tail = queue.tail;

		queue.head = queue.tail = 0;
	}
}

CAT_INLINE void SendQueue::RemoveBefore(OutgoingMessage *node)
{
	if (!node) tail = 0;

	head = node;
}


//// SentList

CAT_INLINE void SentList::FreeMemory()
{
	for (OutgoingMessage *node = head, *next; node; node = next)
	{
		next = node->next;
		Transport::FreeSentNode(node);
	}
}

CAT_INLINE void SentList::Append(OutgoingMessage *node)
{
	if (node)
	{
		if (tail) tail->next = node;
		else head = node;

		node->prev = tail;
		node->next = 0;

		tail = node;
	}
}

CAT_INLINE void SentList::RemoveBefore(OutgoingMessage *node)
{
	if (node) node->prev = 0;
	else tail = 0;

	head = node;
}

CAT_INLINE void SentList::RemoveBetween(OutgoingMessage *prev, OutgoingMessage *next)
{
	if (prev) prev->next = next;
	else head = next;

	if (next) next->prev = prev;
	else tail = prev;
}


//// OutOfOrderQueue

CAT_INLINE void OutOfOrderQueue::FreeMemory()
{
	for (RecvQueue *node = head, *next; node; node = next)
	{
		next = node->next;
		m_std_allocator->Release(node);
	}
}


//// Helpers

const char *cat::sphynx::GetSphynxErrorString(SphynxError err)
{
	switch (err)
	{
	case ERR_CLIENT_OUT_OF_MEMORY:	return "Out of memory";
	case ERR_CLIENT_INVALID_KEY:	return "Invalid key";
	case ERR_CLIENT_SERVER_ADDR:	return "Bad server address";
	case ERR_CLIENT_BROKEN_PIPE:	return "Broken pipe";
	case ERR_CLIENT_TIMEOUT:		return "Connect timeout";
	case ERR_WRONG_KEY:				return "Wrong key";
	case ERR_SERVER_FULL:			return "Server full";
	case ERR_TAMPERING:				return "Tampering detected";
	case ERR_BLOCKED:				return "Blocked";
	case ERR_SHUTDOWN:				return "Server shutdown";
	case ERR_SERVER_ERROR:			return "Server error";
	default:						return "Unknown error";
	}
}

void Transport::FreeSentNode(OutgoingMessage *node)
{
	// If node is a fragment,
	if (node->sop == SOP_FRAG)
	{
		SendFrag *frag = static_cast<SendFrag*>( node );
		OutgoingMessage *full_data_node = frag->full_data;

		// If no more fragments exist for the full data node,
		if (!--full_data_node->frag_count)
		{
			// If it is a huge message,
			if (full_data_node->GetBytes() == FRAG_HUGE)
			{
				// If message has completed sending,
				if (full_data_node->huge_remaining == 0)
				{
					m_std_allocator->Release(full_data_node);
				}
			}
			else
			{
				// If message has completed sending,
				if (full_data_node->sent_bytes >= full_data_node->GetBytes())
				{
					m_std_allocator->Release(full_data_node);
				}
			}
		}
	}

	m_std_allocator->Release(node);
}

CAT_INLINE void Transport::QueueFragFree(TransportTLS *tls, u8 *data)
{
	// Add to the free frag list
	u32 count = tls->free_list_count;
	tls->free_list[count] = data;
	tls->free_list_count = ++count;
}

void Transport::QueueDelivery(TransportTLS *tls, u32 stream, u8 *data, u32 data_bytes, bool huge_fragment)
{
	u32 depth = tls->delivery_queue_depth;

	IncomingMessage *msg = &tls->delivery_queue[depth];
	msg->stream = (StreamMode)stream;
	msg->data = data;
	msg->bytes = data_bytes;
	msg->huge_fragment = huge_fragment;

	if (++depth < TransportTLS::DELIVERY_QUEUE_DEPTH)
		tls->delivery_queue_depth = depth;
	else
	{
		OnMessages(tls->delivery_queue, depth);
		tls->delivery_queue_depth = 0;

		// Free memory for fragments
		for (u32 ii = 0, count = tls->free_list_count; ii < count; ++ii)
			delete []tls->free_list[ii];
		tls->free_list_count = 0;
	}
}

void Transport::DeliverQueued(TransportTLS *tls)
{
	u32 depth = tls->delivery_queue_depth;
	if (depth > 0)
	{
		OnMessages(tls->delivery_queue, depth);
		tls->delivery_queue_depth = 0;

		// Free memory for fragments
		for (u32 ii = 0, count = tls->free_list_count; ii < count; ++ii)
			delete []tls->free_list[ii];
		tls->free_list_count = 0;
	}
}


//// Transport

Transport::Transport()
{
	// Acquire standard allocator
	m_std_allocator = StdAllocator::ref();
	m_clock = Clock::ref();

	// Receive state
	CAT_OBJCLR(_got_reliable);

	CAT_OBJCLR(_fragments);

	CAT_OBJCLR(_recv_wait);

	// Send state
	_send_cluster.Clear();
	_send_flush_after_processing = false;

	CAT_OBJCLR(_send_queue);
	CAT_OBJCLR(_sending_queue);
	CAT_OBJCLR(_sent_list);

	// Just clear these for now.  When security is initialized these will be filled in
	CAT_OBJCLR(_next_send_id);
	CAT_OBJCLR(_send_next_remote_expected);
	CAT_OBJCLR(_next_recv_expected_id);

	_disconnect_countdown = SHUTDOWN_TICK_COUNT;
	_disconnect_reason = DISCO_CONNECTED;

	_outgoing_datagrams.Clear();
	_outgoing_datagrams_count = 0;
	_outgoing_datagrams_bytes = 0;
}

Transport::~Transport()
{
	// Release memory for send buffer
	if (_send_cluster.front) SendBuffer::Release(_send_cluster.front);

	// Release memory for outgoing datagrams
	for (BatchHead *next, *node = _outgoing_datagrams.head; node; node = next)
	{
		next = node->batch_next;
		m_std_allocator->Release(node);
	}

	// For each stream,
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		// Release memory for fragment buffer
		if (_fragments[stream].buffer)
			delete []_fragments[stream].buffer;

		_recv_wait[stream].FreeMemory();
		_sent_list[stream].FreeMemory();
		_send_queue[stream].FreeMemory();
		_sending_queue[stream].FreeMemory();
	}
}

void Transport::Disconnect(u8 reason)
{
	// If already disconnected,
	if (IsDisconnected()) return;

	_disconnect_reason = reason;

	WriteDisconnect(reason);

	OnDisconnectReason(reason);
}

void Transport::InitializePayloadBytes(bool ip6)
{
	_udpip_bytes = UDP_HEADER_BYTES;

	if (ip6) _udpip_bytes += IPV6_HEADER_BYTES;
	else 	 _udpip_bytes += IPV4_HEADER_BYTES;

	_max_payload_bytes = MINIMUM_MTU - _udpip_bytes - SPHYNX_OVERHEAD;
}

bool Transport::InitializeTransportSecurity(bool is_initiator, AuthenticatedEncryption &auth_enc)
{
	/*
		Most protocols just initialize the ACK IDs to zeros at the start.
		The problem is that this gives attackers known plaintext bytes inside
		an encrypted channel.  I used this to break the WoW encryption without
		knowing the key, for example.  Sure, if a cryptosystem can be broken
		with known plaintext there is a problem ANYWAY but I mean why make it
		easier than it needs to be?  So we derive the initial ACK IDs using a
		key derivation function (KDF) based on the session key.
	*/

	// Randomize next send ACK-ID
	if (!auth_enc.GenerateKey(is_initiator ? "ws2_32.dll" : "winsock.ocx", _next_send_id, sizeof(_next_send_id)))
		return false;
	memcpy(_send_next_remote_expected, _next_send_id, sizeof(_send_next_remote_expected));

	// Randomize next recv ACK-ID
	if (!auth_enc.GenerateKey(!is_initiator ? "ws2_32.dll" : "winsock.ocx", _next_recv_expected_id, sizeof(_next_recv_expected_id)))
		return false;

	if (!InitializeRandPad(auth_enc))
		return false;

	return true;
}

void Transport::TickTransport(u32 now)
{
	// If disconnected,
	if (IsDisconnected())
	{
		// If the disconnect has completed sending,
		if (--_disconnect_countdown == 0)
		{
			// Notify derived class
			OnDisconnectComplete();
		}
		else
		{
			// Write another disconnect packet
			WriteDisconnect(_disconnect_reason);
		}

		// Skip other timed events
		return;
	}

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
		if (_sent_list[stream].head)
		{
			loss_count = RetransmitLost(now);
			break;
		}
	}

	_send_flow.OnTick(now, loss_count);

	FlushWrites();
}

void Transport::OnTransportDatagrams(const BatchSet &delivery)
{
	// Initialize the delivery queue
	TransportTLS *tls = &m_transport_tls;
	tls->delivery_queue_depth = 0;
	tls->free_list_count = 0;

	// Simulate 5% packetloss
	//if (tls->csprng->GenerateUnbiased(0, 19) == 2)
	//	return;

	// For each buffer in the batch,
	for (BatchHead *node = delivery.head; !IsDisconnected() && node; node = node->batch_next)
	{
		RecvBuffer *buffer = static_cast<RecvBuffer*>( node );
		u8 *data = GetTrailingBytes(buffer);
		s32 bytes = buffer->data_bytes;
		u32 recv_time = buffer->event_msec;

		// Start peeling out messages from the warm gooey center of the packet
		u32 ack_id = 0, stream = 0;

		CAT_INANE("Transport") << "Datagram dump " << bytes << ":" << HexDumpString(data, bytes);

		while (bytes >= 1)
		{
			// Decode data_bytes
			u8 hdr = data[0];
			u32 data_bytes = hdr & BLO_MASK;

			CAT_INANE("Transport") << " -- Processing subheader " << (int)hdr;

			// If message length requires another byte to represent,
			u32 hdr_bytes = 1;
			if (hdr & C_MASK)
			{
				data_bytes |= (u16)data[1] << BHI_SHIFT;
				++hdr_bytes;
			}
			data += hdr_bytes;
			bytes -= hdr_bytes;

			// If this message has an ACK-ID attached,
			if (hdr & I_MASK)
			{
				if (bytes < 1)
				{
					CAT_WARN("Transport") << "Truncated message ignored (1)";
					break;
				}

				// Decode variable-length ACK-ID into ack_id and stream:
				u8 id = *data++;
				--bytes;
				stream = id & 3;
				ack_id = (id >> 2) & 0x1f;
				u32 counter_bits;

				if (id & C_MASK)
				{
					if (bytes < 1)
					{
						CAT_WARN("Transport") << "Truncated message ignored (2)";
						break;
					}

					id = *data++;
					--bytes;
					ack_id |= (u32)(id & 0x7f) << 5;

					if (id & C_MASK)
					{
						if (bytes < 1)
						{
							CAT_WARN("Transport") << "Truncated message ignored (3)";
							break;
						}

						id = *data++;
						--bytes;
						ack_id |= (u32)id << 12;

						counter_bits = 20;
					}
					else
						counter_bits = 12;
				}
				else
					counter_bits = 5;

				ack_id = ReconstructCounter(counter_bits, _next_recv_expected_id[stream], ack_id);
			}
			else if (hdr & R_MASK)
			{
				// Could check for uninitialized ACK-ID here but I do not think that sending
				// this type of malformed packet can hurt the server.
				++ack_id;
			}

			if (bytes < (s32)data_bytes)
			{
				CAT_WARN("Transport") << "Truncated transport message ignored";
				break;
			}

			// If reliable message,
			if (hdr & R_MASK)
			{
				CAT_INANE("Transport") << "Got # " << stream << ":" << ack_id;

				s32 diff = (s32)(ack_id - _next_recv_expected_id[stream]);

				// If message is next expected,
				if (diff == 0)
				{
					u32 super_opcode = (hdr >> SOP_SHIFT) & SOP_MASK;

					// Process it immediately
					if (super_opcode == SOP_FRAG)
						OnFragment(tls, recv_time, data, data_bytes, stream);
					else if (data_bytes > 0)
					{
						if (super_opcode == SOP_DATA)
							QueueDelivery(tls, stream, data, data_bytes, false);
						else if (super_opcode == SOP_INTERNAL)
							OnInternal(recv_time, data, data_bytes);
						else CAT_WARN("Transport") << "Invalid reliable super opcode ignored";
					}
					else CAT_WARN("Transport") << "Zero-length reliable message ignored";

					RunReliableReceiveQueue(tls, recv_time, ack_id + 1, stream);
				}
				else if (diff > 0) // Message is due to arrive
				{
					StoreReliableOutOfOrder(tls, recv_time, data, data_bytes, ack_id, stream, (hdr >> SOP_SHIFT) & SOP_MASK);
				}
				else
				{
					CAT_INFO("Transport") << "Ignored duplicate rolled reliable message " << stream << ":" << ack_id;

					CAT_INANE("Transport") << "Rel dump " << bytes << ":" << HexDumpString(data, bytes);

					_got_reliable[stream] = true;
				}
			}
			else if (data_bytes > 0) // Unreliable message:
			{
				u32 super_opcode = (hdr >> SOP_SHIFT) & SOP_MASK;

				if (super_opcode == SOP_DATA)
					QueueDelivery(tls, stream, data, data_bytes, false);
				else if (super_opcode == SOP_ACK)
					OnACK(recv_time, data, data_bytes);
				else if (super_opcode == SOP_INTERNAL)
					OnInternal(recv_time, data, data_bytes);
			}
			else if (hdr == HDR_NOP)
			{
				// Abort processing this message on first NOP
				CAT_INANE("Transport") << "Aborted processing on NOP";
				break;
			}

			bytes -= data_bytes;
			data += data_bytes;
		} // while bytes >= 1
	} // end for each buffer

	// Deliver any messages that are queued up
	DeliverQueued(tls);

	// If flush was requested,
	if (_send_flush_after_processing)
	{
		FlushWrites();
		_send_flush_after_processing = false;
	}
}

void Transport::RunReliableReceiveQueue(TransportTLS *tls, u32 recv_time, u32 ack_id, u32 stream)
{
	RecvQueue *node = _recv_wait[stream].head;

	// If no queue to run or queue is not ready yet,
	if (!node || node->id != ack_id)
	{
		// Just update next expected id and set flag to send acks on next tick
		_next_recv_expected_id[stream] = ack_id;
		_got_reliable[stream] = true;
		return;
	}

	// For each queued message that is now ready to go,
	u32 next_ack_id = ack_id;
	do
	{
		// Grab the queued message
		u32 super_opcode = node->sop;
		u8 *old_data = GetTrailingBytes(node);
		u32 old_data_bytes = node->bytes;

		// Process queued message
		if (super_opcode == SOP_FRAG)
		{
			// Fragments are always processed in order, and zero data bytes indicates abortion
			OnFragment(tls, recv_time, old_data, old_data_bytes, stream);
		}
		else if (old_data_bytes > 0)
		{
			CAT_WARN("Transport") << "Running queued message # " << stream << ":" << next_ack_id;

			if (super_opcode == SOP_DATA)
				QueueDelivery(tls, stream, old_data, old_data_bytes, false);
			else if (super_opcode == SOP_INTERNAL)
				OnInternal(recv_time, old_data, old_data_bytes);

			// NOTE: Unordered stream writes zero-length messages
			// to the receive queue since it processes immediately
			// and does not need to store the data.
		}

		// And proceed on to next message
		++next_ack_id;

		RecvQueue *next = node->next;
		m_std_allocator->Release(node);
		node = next;
	} while (node && node->id == next_ack_id);

	// Reduce the size of the wait queue
	_recv_wait[stream].size -= next_ack_id - ack_id;
	_recv_wait[stream].head = node;
	_next_recv_expected_id[stream] = next_ack_id;
	_got_reliable[stream] = true;
}

void Transport::StoreReliableOutOfOrder(TransportTLS *tls, u32 recv_time, u8 *data, u32 data_bytes, u32 ack_id, u32 stream, u32 super_opcode)
{
	// If too many out of order arrivals already,
	u32 count = _recv_wait[stream].size;
	if (count >= OUT_OF_ORDER_LIMIT)
	{
		CAT_WARN("Transport") << "Out of room for out-of-order arrivals";
		return;
	}

	// Walk forwards because the skip list makes this straight-forward (pun intended)
	RecvQueue *next = _recv_wait[stream].head;
	RecvQueue *prev = 0, *prev_seq = 0;

	// Search for queue insertion point
	u32 ii = 0;
	while (next)
	{
		// If insertion point is found,
		if (ack_id < next->id)
			break;

		// Node is either in this sequence or after it

		// Investigate the end of sequence
		RecvQueue *eos = next->eos;

		// If ack_id is contained within the sequence,
		if (ack_id <= eos->id)
		{
			CAT_WARN("Transport") << "Ignored duplicate queued reliable message";
			return;
		}

		// Set up for the next loop
		prev_seq = next;
		prev = eos;
		next = eos->next;

		// If too many attempts to find insertion point already,
		if (++ii >= OUT_OF_ORDER_LOOPS)
		{
			CAT_WARN("Transport") << "Dropped message due to swiss cheese";
			return;
		}
	}

	CAT_WARN("Transport") << "Queuing out-of-order message # " << stream << ":" << ack_id;
	CAT_INANE("Transport") << "Out-of-order message " << data_bytes << ":" << HexDumpString(data, data_bytes);

	u32 stored_bytes;

	if (stream == STREAM_UNORDERED)
	{
		// If it is a fragment,
		if (super_opcode == SOP_FRAG)
		{
			// Then wait until it is in order to process it
			stored_bytes = data_bytes;
		}
		else if (data_bytes > 0)
		{
			if (super_opcode == SOP_DATA)
				QueueDelivery(tls, stream, data, data_bytes, false);
			else if (super_opcode == SOP_INTERNAL)
				OnInternal(recv_time, data, data_bytes);

			stored_bytes = 0;
		}
		else
		{
			CAT_WARN("Transport") << "Zero-length reliable message ignored";
			return;
		}
	}
	else
	{
		stored_bytes = data_bytes;
	}

	RecvQueue *new_node = m_std_allocator->AcquireTrailing<RecvQueue>(stored_bytes);
	if (!new_node)
	{
		CAT_WARN("Transport") << "Out of memory for incoming packet queue";
		return;
	}

	// Initialize data
	new_node->bytes = stored_bytes;
	new_node->sop = super_opcode;
	new_node->id = ack_id;
	memcpy(GetTrailingBytes(new_node), data, stored_bytes);

	// Link into list
	new_node->next = next;
	if (prev) prev->next = new_node;
	else _recv_wait[stream].head = new_node;

	// Link into sequence (skip list),
	if (prev && prev->id + 1 == ack_id)
		prev_seq->eos = (next && ack_id + 1 == next->id) ? next->eos : new_node;
	else if (next && ack_id + 1 == next->id)
		new_node->eos = next->eos;
	else
		new_node->eos = new_node;

	_got_reliable[stream] = true;
	_recv_wait[stream].size = count + 1;
}

void Transport::OnFragment(TransportTLS *tls, u32 recv_time, u8 *data, u32 bytes, u32 stream)
{
	//INFO("Transport") << "OnFragment " << bytes << ":" << HexDumpString(data, bytes);

	u16 frag_length = _fragments[stream].length;
	u16 frag_offset = _fragments[stream].offset;

	// If fragment is starting,
	if (!frag_offset)
	{
		if (bytes < FRAG_HEADER_BYTES + 1)
		{
			CAT_WARN("Transport") << "Truncated message fragment head ignored";
			return;
		}
		else
		{
			frag_length = getLE(*(u16*)(data));
			data += FRAG_HEADER_BYTES;
			bytes -= FRAG_HEADER_BYTES;

			// If message is huge,
			if (frag_length == FRAG_HUGE)
				_fragments[stream].offset = 1; // Mark as having seen fragment header
			else
			{
				// Allocate fragment buffer
				_fragments[stream].buffer = new (std::nothrow) u8[frag_length];
				if (!_fragments[stream].buffer)
				{
					CAT_WARN("Transport") << "Out of memory: Unable to allocate fragment buffer";
					return;
				}
				else
				{
					_fragments[stream].length = frag_length;
					_fragments[stream].offset = 0;
				}
			}
		}

		// Fall-thru to processing data part of fragment message:
	}

	// If fragment length is huge,
	if (frag_length == FRAG_HUGE)
	{
		// Deliver this buffer
		QueueDelivery(tls, stream, data, bytes, true);

		// If got final part,
		if (bytes == 0)
		{
			// Reset the stream's fragment offset to prepare for the next fragmented message
			_fragments[stream].offset = 0;
			CAT_WARN("Transport") << "Aborted huge fragment transfer in stream " << stream;
		}

		return;
	}

	// If there are no data bytes in this fragment,
	if (bytes == 0)
	{
		// This is a request to abort the fragment
		if (_fragments[stream].buffer)
			delete []_fragments[stream].buffer;

		_fragments[stream].length = 0;
		CAT_WARN("Transport") << "Aborted fragment transfer in stream " << stream;
		return;
	}

	u32 fragment_remaining = _fragments[stream].length - _fragments[stream].offset;

	// If the fragment is now complete,
	if (bytes >= fragment_remaining)
	{
		u8 *buffer = _fragments[stream].buffer;

		if (bytes > fragment_remaining)
		{
			CAT_WARN("Transport") << "Message fragment overflow truncated";
		}

		memcpy(buffer + _fragments[stream].offset, data, fragment_remaining);

		// Queue up this buffer for deletion after we are done
		QueueFragFree(tls, buffer);

		// Deliver this buffer
		QueueDelivery(tls, stream, buffer, _fragments[stream].length, false);

		_fragments[stream].length = 0;
	}
	else
	{
		memcpy(_fragments[stream].buffer + _fragments[stream].offset, data, bytes);
		_fragments[stream].offset += bytes;
	}
}

bool Transport::WriteOOB(u8 msg_opcode, const void *msg_data, u32 msg_bytes, SuperOpcode super_opcode)
{
	u32 data_bytes = 1 + msg_bytes;
	u32 needed = MAX_MESSAGE_HEADER_BYTES + data_bytes + SPHYNX_OVERHEAD;

	u8 *pkt = SendBuffer::Acquire(needed);
	if (!pkt)
	{
		CAT_WARN("Transport") << "Out of memory for out-of-band message";
		return false;
	}

	u32 offset = 1;

	// Write header
	if (data_bytes <= BLO_MASK)
		pkt[0] = (u8)data_bytes | (super_opcode << SOP_SHIFT);
	else
	{
		pkt[0] = (u8)(data_bytes & BLO_MASK) | (super_opcode << SOP_SHIFT) | C_MASK;
		pkt[1] = (u8)(data_bytes >> BHI_SHIFT);
		++offset;
	}

	// Write data
	pkt[offset++] = msg_opcode;
	memcpy(pkt + offset, msg_data, msg_bytes);

	bool success = WriteDatagram(pkt, offset + msg_bytes);

	_send_cluster_lock.Enter();
	_send_flow.OnPacketSend(_udpip_bytes + needed);
	_send_cluster_lock.Leave();

	return success;
}

bool Transport::WriteUnreliable(u8 msg_opcode, const void *vmsg_data, u32 msg_bytes, SuperOpcode super_opcode)
{
	const u8 *msg_data = reinterpret_cast<const u8*>( vmsg_data );

	u32 max_payload_bytes = _max_payload_bytes;
	u32 data_bytes = msg_bytes + 1;
	u32 header_bytes = data_bytes > BLO_MASK ? 2 : 1;
	u32 needed = header_bytes + data_bytes;

	// Fail on invalid input
	if (needed > max_payload_bytes)
	{
		CAT_WARN("Transport") << "Invalid input: Unreliable buffer size request too large";
		return false;
	}

	_send_cluster_lock.Enter();

	// If growing the send buffer cannot contain the new message,
	if (_send_cluster.bytes + needed > max_payload_bytes)
	{
		QueueWriteDatagram(_send_cluster.front, _send_cluster.bytes);
		_send_cluster.Clear();
	}

	// Create or grow buffer and write into it
	u8 *pkt;
	while (!(pkt = _send_cluster.Grow(needed)));

	// Write header
	if (data_bytes <= BLO_MASK)
		pkt[0] = (u8)data_bytes | (super_opcode << SOP_SHIFT);
	else
	{
		pkt[0] = (u8)(data_bytes & BLO_MASK) | (super_opcode << SOP_SHIFT) | C_MASK;
		pkt[1] = (u8)(data_bytes >> BHI_SHIFT);
	}
	pkt += header_bytes;

	// Write data
	pkt[0] = msg_opcode;
	memcpy(pkt + 1, msg_data, msg_bytes);

	_send_cluster_lock.Leave();

	CAT_INFO("Transport") << "Wrote unreliable message with " << data_bytes << " bytes";

	return true;
}

bool Transport::WriteReliable(StreamMode stream, u8 msg_opcode, const void *msg_data, u32 msg_bytes, SuperOpcode super_opcode)
{
	u32 data_bytes = 1 + msg_bytes;
	u8 *msg = OutgoingMessage::Acquire(data_bytes);
	if (!msg) return false;

	msg[0] = msg_opcode;
	memcpy(msg + 1, msg_data, msg_bytes);

	return WriteReliableZeroCopy(stream, msg, data_bytes, super_opcode);
}

bool Transport::WriteReliableZeroCopy(StreamMode stream, u8 *msg, u32 msg_bytes, SuperOpcode super_opcode)
{
	if (msg_bytes > MAX_MESSAGE_SIZE)
	{
		CAT_WARN("Transport") << "Reliable write request too large " << msg_bytes;
		OutgoingMessage::Release(msg);
		return false;
	}

	// Fill the object
	OutgoingMessage *node = OutgoingMessage::Promote(msg);
	node->SetBytes(msg_bytes);
	node->frag_count = 0;
	node->sop = super_opcode;
	node->send_bytes = 0;
	node->sent_bytes = 0;

	// Add to back of send queue
	_send_queue_lock.Enter();
	_send_queue[stream].Append(node);
	_send_queue_lock.Leave();

	CAT_INFO("Transport") << "Appended reliable message with " << msg_bytes << " bytes to stream " << stream;

	return true;
}

bool Transport::WriteHuge(StreamMode stream, IHugeSource *source)
{
	SendHuge *node = m_std_allocator->AcquireObject<SendHuge>();
	if (!node) return false;

	// Fill the object
	node->SetBytes(FRAG_HUGE);
	node->huge_remaining = source->GetRemaining(stream);
	node->frag_count = 0;
	node->sop = SOP_DATA;
	node->send_bytes = 0;
	node->sent_bytes = 0;
	node->next = 0;
	node->source = source;

	// Add to back of send queue
	_send_queue_lock.Enter();
	_send_queue[stream].Append(node);
	_send_queue_lock.Leave();

	CAT_INFO("Transport") << "Appended huge message placeholder to stream " << stream;

	return true;
}

void Transport::Retransmit(u32 stream, OutgoingMessage *node, u32 now)
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
	u32 frag_overhead = 0;
	u16 frag_total_bytes = 0;

	// If node is a fragment,
	if (node->sop == SOP_FRAG)
	{
		SendFrag *frag = static_cast<SendFrag*>( node );
		OutgoingMessage *full_node = frag->full_data;
		frag_total_bytes = full_node->GetBytes();

		// If the fragment is from a huge message,
		if (frag_total_bytes == FRAG_HUGE)
			data = GetTrailingBytes(frag);
		else
			data = GetTrailingBytes(full_node) + frag->offset;

		// If this is the first fragment of the message,
		if (frag->offset == 0) frag_overhead = FRAG_HEADER_BYTES;
	}
	else
	{
		data = GetTrailingBytes(node);
	}

	// Calculate message length
	u16 copy_bytes = node->GetBytes();
	u32 data_bytes = frag_overhead + copy_bytes;
	u32 hdr_bytes = (data_bytes <= BLO_MASK) ? 1 : 2;
	u32 ack_id = node->id;

	_send_cluster_lock.Enter();
	SendCluster cluster = _send_cluster;

	// Calculate ack_id_overhead
	u32 ack_id_overhead = (cluster.stream != stream || cluster.ack_id != ack_id) ? MAX_ACK_ID_BYTES : 0;
	u32 msg_bytes = hdr_bytes + ack_id_overhead + data_bytes;

	// If the growing send buffer cannot contain the new message,
	if (cluster.bytes + msg_bytes > _max_payload_bytes)
	{
		QueueWriteDatagram(cluster.front, cluster.bytes);
		cluster.Clear();

		// Recalculate message length
		msg_bytes += MAX_ACK_ID_BYTES - ack_id_overhead;
		ack_id_overhead = MAX_ACK_ID_BYTES;
	}

	// Create or grow buffer and write into it
	u8 *pkt;
	while (!(pkt = cluster.Grow(msg_bytes)));

	ClusterReliableAppend(stream, ack_id, pkt, ack_id_overhead, frag_overhead,
		cluster, node->sop, data, copy_bytes, frag_total_bytes);

	_send_cluster = cluster;
	_send_cluster_lock.Leave();

	node->ts_lastsend = now;

	CAT_INFO("Transport") << "Retransmitted stream " << stream << " # " << ack_id;
}

void Transport::FlushWrites()
{
	WriteQueuedReliable();

	// If no data to flush (common),
	if (_send_cluster.bytes == 0 && _outgoing_datagrams_count == 0)
		return;

	_send_cluster_lock.Enter();

	u32 count = _outgoing_datagrams_count;

	if (_send_cluster.bytes)
	{
		QueueWriteDatagram(_send_cluster.front, _send_cluster.bytes);
		_send_cluster.Clear();
		++count;
	}

	BatchSet outgoing_datagrams = _outgoing_datagrams;
	_outgoing_datagrams.Clear();

	_send_flow.OnPacketSend(_outgoing_datagrams_bytes + _outgoing_datagrams_count * _udpip_bytes);

	_outgoing_datagrams_count = 0;
	_outgoing_datagrams_bytes = 0;

	_send_cluster_lock.Leave();

	// If any datagrams to write,
	if (outgoing_datagrams.head)
		WriteDatagrams(outgoing_datagrams, count);
}

void Transport::WriteACK()
{
	u8 packet[MAXIMUM_MTU];
	u8 *offset = packet + MAX_MESSAGE_HEADER_BYTES;
	u32 max_payload_bytes = _max_payload_bytes;
	u32 remaining = max_payload_bytes - MAX_MESSAGE_HEADER_BYTES;

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
				CAT_WARN("Transport") << "ACK packet truncated due to lack of space(1)";
				break;
			}

			u32 rollup_ack_id = _next_recv_expected_id[stream];

			// Write ROLLUP
			offset[0] = (u8)(1 | (stream << 1) | ((rollup_ack_id & 31) << 3));
			offset[1] = (u8)(rollup_ack_id >> 5);
			offset[2] = (u8)(rollup_ack_id >> 13);
			offset += 3;
			remaining -= 3;

			CAT_INFO("Transport") << "Acknowledging rollup # " << stream << ":" << rollup_ack_id;

			RecvQueue *eos, *node = _recv_wait[stream].head;
			u32 last_id = rollup_ack_id;

			for (u32 ii = 0; node && ii < OUT_OF_ORDER_LOOPS; ++ii, node = eos->next)
			{
				// Get end of sequence pointer (eos)
				eos = node->eos;

				// Encode RANGE: START(3) || END(3)
				if (remaining < 6)
				{
					CAT_WARN("Transport") << "ACK packet truncated due to lack of space(2)";
					break;
				}

				u32 start_id = node->id, end_id = eos->id;

				// ACK messages transmits ids relative to the previous one in the datagram
				u32 start_offset = start_id - last_id;
				u32 end_offset = end_id - start_id;
				last_id = end_id;

				CAT_INFO("Transport") << "Acknowledging range # " << stream << ":" << start_id << " - " << end_id;

				// Write START
				u8 ack_hdr = (u8)((end_offset ? 2 : 0) | (start_offset << 2));
				if (start_offset & ~0x1f)
				{
					offset[0] = ack_hdr | 0x80;

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
					*offset++ = ack_hdr;
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
			} // for each range in the waiting list

			// If we exhausted all in the list, unset flag
			if (!node) _got_reliable[stream] = false;
		}
	}

	u32 msg_bytes = max_payload_bytes - remaining;
	u8 *packet_copy_source = packet;

	// Write header
	u32 data_bytes = msg_bytes - MAX_MESSAGE_HEADER_BYTES;
	if (data_bytes <= BLO_MASK)
	{
		// Eat first byte and skip sending BHI if possible
		packet[1] = (u8)(data_bytes | (SOP_ACK << SOP_SHIFT));
		++packet_copy_source;
		--msg_bytes;
	}
	else
	{
		packet[0] = (u8)((data_bytes & BLO_MASK) | (SOP_ACK << SOP_SHIFT) | C_MASK);
		packet[1] = (u8)(data_bytes >> BHI_SHIFT);
	}

	// Now that the packet is constructed, write it into the send cluster

	_send_cluster_lock.Enter();

	// If the growing send buffer cannot contain the new message,
	if (_send_cluster.bytes + msg_bytes > max_payload_bytes)
	{
		QueueWriteDatagram(_send_cluster.front, _send_cluster.bytes);
		_send_cluster.Clear();
	}

	// Create or grow buffer
	u8 *pkt;
	while (!(pkt = _send_cluster.Grow(msg_bytes)));

	memcpy(pkt, packet_copy_source, msg_bytes);

	_send_cluster_lock.Leave();
}

u32 Transport::RetransmitLost(u32 now)
{
	u32 loss_count = 0;

	// Retransmit lost packets
	for (int stream = 0; stream < NUM_STREAMS; ++stream)
	{
		OutgoingMessage *node = _sent_list[stream].head;
		if (!node) continue;

		u32 timeout = _send_flow.GetHeadTimeout(stream);

		// For each node that might be ready for a retransmission,
		do
		{
			s32 mia_time = now - node->ts_lastsend;

			u32 backoff = node->ts_lastsend - node->ts_firstsend;
			if (backoff > 4 * timeout) backoff = 4 * timeout;

			if (mia_time >= (s32)(timeout + backoff))
			{
				Retransmit(stream, node, now);

				// Record a loss if the node is representative of loss
				loss_count += node->loss_on;
			}
			else if ((s32)(now - node->ts_firstsend) < (s32)timeout)
			{
				// Nodes are added to the end of the sent list, so as soon as it
				// finds one that cannot possibly be retransmitted it is done
				break;
			}

			node = node->next;
		} while (node);
	}

	return loss_count;
}

bool Transport::PostMTUProbe(u32 mtu)
{
	CAT_INANE("Transport") << "Posting MTU Probe";

	if (mtu < MINIMUM_MTU || mtu > MAXIMUM_MTU)
		return false;

	u32 payload_bytes = mtu - _udpip_bytes - SPHYNX_OVERHEAD;

	u8 *pkt = SendBuffer::Acquire(payload_bytes + SPHYNX_OVERHEAD);
	if (!pkt)
	{
		CAT_WARN("Transport") << "Out of memory error while posting MTU probe";
		return false;
	}

	// Write message
	//	I = 0 (no ack id follows)
	//	R = 0 (unreliable)
	//	C = 1 (large packet size)
	//	SOP = IOP_C2S_MTU_PROBE
	u32 data_bytes = payload_bytes - MAX_MESSAGE_HEADER_BYTES;
	pkt[0] = (u8)((SOP_INTERNAL << SOP_SHIFT) | C_MASK | (data_bytes & BLO_MASK));
	pkt[1] = (u8)(data_bytes >> BHI_SHIFT);
	pkt[2] = IOP_C2S_MTU_PROBE;

	// Fill payload with random bytes
	u8 key_stream[64];
	_rand_pad_csprng.GenerateNeutralKeyStream((u32*)key_stream);
	u32 pad_count = data_bytes + 1;
	u8 *pkt_pad = pkt + 3;

	// For each 64 byte chunk,
	while (pad_count >= 64)
	{
		memcpy(pkt_pad, key_stream, 64);
		pkt_pad += 64;
		pad_count -=64;
	}

	// For last chunk < 64 bytes,
	if (pad_count > 0)
		memcpy(pkt_pad, key_stream, pad_count);

	bool success = WriteDatagram(pkt, payload_bytes);

	_send_cluster_lock.Enter();
	_send_flow.OnPacketSend(mtu);
	_send_cluster_lock.Leave();

	return success;
}

CAT_INLINE void Transport::RetransmitNegative(u32 recv_time, u32 stream, u32 last_ack_id, u32 &loss_count)
{
	// Just saw the end of a stream's ACK list.
	// We can now detect losses: Any node that is under
	// last_ack_id that still remains in the sent list
	// is probably lost.

	OutgoingMessage *rnode = _sent_list[stream].head;

	if (rnode)
	{
		u32 timeout = _send_flow.GetNACKTimeout(stream);

		while ((s32)(last_ack_id - rnode->id) > 0)
		{
			s32 mia_time = recv_time - rnode->ts_lastsend;

			u32 backoff = rnode->ts_lastsend - rnode->ts_firstsend;
			if (backoff > 4 * timeout) backoff = 4 * timeout;

			if (mia_time >= (s32)(timeout + backoff))
			{
				Retransmit(stream, rnode, recv_time);

				// Record a loss if the node is representative of loss
				loss_count += rnode->loss_on;
			}

			rnode = rnode->next;
			if (!rnode) break;
		}
	}
}

void Transport::OnACK(u32 recv_time, u8 *data, u32 data_bytes)
{
	u32 stream = NUM_STREAMS, last_ack_id = 0;
	OutgoingMessage *node = 0;
	u32 loss_count = 0;
	u32 acknowledged_data_sum = 0;

	CAT_INANE("Transport") << "Got ACK with " << data_bytes << " bytes of data to decode ----";

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
					RetransmitNegative(recv_time, stream, last_ack_id, loss_count);

				stream = (ida >> 1) & 3;
				u32 ack_id = ((u32)idc << 13) | ((u16)idb << 5) | (ida >> 3);

				node = _sent_list[stream].head;

				if (node)
				{
					ack_id = ReconstructCounter<21>(node->id, ack_id);

					// Update the send next remote expected ack id
					_send_next_remote_expected[stream] = ack_id;

					last_ack_id = ack_id;

					CAT_INFO("Transport") << "Got acknowledgment for rollup # " << stream << ":" << ack_id;

					// If the id got rolled,
					if ((s32)(ack_id - node->id) > 0)
					{
						// For each rolled node,
						do
						{
							if (node->loss_on)
							{
								_send_flow.OnACK(recv_time, node);
								acknowledged_data_sum += _udpip_bytes;
							}
							acknowledged_data_sum += 2 + node->GetBytes();

							OutgoingMessage *next = node->next;
							FreeSentNode(node);
							node = next;
						} while (node && (s32)(ack_id - node->id) > 0);

						_sent_list[stream].RemoveBefore(node);
					}
				}
			}
			else
			{
				CAT_WARN("Transport") << "Truncated ACK ignored(1)";
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
							CAT_WARN("Transport") << "Truncated ACK ignored(2)";
							break;
						}
					}
				}
				else
				{
					CAT_WARN("Transport") << "Truncated ACK ignored(3)";
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
									CAT_WARN("Transport") << "Truncated ACK ignored(4)";
									break;
								}
							}
						}
						else
						{
							CAT_WARN("Transport") << "Truncated ACK ignored(5)";
							break;
						}
					}
				}
				else
				{
					CAT_WARN("Transport") << "Truncated ACK ignored(6)";
					break;
				}
			}

			CAT_INFO("Transport") << "Got acknowledgment for range # " << stream << ":" << start_ack_id << " - " << end_ack_id;

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
					OutgoingMessage *prev = node->prev;

					// While nodes are in range,
					do 
					{
						if (node->loss_on)
						{
							_send_flow.OnACK(recv_time, node);
							acknowledged_data_sum += _udpip_bytes;
						}
						acknowledged_data_sum += 2 + node->GetBytes();

						OutgoingMessage *next = node->next;
						FreeSentNode(node);
						node = next;
					} while (node && (s32)(end_ack_id - node->id) >= 0);

					_sent_list[stream].RemoveBetween(prev, node);
				}

				// Next range start is offset from the end of this range
				last_ack_id = end_ack_id;

			} // nodes remain to check
		} // field is range
	} // while data bytes > 0

	// Retransmit lost packets
	if (stream < NUM_STREAMS)
		RetransmitNegative(recv_time, stream, last_ack_id, loss_count);

	// Inform the flow control algorithm
	_send_flow.OnACKDone(recv_time, loss_count, acknowledged_data_sum);
}

OutgoingMessage *Transport::DequeueBandwidth(OutgoingMessage *node, s32 available_bytes, s32 &bandwidth)
{
	s32 buffer_remaining;

	// For each node in the list,
	for (buffer_remaining = available_bytes; buffer_remaining > 0 && node; node = node->next)
	{
		if (node->GetBytes() == FRAG_HUGE)
		{
			u64 send_remaining = node->huge_remaining;

			// If this node ate the last of the bandwidth,
			if (send_remaining > buffer_remaining + FRAG_THRESHOLD)
				node->send_bytes = buffer_remaining;
			else
				node->send_bytes = (u32)send_remaining;
		}
		else
		{
			u32 send_remaining = node->GetBytes() - node->sent_bytes;

			if (send_remaining <= buffer_remaining + FRAG_THRESHOLD ||
				buffer_remaining <= FRAG_THRESHOLD)
			{
				node->send_bytes = send_remaining;
			}
			else
				node->send_bytes = buffer_remaining;
		}

		// Add one for average header size (only need a rough estimate)
		buffer_remaining -= node->send_bytes + 1;
	}

	// If we got here then all nodes were consumed

	// Report number of bytes used
	bandwidth -= available_bytes - buffer_remaining;
	return node;
}

static CAT_INLINE u32 GetACKIDOverhead(u32 ack_id, u32 remote_expected)
{
	const u32 ACK_ID_1_THRESH = 16; // Compression threshold for 1 byte ACK-ID
	const u32 ACK_ID_2_THRESH = 2048; // Compression threshold for 2 byte ACK-ID

	// Recalculate how many bytes it would take to represent ACK-ID
	u32 ack_id_overhead, diff = ack_id - remote_expected;

	if (diff < ACK_ID_1_THRESH)			ack_id_overhead = 1;
	else if (diff < ACK_ID_2_THRESH)	ack_id_overhead = 2;
	else								ack_id_overhead = 3;

	return ack_id_overhead;
}

CAT_INLINE void Transport::ClusterReliableAppend(u32 stream, u32 &ack_id, u8 *pkt, u32 &ack_id_overhead, u32 &frag_overhead, SendCluster &cluster, u8 sop, const u8 *copy_src, u32 copy_bytes, u16 frag_total_bytes)
{
	// Write header
	u32 data_bytes = copy_bytes + frag_overhead;
	u8 hdr = R_MASK | (sop << SOP_SHIFT);
	if (ack_id_overhead) hdr |= I_MASK;

	if (data_bytes <= BLO_MASK)
	{
		pkt[0] = (u8)data_bytes | hdr;
		++pkt;
		cluster.bytes--; // Turns out we can cut out a byte
	}
	else
	{
		pkt[0] = (u8)(data_bytes & BLO_MASK) | C_MASK | hdr;
		pkt[1] = (u8)(data_bytes >> BHI_SHIFT);
		pkt += 2;
	}

	// Write optional ACK-ID
	if (ack_id_overhead)
	{
		// ACK-ID compression
		if (ack_id_overhead == 1)
		{
			pkt[0] = (u8)(((ack_id & 31) << 2) | stream);
		}
		else if (ack_id_overhead == 2)
		{
			pkt[0] = (u8)((ack_id << 2) | 0x80 | stream);
			pkt[1] = (u8)((ack_id >> 5) & 0x7f);
		}
		else // if (ack_id_overhead == 3)
		{
			pkt[0] = (u8)((ack_id << 2) | 0x80 | stream);
			pkt[1] = (u8)((ack_id >> 5) | 0x80);
			pkt[2] = (u8)(ack_id >> 12);
		}

		pkt += ack_id_overhead;

		cluster.stream = stream;

		ack_id_overhead = 0; // Don't write ACK-ID next time around
	}

	cluster.ack_id = ++ack_id;

	// Write optional fragment header
	if (frag_overhead)
	{
		*(u16*)pkt = getLE16(frag_total_bytes);
		pkt += FRAG_HEADER_BYTES;

		frag_overhead = 0;
	}

	// Copy data bytes
	memcpy(pkt, copy_src, copy_bytes);
}

bool Transport::WriteSendQueueNode(OutgoingMessage *node, u32 now, u32 stream, s32 remaining)
{
	bool success = true;
	u32 max_payload_bytes = _max_payload_bytes;
	SendCluster cluster = _send_cluster;

	u32 ack_id = _next_send_id[stream];
	u32 remote_expected = _send_next_remote_expected[stream];
	u32 ack_id_overhead = 0;

	// If the next ACK-ID is too far ahead of the receiver,
	if (ack_id - remote_expected >= OUT_OF_ORDER_LIMIT)
	{
		CAT_WARN("Transport") << "Next ACK-ID is too far ahead of receiver for stream " << stream;
		return false;
	}

	// Calculate ack_id_overhead
	if (cluster.ack_id != ack_id || cluster.stream != stream)
		ack_id_overhead = GetACKIDOverhead(ack_id, remote_expected);

	// If node is already fragmented then we have sent some data before
	bool fragmented = node->sent_bytes > 0;

	// Grab the number of bytes to send from the QoS stuff above
	u32 bytes_to_send = node->send_bytes;
	u16 sent_bytes = node->sent_bytes;

	// For each fragment of the message,
	do
	{
		u32 remaining_send_buffer = max_payload_bytes - cluster.bytes;
		u32 frag_overhead = 0;

		// If message would be fragmented,
		if (MAX_MESSAGE_HEADER_BYTES + ack_id_overhead + bytes_to_send > remaining_send_buffer)
		{
			// If it is worth fragmentation,
			if (remaining_send_buffer >= FRAG_THRESHOLD)
			{
				if (!fragmented)
				{
					frag_overhead = FRAG_HEADER_BYTES;
					fragmented = true;
				}
			}
			else if (cluster.bytes > 0) // Not worth fragmentation, dump current send buffer
			{
				// If no more room remaining within bandwidth limit,
				remaining -= cluster.bytes;
				if (remaining <= FRAG_THRESHOLD)
				{
					success = false;
					break;
				}

				QueueWriteDatagram(cluster.front, cluster.bytes);
				cluster.Clear();

				remaining_send_buffer = max_payload_bytes;
				ack_id_overhead = GetACKIDOverhead(ack_id, remote_expected);

				if (!fragmented)
				{
					// If the message is still fragmented after emptying the send buffer,
					if (MAX_MESSAGE_HEADER_BYTES + ack_id_overhead + bytes_to_send > remaining_send_buffer)
					{
						frag_overhead = FRAG_HEADER_BYTES;
						fragmented = true;
					}
				}
			}
			else if (!fragmented)
			{
				frag_overhead = FRAG_HEADER_BYTES;
				fragmented = true;
			}
		} // end if message would be fragmented

		// Calculate total bytes to write to the send buffer on this pass
		u32 overhead = MAX_MESSAGE_HEADER_BYTES + ack_id_overhead + frag_overhead;
		u32 msg_bytes = overhead + bytes_to_send;
		u32 write_bytes = min(msg_bytes, remaining_send_buffer);

		// Limit size to allow ACK-ID decompression during retransmission
		u32 retransmit_limit = max_payload_bytes - (MAX_ACK_ID_BYTES - ack_id_overhead);
		if (write_bytes > retransmit_limit) write_bytes = retransmit_limit;

		u32 data_bytes_to_copy = write_bytes - overhead;
		OutgoingMessage *add_node = node;

		if (fragmented)
		{
			SendFrag *frag;
			do frag = m_std_allocator->AcquireObject<SendFrag>();
			while (!frag);

			// Fill fragment object
			frag->SetBytes(data_bytes_to_copy);
			frag->offset = sent_bytes;
			frag->full_data = node;
			frag->sop = SOP_FRAG;

			// Increment fragment count on the source node
			++node->frag_count;

			add_node = static_cast<OutgoingMessage*>( frag );
		}

		// Write common data
		add_node->id = ack_id;
		add_node->ts_firstsend = now;
		add_node->ts_lastsend = now;

		// If this node will represent loss,
		if (cluster.loss_on)
			add_node->loss_on = 0;
		else
		{
			add_node->loss_on = 1;
			cluster.loss_on = 1;
		}

		// Link to the end of the sent list
		_sent_list[stream].Append(add_node);

		// Grow the cluster
		u8 *msg = cluster.Grow(write_bytes);
		if (!msg)
		{
			ack_id_overhead = GetACKIDOverhead(ack_id, remote_expected);
			continue;
		}

		// Append to cluster
		ClusterReliableAppend(stream, ack_id, msg, ack_id_overhead, frag_overhead,
			cluster, node->sop, GetTrailingBytes(node) + sent_bytes,
			data_bytes_to_copy, node->GetBytes());

		sent_bytes += data_bytes_to_copy;
		bytes_to_send -= data_bytes_to_copy;

		CAT_FATAL("Transport") << "Wrote " << stream << ": bytes=" << data_bytes_to_copy << " ack_id=" << ack_id;

	} while (bytes_to_send > 0); // end while sending message fragments

	// If node is fragmented, remember number of bytes sent
	if (fragmented) node->sent_bytes = sent_bytes;

	// Update state
	_next_send_id[stream] = ack_id;
	_send_cluster = cluster;

	return success;
}

bool Transport::WriteSendHugeNode(SendHuge *node, u32 now, u32 stream, s32 remaining)
{
	bool complete = false;

	u32 max_payload_bytes = _max_payload_bytes;
	SendCluster cluster = _send_cluster;

	u32 ack_id = _next_send_id[stream];
	u32 remote_expected = _send_next_remote_expected[stream];
	u32 ack_id_overhead = 0;

	// If the next ACK-ID is too far ahead of the receiver,
	if (ack_id - remote_expected >= OUT_OF_ORDER_LIMIT)
	{
		CAT_WARN("Transport") << "Next ACK-ID is too far ahead of receiver (huge) for stream " << stream;
		return false;
	}

	// Calculate ack_id_overhead
	if (cluster.ack_id != ack_id || cluster.stream != stream)
		ack_id_overhead = GetACKIDOverhead(ack_id, remote_expected);

	// If this is the first time it is being sent, add fragment header
	u16 sent_bytes = node->sent_bytes;
	u32 frag_overhead = (sent_bytes == 0) ? FRAG_HEADER_BYTES : 0;

	// For each fragment of the message,
	u32 copy_bytes, total_copied_bytes = 0;
	s32 send_limit = node->send_bytes;
	bool read_success;
	do
	{
		// If there is not enough room to write anything,
		u32 remaining_send_buffer = max_payload_bytes - cluster.bytes;
		if (remaining_send_buffer < FRAG_THRESHOLD)
		{
			// If no more room remaining within bandwidth limit,
			remaining -= cluster.bytes;
			if (remaining <= FRAG_THRESHOLD)
			{
				break;
			}

			QueueWriteDatagram(cluster.front, cluster.bytes);
			cluster.Clear();

			ack_id_overhead = GetACKIDOverhead(ack_id, remote_expected);
			continue;
		}

		// Apply send limit
		u32 overhead = MAX_MESSAGE_HEADER_BYTES + ack_id_overhead + frag_overhead;
		copy_bytes = remaining_send_buffer - overhead;
		// Do not limit to bandwidth, just fill up to the next MTU
		//if (copy_bytes > send_limit) copy_bytes = send_limit;
		u32 total_bytes = overhead + copy_bytes;

		// Apply retransmit limit
		u32 retransmit_limit = max_payload_bytes - (MAX_ACK_ID_BYTES - ack_id_overhead);
		if (total_bytes > retransmit_limit) total_bytes = retransmit_limit;
		copy_bytes = total_bytes - overhead;

		// If this drops the number of copy bytes to zero, then abort here
		if (copy_bytes <= 0)
			break;

		// Acquire a fragment
		SendFrag *frag;
		do frag = m_std_allocator->AcquireTrailing<SendFrag>(copy_bytes);
		while (!frag);

		// Read data into fragment
		u32 copied = copy_bytes;
		read_success = node->source->Read((StreamMode)stream, GetTrailingBytes(frag), copied, this);

		// If data source has failed us,
		if (copied < copy_bytes)
		{
			// If no data could be copied,
			if (copied == 0)
			{
				// If end of file,
				if (!read_success)
				{
					// Allow this final transfer of zero bytes to indicate file completion
					send_limit = 0;
					complete = true;
				}
				else
				{
					// Abort transmit for now
					m_std_allocator->Release(frag);
					break;
				}
			}

			// Update the byte counts to reflect the number actually copied
			copy_bytes = copied;
			total_bytes = overhead + copied;
		}

		// Write common data
		frag->id = ack_id;
		frag->ts_firstsend = now;
		frag->ts_lastsend = now;
		frag->sop = SOP_FRAG;
		frag->SetBytes(copy_bytes);
		frag->full_data = node;
		frag->offset = sent_bytes;

		// If this node will represent loss,
		if (cluster.loss_on)
			frag->loss_on = 0;
		else
		{
			frag->loss_on = 1;
			cluster.loss_on = 1;
		}

		node->frag_count++;

		_sent_list[stream].Append(frag);

		// Grow the cluster
		u8 *pkt = cluster.Grow(total_bytes);
		if (!pkt)
		{
			// Update ack_id_overhead for a blank cluster
			u32 new_ack_id_overhead = GetACKIDOverhead(ack_id, remote_expected);
			overhead += new_ack_id_overhead - ack_id_overhead;
			total_bytes = overhead + copy_bytes;
			ack_id_overhead = new_ack_id_overhead;

			while (!(pkt = cluster.Grow(total_bytes)));
		}

		ClusterReliableAppend(stream, ack_id, pkt, ack_id_overhead,
			frag_overhead, cluster, SOP_FRAG, GetTrailingBytes(frag),
			copy_bytes, node->GetBytes());

		CAT_INFO("Transport") << "Wrote Huge " << stream << ": bytes=" << copy_bytes << " ack_id=" << ack_id;

		sent_bytes = 1;
		total_copied_bytes += copy_bytes;
		send_limit -= copy_bytes;

	} while (send_limit > 0);

	node->sent_bytes = sent_bytes;
	node->huge_remaining -= total_copied_bytes;

	// Update state
	_next_send_id[stream] = ack_id;
	_send_cluster = cluster;

	return complete;
}

void Transport::WriteQueuedReliable()
{
	// Avoid locking to transmit queued if no queued exist
	int stream;
	for (stream = 0; stream < NUM_STREAMS; ++stream)
		if (_send_queue[stream].head || _sending_queue[stream].head)
			break;
	if (stream >= NUM_STREAMS)
		return;

	// Use the same ts_firstsend for all messages delivered now, to insure they are clustered on retransmission
	u32 now = m_clock->msec();

	// Calculate bandwidth available for this transmission
	s32 bandwidth = _send_flow.GetRemainingBytes(now);

	// If there is no more room in the channel,
	if (bandwidth < 0) return;

	// Steal all work from each stream's send queue
	_send_queue_lock.Enter();
	for (u32 stream = 0; stream < NUM_STREAMS; ++stream)
		_sending_queue[stream].Steal(_send_queue[stream]);
	_send_queue_lock.Leave();

	// Generate a list of messages to transmit based on the bandwidth available
	OutgoingMessage *out_head[NUM_STREAMS], *out_tail[NUM_STREAMS];
	s32 remaining = bandwidth;

	// Split bandwidth evenly between normal streams
	for (u32 stream = 0; stream < NUM_STREAMS - 1; ++stream)
	{
		OutgoingMessage *node = remaining > 0 ? _sending_queue[stream].head : 0;
		out_head[stream] = node;

		if (!node)
		{
			out_tail[stream] = 0;
			continue;
		}

		// Reset the send bytes of the head node on the first pass since it may
		// have been retained from the previous timer tick
		node->send_bytes = 0;

		out_tail[stream] = DequeueBandwidth(node, remaining / (NUM_STREAMS - 1 - stream), remaining);
	}

	// All streams may claim remaining bandwidth with stream 0 as highest priority
	for (u32 stream = 0; remaining > 0 && stream < NUM_STREAMS - 1; ++stream)
	{
		OutgoingMessage *node = out_tail[stream];
		if (node) out_tail[stream] = DequeueBandwidth(node, remaining, remaining);
	}

	// If any bandwidth remains, give it to the bulk stream
	OutgoingMessage *node = remaining > 0 ? _sending_queue[STREAM_BULK].head : 0;
	out_head[STREAM_BULK] = node;
	if (!node)
		out_tail[STREAM_BULK] = 0;
	else
	{
		// Reset the send bytes of the head node on the first pass since it may
		// have been retained from the previous timer tick
		node->send_bytes = 0;

		out_tail[STREAM_BULK] = DequeueBandwidth(node, remaining, remaining);
	}

	// NOTE: What we have now is a *best guess* at how much data can fit into
	// the bandwidth allowed by the rate limiter.  Due to message headers, the
	// actual amount of data we can send is somewhat lower.
	// There may also be messages in the send cluster that greatly reduce the
	// amount of bandwidth remaining.
	remaining = bandwidth;

	// Write dequeued messages to the send cluster
	_send_cluster_lock.Enter();

	// Subtract off the number of bytes already queued up for sending
	remaining -= _outgoing_datagrams_bytes;

	// For each stream,
	for (u32 stream = 0; stream < NUM_STREAMS; ++stream)
	{
		OutgoingMessage *node = out_head[stream];
		if (!node) continue;

		// For each message to send,
		OutgoingMessage *next;
		do
		{
			// Cache next pointer since node may be relinked into sent list
			next = node->next;

			bool success = (node->GetBytes() == FRAG_HUGE) ?
				WriteSendHugeNode(static_cast<SendHuge*>( node ), now, stream, remaining) :
				WriteSendQueueNode(node, now, stream, remaining);

			// If node aborted early,
			if (!success)
				break;
			else
			{
				_sending_queue[stream].head = next;
				if (!next) _sending_queue[stream].tail = 0;
			}

		} while (node != out_tail[stream] && (node = next));
	}

	_send_cluster_lock.Leave();
}
