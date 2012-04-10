/*
	Copyright (c) 2009-2012 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_SPHYNX_TRANSPORT_HPP
#define CAT_SPHYNX_TRANSPORT_HPP

#include <cat/threads/Thread.hpp>
#include <cat/threads/Mutex.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>
#include <cat/parse/BufferStream.hpp>
#include <cat/time/Clock.hpp>
#include <cat/math/BitMath.hpp>
#include <cat/sphynx/FlowControl.hpp>

namespace cat {


namespace sphynx {


/*
	This transport layer provides fragmentation, two reliable ordered
	streams, one reliable ordered bulk stream, one unordered reliable stream,
	and unreliable delivery.

	Packet format on top of UDP header:

		E { HDR(2 bytes)|ACK-ID(3 bytes)|DATA || ... || MAC(8 bytes) } || IV(3 bytes)

		E: ChaCha stream cipher.
		IV: Initialization vector used by security layer (Randomly initialized).
		MAC: Message authentication code used by security layer (HMAC-MD5).

		HDR|ACK-ID|DATA: A message block inside the datagram.  The HDR and
		ACK-ID fields employ compression to use as little as 1 byte together.

		If CAT_TRANSPORT_RANDOMIZE_LENGTH is defined, no-op bytes are appended
		to each datagram plaintext based on an exponential distribution.  The
		purpose is to hide the true length of the datagrams to avoid filtering
		based on datagram length.

	Each message follows the same format.  A two-byte header followed by data:

		--- Message Header  (16 bits) ---
		 0 1 2 3 4 5 6 7 8 9 a b c d e f
		<-- LSB ----------------- MSB -->
		| BLO |I|R|SOP|C|      BHI      |
		---------------------------------

		DATA_BYTES: BHI | BLO = Number of bytes in data part of message
		I: 1=Followed by ACK-ID field. 0=ACK-ID is one higher than the last.
		R: 1=Reliable. 0=Unreliable.
		SOP: Super opcodes:
				0=Internal (any)
				1=Data (reliable or unreliable)
				2=Fragment (reliable)
				3=ACK (unreliable)
		C: 1=BHI byte is sent. 0=BHI byte is not sent and is assumed to be 0.

			There is an exceptional case:

				When an unreliable (R=0) message has I=1, then an ACK-ID
				does NOT follow the header.  Instead, the message length
				is replaced with the remaining payload bytes and the rest
				of the payload bytes are considered part of the message.

				This mode is useful to avoid sending BHI for OOB types,
				to reduce the overhead by 1 byte.

			Otherwise, when the I bit is set, the data part is preceded by an,
			ACK-ID which is then applied to all following reliable messages.
			This additional size is NOT accounted for in the DATA_BYTES field.

			When the FRAG opcode used for the first time in an ordered stream,
			the data part begins with a 16-bit Fragment Header.
			This additional size IS accounted for in the DATA_BYTES field.

			When DATA_BYTES is between 0 and 7, C can be set to 0 to avoid
			sending the BHI byte.

			When all bits are zero, it is considered a no-op.  The first nop
			will cause processing of a message to terminate early.

		------------- ACK-ID Field (24 bits) ------------
		 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 
		<-- LSB --------------------------------- MSB -->
		| S | IDA (5) |C|   IDB (7)   |C|  IDC (8)      |
		-------------------------------------------------

		C: 1=Continues to next byte.
		S: 0=Unordered stream, 1-3: Ordered streams.
		ID: IDC | IDB | IDA (20 bits)

		On retransmission, the ACK-ID field uses no compression
		since the receiver state cannot be determined.

		--- Fragment Header (16 bits) ---
		 0 1 2 3 4 5 6 7 8 9 a b c d e f
		<-- LSB -------------------------
		|        TOTAL_BYTES(16)        |
		---------------------------------

		TOTAL_BYTES: Total bytes in this and following data fragments.
			0 means that the overall message is Huge and should go
			through the OnPartialHuge() callback on the receiving side
			instead of being reassembled in the transport layer.

		As a result, normal message transmission is limited to messages
		that are up to 65535 bytes.  This includes the message type byte
		so the payload part of messages can be as long as 65534 bytes.
*/

/*
	ACK message format:

	Header: I=0, R=0, SOP=SOP_ACK
	Data: ROLLUP(3) || RANGE1 || RANGE2 || ... ROLLUP(3) || RANGE1 || RANGE2 || ...

	ROLLUP = Next expected ACK-ID.  Acknowledges every ID before this one.

	RANGE1:
		START || END

		START = First inclusive ACK-ID in a range to acknowledge.
		END = Final inclusive ACK-ID in a range to acknowledge.

	Negative acknowledgment can be inferred from the holes in the RANGEs.

	------------ ROLLUP Field (24 bits) -------------
	 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 
	<-- LSB --------------------------------- MSB -->
	|1| S | IDA (5) |    IDB (8)    |    IDC (8)    |
	-------------------------------------------------

	1: Indicates start of ROLLUP field.
	S: 0=Unordered stream, 1-3: Ordered streams.
	ID: IDC | IDB | IDA (21 bits)

	ROLLUP is always 3 bytes since we cannot tell how far ahead the remote host is now.

	--------- RANGE START Field (24 bits) -----------
	 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 
	<-- LSB --------------------------------- MSB -->
	|0|E| IDA (5) |C|   IDB (7)   |C|    IDC (8)    |
	-------------------------------------------------

	0: Indicates start of RANGE field.
	C: 1=Continues to next byte.
	E: 1=Has end field. 0=One ID in range.
	ID: IDC | IDB | IDA (20 bits) + last ack id in message

	---------- RANGE END Field (24 bits) ------------
	 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 
	<-- LSB --------------------------------- MSB -->
	|   IDA (7)   |C|   IDB (7)   |C|    IDC (8)    |
	-------------------------------------------------

	C: 1=Continues to next byte.
	ID: IDC | IDB | IDA (22 bits) + START.ID
*/

/*
	Thread Safety

	InitializePayloadBytes() and InitializeTransportSecurity() and other
	initialization functions are called from the same thread.

	TickTransport() and OnTransportDatagrams() are called from the same thread.

	Other interfaces to the transport layer may be called asynchronously from
	other threads.  For example, on the server another Connexion object in a
	different worker thread may react to a message arriving by retransmitting
	it via our transport object.

	All of the simple inline functions are thread-safe.

	The following functions need careful attention to avoid race conditions,
	since these functions may be accessed from outside of the worker thread:

	WriteOOB, WriteUnreliable, WriteReliable, FlushImmediately

	Locks should always be held for a minimal amount of time, never to include
	invocations of the IO layer functions that actually transmit data.  Ideally
	locks should also be held for a constant amount of time that does not grow
	with the number of items to be processed.
*/

/*
	Graceful Disconnection

	When the user calls Transport::Disconnect(), the _disconnect_reason is set
	to the given reason.  This is then used to send a few unreliable OOB
	IOP_DISCO messages including the provided reason.  After the transmits are
	finished, the Transport layer calls the OnDisconnectComplete() callback.

	It takes a few timer ticks to finish sending all the IOP_DISCO messages,
	say less than 100 milliseconds.

	Once a disconnect is requested, further data from the remote host will
	be silently ignored while IOP_DISCO messages are going out.  Timer tick
	events will also no longer happen.

	If the application needs to close fast, it can call RequestShutdown()
	directly on the derived object.  This option for shutdown is not graceful
	and will not transmit IOP_DISCO to the remote host(s).

	The remote host will get notified about a graceful disconnect when its
	OnDisconnectReason() callback is invoked.
*/


// A queue of messages to transmit
struct SendQueue
{
	OutgoingMessage *head, *tail;

	CAT_INLINE void FreeMemory();
	CAT_INLINE void Append(OutgoingMessage *node);
	CAT_INLINE void Steal(SendQueue &queue);
	CAT_INLINE void RemoveBefore(OutgoingMessage *node);
};


// A doubly-linked version of the above queue for the sent list
struct SentList : SendQueue
{
	CAT_INLINE void FreeMemory();
	CAT_INLINE void Append(OutgoingMessage *node);
	CAT_INLINE void RemoveBefore(OutgoingMessage *node);
	CAT_INLINE void RemoveBetween(OutgoingMessage *prev, OutgoingMessage *next);
};


// Receive state: Out of order wait queue
struct OutOfOrderQueue
{
	/*
		An alternative to a skip list is a huge preallocated
		circular buffer.  The memory space required for this
		is really prohibitive.  It would be 1 GB for 1k users
		for a window of 32k packets.  With this skip list
		approach I can achieve good average case efficiency
		with just 48 bytes overhead.

		If the circular buffer grows with demand, then it
		requires a lot of additional overhead for allocation.
		And the advantage over a skip list becomes less clear.

		In the worst case it may take longer to walk the list
		on insert and an attacker may be able to slow down the
		server by sending a lot of swiss cheese.  So I limit
		the number of loops allowed through the wait list to
		bound the processing time.
	*/
	RecvQueue *head;	// Head of skip list
	u32 size;			// Number of elements

	CAT_INLINE void FreeMemory();
};


// TLS container
struct TransportTLS : ITLS
{
	static const u32 DELIVERY_QUEUE_DEPTH = 128;

	// Called when TLS object is inserted into the TLS slot
	CAT_INLINE virtual bool OnInitialize() { return true; }

	// Called during thread termination
	CAT_INLINE virtual void OnFinalize() {}

	// Must override this
	static CAT_INLINE const char *GetNameString() { return "TransportTLS"; }

	// Used internally by the Sphynx Transport layer to queue up messages for delivery
	sphynx::IncomingMessage delivery_queue[DELIVERY_QUEUE_DEPTH];
	u32 delivery_queue_depth;
	u8 *free_list[DELIVERY_QUEUE_DEPTH];
	u32 free_list_count;
};


// The transport layer
class CAT_EXPORT Transport
{
	friend struct SendQueue;
	friend struct SentList;

	static const u8 SHUTDOWN_TICK_COUNT = 3; // Number of ticks before shutting down the object

	static const u8 BLO_MASK = 7;
	static const u32 BHI_SHIFT = 3; 
	static const u8 I_MASK = 1 << 3;
	static const u8 R_MASK = 1 << 4;
	static const u8 C_MASK = 1 << 7;
	static const u32 SOP_SHIFT = 5;
	static const u32 SOP_MASK = 3;
	static const u8 HDR_NOP = 0; // Official no-op header byte

	static const u32 MAX_ACK_ID_BYTES = 3;

	static const u32 MAX_MESSAGE_HEADER_BYTES = 2;

	static const u32 MIN_RTT = 2; // Minimum milliseconds for RTT
	static const int INITIAL_RTT = 1500; // milliseconds

	static const u32 IPV6_OPTIONS_BYTES = 40; // TODO: Not sure about this
	static const u32 IPV6_HEADER_BYTES = 40 + IPV6_OPTIONS_BYTES;

	static const u32 IPV4_OPTIONS_BYTES = 40;
	static const u32 IPV4_HEADER_BYTES = 20 + IPV4_OPTIONS_BYTES;

	static const u32 UDP_HEADER_BYTES = 8;

	static const u32 FRAG_THRESHOLD = 32; // Minimum fragment size; used elsewhere as a kind of "fuzz factor" for edges of packets
	static const u32 FRAG_HEADER_BYTES = 2;

	// This is 4 times larger than the encryption out of order limit to match max expectations
	static const u32 OUT_OF_ORDER_LIMIT = 4096; // Stop acknowledging out of order packets after caching this many
	static const u32 OUT_OF_ORDER_LOOPS = 32; // Max number of loops looking for the insertion point for out of order arrivals

	// Receive state: Next expected ack id to receive
	u32 _next_recv_expected_id[NUM_STREAMS];

	// Receive state: Synchronization objects
	bool _got_reliable[NUM_STREAMS];

	// Receive state: Fragmentation
	RecvFrag _fragments[NUM_STREAMS]; // Fragments for each stream

	// Receive state: Out of order packets waiting for an earlier ACK_ID to be processed
	OutOfOrderQueue _recv_wait[NUM_STREAMS];

	// Serializes writes to the send buffer
	Mutex _send_cluster_lock;

	// Serializes changes to the send queue for reliable messages going out
	Mutex _send_queue_lock;

	// Send state: Next ack id to use
	u32 _next_send_id[NUM_STREAMS];

	// Send state: Flush after processing incoming data
	volatile bool _send_flush_after_processing;

	// Send state: Last rollup ack id from remote receiver
	u32 _send_next_remote_expected[NUM_STREAMS];

	// Send state: Writes combined into a send cluster
	// Protected by _send_cluster_lock
	SendCluster _send_cluster;

	// Send state: Queue of messages that are waiting to be sent
	// Protected by _send_queue_lock
	SendQueue _send_queue[NUM_STREAMS];

	// Send state: Queue of messages that are being sent
	SendQueue _sending_queue[NUM_STREAMS];

	// Send state: List of messages that are waiting to be acknowledged
	SentList _sent_list[NUM_STREAMS];

	CAT_INLINE void RetransmitNegative(u32 recv_time, u32 stream, u32 last_ack_id, u32 &loss_count);
	static void FreeSentNode(OutgoingMessage *node);

	// Queue of outgoing datagrams for batched output
	// Protected by _send_cluster_lock
	BatchSet _outgoing_datagrams;
	u32 _outgoing_datagrams_count, _outgoing_datagrams_bytes;

	// Random padding state
	ChaChaOutput _rand_pad_csprng;
	u8 _rand_pad_source[64];
	u32 _rand_pad_index;

	bool InitializeRandPad(AuthenticatedEncryption &auth_enc);

#if defined(CAT_TRANSPORT_RANDOMIZE_LENGTH)
	bool RandPadDatagram(SendBuffer *&buffer, u32 &data_bytes);
#endif // CAT_TRANSPORT_RANDOMIZE_LENGTH

	CAT_INLINE void QueueWriteDatagram(u8 *data, u32 data_bytes)
	{
		SendBuffer *buffer = SendBuffer::Promote(data);

#if defined(CAT_TRANSPORT_RANDOMIZE_LENGTH)
		if (!RandPadDatagram(buffer, data_bytes))
			return;
#endif // CAT_TRANSPORT_RANDOMIZE_LENGTH

		buffer->SetBytes(data_bytes + SPHYNX_OVERHEAD);

		_outgoing_datagrams.PushBack(buffer);
		_outgoing_datagrams_count++;
		_outgoing_datagrams_bytes += data_bytes + SPHYNX_OVERHEAD;
	}

	// true = no longer connected
	u8 _disconnect_countdown; // When it hits zero, will called RequestShutdown() and close the socket
	u8 _disconnect_reason; // DISCO_CONNECTED = still connected

	u32 RetransmitLost(u32 now); // Returns estimated number of lost packets

	// Queue a fragment for freeing
	CAT_INLINE void QueueFragFree(TransportTLS *tls, u8 *data);

	// Queue received data for user processing
	void QueueDelivery(TransportTLS *tls, u32 stream, u8 *data, u32 data_bytes, bool huge_fragment);

	// Deliver messages to user in one big batch
	CAT_INLINE void DeliverQueued(TransportTLS *tls);

	void RunReliableReceiveQueue(TransportTLS *tls, u32 recv_time, u32 ack_id, u32 stream);
	void StoreReliableOutOfOrder(TransportTLS *tls, u32 recv_time, u8 *data, u32 bytes, u32 ack_id, u32 stream, u32 super_opcode);

	// Starting at a given node, walk the send queue forward until available bytes of bandwidth are expended
	// Returns the last node to send or 0 if no nodes remain
	static OutgoingMessage *DequeueBandwidth(OutgoingMessage *head, s32 available_bytes, s32 &used_bytes);

	static CAT_INLINE void ClusterReliableAppend(u32 stream, u32 &ack_id, u8 *pkt,
		u32 &ack_id_overhead, u32 &frag_overhead, SendCluster &cluster, u8 sop,
		const u8 *copy_src, u32 copy_bytes, u16 frag_total_bytes);

	// Write one SendQueue node into the send buffer
	bool WriteSendQueueNode(OutgoingMessage *node, u32 now, u32 stream, s32 remaining);

	void WriteQueuedReliable();
	void Retransmit(u32 stream, OutgoingMessage *node, u32 now); // Does not hold the send lock!
	void WriteACK();
	void OnACK(u32 recv_time, u8 *data, u32 data_bytes);
	void OnFragment(TransportTLS *tls, u32 recv_time, u8 *data, u32 bytes, u32 stream);

public:
	Transport();
	virtual ~Transport();

	void InitializePayloadBytes(bool ip6);
	bool InitializeTransportSecurity(bool is_initiator, AuthenticatedEncryption &auth_enc);

	// Copy data directly to the send buffer, no need to acquire an OutgoingMessage
	bool WriteOOB(u8 msg_opcode, const void *msg_data = 0, u32 msg_bytes = 0, SuperOpcode super_opcode = SOP_DATA);
	bool WriteUnreliable(u8 msg_opcode, const void *msg_data = 0, u32 msg_bytes = 0, SuperOpcode super_opcode = SOP_DATA);
	bool WriteReliable(StreamMode stream, u8 msg_opcode, const void *msg_data = 0, u32 msg_bytes = 0, SuperOpcode super_opcode = SOP_DATA);

	// Queue up a reliable message for delivery without copy overhead
	// msg: Allocate with OutgoingMessage::Acquire(msg_bytes)
	// msg_bytes: Includes message opcode byte at offset 0
	bool WriteReliableZeroCopy(StreamMode stream, u8 *msg, u32 msg_bytes, SuperOpcode super_opcode = SOP_DATA);

	// Queue up a huge data transfer
	bool WriteHuge(StreamMode stream, IHugeSource *source);

	// Flush send buffer after processing the current message from the remote host
	CAT_INLINE void FlushAfter() { _send_flush_after_processing = true; }

	// Flush send buffer immediately, don't try to blob.
	// Try to use FlushAfter() unless you really see benefit from this!
	void FlushWrites();

	void Disconnect(u8 reason = DISCO_USER_EXIT);
	CAT_INLINE bool IsDisconnected() { return _disconnect_reason != DISCO_CONNECTED; }
	CAT_INLINE bool WriteDisconnect(u8 reason) { return WriteOOB(IOP_DISCO, &reason, 1, SOP_INTERNAL); }

	void TickTransport(u32 now);
	void OnTransportDatagrams(ThreadLocalStorage &tls, const BatchSet &delivery);

protected:
	// Maximum transfer unit (MTU) in UDP payload bytes, excluding _udpip_bytes
	u32 _max_payload_bytes;

	// Overhead bytes: UDP/IP headers
	u32 _udpip_bytes;

	// Send state: Flow control
	FlowControl _send_flow;

	// Huge source of upstream/downstream data
	IHugeSource *_huge_source;

	CAT_INLINE u8 GetDisconnectReason() { return _disconnect_reason; }

	virtual void OnDisconnectComplete() = 0;

	virtual bool WriteDatagrams(const BatchSet &buffers, u32 count) = 0;

	CAT_INLINE bool WriteDatagram(u8 *single, u32 data_bytes)
	{
		SendBuffer *buffer = SendBuffer::Promote(single);

#if defined(CAT_TRANSPORT_RANDOMIZE_LENGTH)
		if (!RandPadDatagram(buffer, data_bytes))
			return false;
#endif // CAT_TRANSPORT_RANDOMIZE_LENGTH

		buffer->SetBytes(data_bytes + SPHYNX_OVERHEAD);
		return WriteDatagrams(buffer, 1);
	}

	virtual void OnMessages(IncomingMessage msgs[], u32 count) = 0;
	virtual void OnInternal(u32 recv_time, BufferStream msg, u32 bytes) = 0; // precondition: bytes > 0
	virtual void OnDisconnectReason(u8 reason) = 0; // Called to help explain why a disconnect is happening

	bool PostMTUProbe(u32 mtu);

	void OnFlowControlWrite(u32 bytes);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
