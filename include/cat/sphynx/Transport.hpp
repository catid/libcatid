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

#ifndef CAT_SPHYNX_TRANSPORT_HPP
#define CAT_SPHYNX_TRANSPORT_HPP

#include <cat/threads/Mutex.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>
#include <cat/parse/BufferStream.hpp>
#include <cat/time/Clock.hpp>
#include <cat/math/BitMath.hpp>
#include <cat/sphynx/FlowControl.hpp>
#include <cat/sphynx/SphynxLayer.hpp>

#define CAT_SEPARATE_ACK_LOCK /* Use a second mutex to serialize message acknowledgment data */

#if defined(CAT_SEPARATE_ACK_LOCK)
#define CAT_ACK_LOCK _ack_lock
#else
#define CAT_ACK_LOCK _big_lock
#endif

namespace cat {


namespace sphynx {


/*
	This transport layer provides fragmentation, three reliable ordered
	streams, one unordered reliable stream, and unreliable delivery.

	The Transport object that implements a sender/receiver in the protocol
	requires 276 bytes of memory per server connection, plus 32 bytes per
	message fragment in flight, plus buffers for queued send/recv packets, and
	it keeps two mutexes for thread-safety.  A lockless memory allocator is used
	to allocate all buffers except the fragmented message reassembly buffer.

	Packet format on top of UDP header:

		E { HDR(2 bytes)|ACK-ID(3 bytes)|DATA || ... || MAC(8 bytes) } || IV(3 bytes)

		E: ChaCha-12 stream cipher.
		IV: Initialization vector used by security layer (Randomly initialized).
		MAC: Message authentication code used by security layer (HMAC-MD5).

		HDR|ACK-ID|DATA: A message block inside the datagram.  The HDR and
		ACK-ID fields employ compression to use as little as 1 byte together.

	Each message follows the same format.  A two-byte header followed by data:

		--- Message Header  (16 bits) ---
		 0 1 2 3 4 5 6 7 8 9 a b c d e f
		<-- LSB ----------------- MSB -->
		| BLO |I|R|SOP|C|      BHI      |
		---------------------------------

		DATA_BYTES: BHI | BLO = Number of bytes in data part of message.
		I: 1=Followed by ACK-ID field. 0=ACK-ID is one higher than the last.
		R: 1=Reliable. 0=Unreliable.
		SOP: Super opcodes:
				0=Data (reliable or unreliable)
				1=Fragment (reliable)
				2=ACK (unreliable)
				3=Internal (any)
		C: 1=BHI byte is sent. 0=BHI byte is not sent and is assumed to be 0.

			When the I bit is set, the data part is preceded by an ACK-ID,
			which is then applied to all following reliable messages.
			This additional size is NOT accounted for in the DATA_BYTES field.

			When the FRAG opcode used for the first time in an ordered stream,
			the data part begins with a 16-bit Fragment Header.
			This additional size IS accounted for in the DATA_BYTES field.

			When DATA_BYTES is between 0 and 7, C can be set to 0 to avoid
			sending the BHI byte.

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
		<-- LSB ----------------- MSB -->
		|        TOTAL_BYTES(16)        |
		---------------------------------

		TOTAL_BYTES: Total bytes in data part of fragmented message,
					 not including this header.
*/

/*
	ACK message format:

	Header: I=0, R=0, SOP=SOP_ACK
	Data: AVGTRIP(2) || ROLLUP(3) || RANGE1 || RANGE2 || ... ROLLUP(3) || RANGE1 || RANGE2 || ...

	AVGTRIP:
		Average one-way transmit time based on the timestamps written at the end
		of the encrypted data within each outgoing datagram.  On receipt the
		timestamps are converted to local time and trip time is calculated.

		Trip time is accumulated between ACK responses and the average trip time
		is reported to the sender in the next ACK response.  The sender uses the
		trip time statistics to do congestion avoidance flow control.

	---- AVGTRIP Field (16 bits) ----
	 0 1 2 3 4 5 6 7 8 9 a b c d e f
	<-- LSB --------------- MSB ---->
	|     TLO     |C|      THI      |
	---------------------------------

	C: 0=Omit the high byte (field is just one byte), 1=Followed by high byte
	T: THI | TLO (15 bits)

	T is the average one-way transmit time since the last ACK.

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
	Bulk Transfer

	Writes bulk data to the end of outgoing packets to fill up to MTU size and
	writes at each tick of the transport layer if channel is underutilized.
*/

class Transport
{
public:
	static const u8 BLO_MASK = 7;
	static const u32 BHI_SHIFT = 3; 
	static const u8 I_MASK = 1 << 3;
	static const u8 R_MASK = 1 << 4;
	static const u8 C_MASK = 1 << 7;
	static const u32 SOP_SHIFT = 5;
	static const u32 SOP_MASK = 3;

	static const u32 NUM_STREAMS = 4; // Number of reliable streams
	static const u32 MIN_RTT = 2; // Minimum milliseconds for RTT

	static const int TIMEOUT_DISCONNECT = 15000; // milliseconds; NOTE: If this changes, the timestamp compression will stop working
	static const int TS_COMPRESS_FUTURE_TOLERANCE = 1000; // Milliseconds of time synch error before errors may occur in timestamp compression

	static const int INITIAL_RTT = 1500; // milliseconds
	static const int SILENCE_LIMIT = 4357; // Time silent before sending a keep-alive (0-length unordered reliable message), milliseconds

	static const int TICK_INTERVAL = 20; // Milliseconds between ticks

	static const u32 MINIMUM_MTU = 576; // Dial-up
	static const u32 MEDIUM_MTU = 1400; // Highspeed with unexpected overhead, maybe VPN
	static const u32 MAXIMUM_MTU = 1500; // Highspeed

	static const u32 IPV6_OPTIONS_BYTES = 40; // TODO: Not sure about this
	static const u32 IPV6_HEADER_BYTES = 40 + IPV6_OPTIONS_BYTES;

	static const u32 IPV4_OPTIONS_BYTES = 40;
	static const u32 IPV4_HEADER_BYTES = 20 + IPV4_OPTIONS_BYTES;

	static const u32 UDP_HEADER_BYTES = 8;

	static const u32 FRAG_MIN = 0;		// Min bytes for a fragmented message
	static const u32 FRAG_MAX = 65535;	// Max bytes for a fragmented message

	static const u32 FRAG_THRESHOLD = 32; // Fragment if FRAG_THRESHOLD bytes would be in each fragment

	static const u32 MAX_MESSAGE_DATALEN = 65535-1; // Maximum number of bytes in the data part of a message (-1 for the opcode)

	static const u32 TRANSPORT_OVERHEAD = 2; // Number of bytes added to each packet for the transport layer

protected:
	// Maximum transfer unit (MTU) in UDP payload bytes, excluding anything included in _overhead_bytes
	u32 _max_payload_bytes;

	// Overhead bytes: UDP/IP headers, encryption and transport overhead
	u32 _overhead_bytes;

public:
	void InitializePayloadBytes(bool ip6);
	bool InitializeTransportSecurity(bool is_initiator, AuthenticatedEncryption &auth_enc);

protected:
	// Receive state: Next expected ack id to receive
	u32 _next_recv_expected_id[NUM_STREAMS];

	// Receive state: Synchronization objects
	volatile bool _got_reliable[NUM_STREAMS];

#if defined(CAT_SEPARATE_ACK_LOCK)
	Mutex _ack_lock; // Just needs to protect writes OnDatagram() from messing up reads on tick
#endif // CAT_SEPARATE_ACK_LOCK

	// Receive state: Fragmentation
	RecvFrag _fragments[NUM_STREAMS]; // Fragments for each stream

	// Receive state: Receive queue head
	/*
		These are protected by the ack lock, which may be the same as the big
		lock if CAT_SEPARATE_ACK_LOCK is unset.

		OnDatagram() is serialized in Sphynx, so the locking is simplified.
		This thread is arbitrator for queue contents.
		So, when it is walking the queue it does not need to hold a lock.
		It calls RunQueue() and QueueRecv() that work on this queue.
		When making changes, it needs to hold the ack lock to synchronize
		with the timer thread.

		The transport timer is the only other thread to access the queue.
		It calls WriteACK(), which locks the queue and then walks it.
	*/
	RecvQueue *_recv_queue_head[NUM_STREAMS], *_recv_queue_tail[NUM_STREAMS];

	// Receive state: Statistics for flow control report in ACK response
	u32 _recv_trip_time_sum, _recv_trip_count;
	volatile u32 _recv_trip_time_avg; // Average trip time shared with timer thread

private:
	void RunQueue(SphynxTLS *tls, u32 recv_time, u32 ack_id, u32 stream);
	void QueueRecv(SphynxTLS *tls, u32 send_time, u32 recv_time, u8 *data, u32 bytes, u32 ack_id, u32 stream, u32 super_opcode);

protected:
	// Send state: Synchronization objects
	Mutex _big_lock;

	// Send state: Next ack id to use
	u32 _next_send_id[NUM_STREAMS];

	// Send statE: Flush after processing incoming data
	volatile bool _send_flush_after_processing;

	// Send state: Last rollup ack id from remote receiver
	u32 _send_next_remote_expected[NUM_STREAMS];

	// Send state: Combined writes
	u8 *_send_buffer;
	u32 _send_buffer_bytes;
	u32 _send_buffer_stream, _send_buffer_ack_id; // Used to compress ACK-ID by setting I=0 after the first reliable message

	// Send state: Flow control
	FlowControl _send_flow;

	// Send state: Queue of messages that are waiting to be sent
	SendQueue *_send_queue_head[NUM_STREAMS], *_send_queue_tail[NUM_STREAMS];

	// Send state: List of messages that are waiting to be acknowledged
	SendQueue *_sent_list_head[NUM_STREAMS], *_sent_list_tail[NUM_STREAMS];

protected:
	// true = no longer connected
	bool _disconnected;

	u32 _ts_delta; // Milliseconds clock difference between server and client: server_time = client_time + _ts_delta

private:
	void TransmitQueued();
	void Retransmit(u32 stream, SendQueue *node, u32 now); // Does not hold the send lock!
	void WriteACK();
	void OnACK(u32 send_time, u32 recv_time, u8 *data, u32 data_bytes);

public:
	Transport();
	virtual ~Transport();

public:
	// Message sending commands
	bool WriteUnreliableOOB(u8 msg_opcode, const void *msg_data = 0, u32 data_bytes = 0, SuperOpcode super_opcode = SOP_DATA);
	bool WriteUnreliable(u8 msg_opcode, const void *msg_data = 0, u32 data_bytes = 0, SuperOpcode super_opcode = SOP_DATA);
	bool WriteReliable(StreamMode, u8 msg_opcode, const void *msg_data = 0, u32 data_bytes = 0, SuperOpcode super_opcode = SOP_DATA);

	// Flush send buffer after processing the current message from the remote host
	CAT_INLINE void FlushAfter() { _send_flush_after_processing = true; }

	// Flush send buffer immediately, don't try to blob.
	// Try to use FlushAfter() unless you really see benefit from this!
	void FlushImmediately();

public:
	// Current local time
	CAT_INLINE u32 GetLocalTime() { return Clock::msec(); }

	// Convert from local time to server time
	CAT_INLINE u32 ToServerTime(u32 local_time) { return local_time + _ts_delta; }

	// Convert from server time to local time
	CAT_INLINE u32 FromServerTime(u32 server_time) { return server_time - _ts_delta; }

	// Current server time
	CAT_INLINE u32 GetServerTime() { return ToServerTime(GetLocalTime()); }

	// Compress timestamp on client for delivery to server; high two bits are unused; byte order must be fixed before writing to message
	CAT_INLINE u16 EncodeClientTimestamp(u32 local_time) { return (u16)(ToServerTime(local_time) & 0x3fff); }

	// Decompress a timestamp on server from client; high two bits are unused; byte order must be fixed before decoding
	CAT_INLINE u32 DecodeClientTimestamp(u32 local_time, u16 timestamp) { return BiasedReconstructCounter<14>(local_time, TS_COMPRESS_FUTURE_TOLERANCE, timestamp & 0x3fff); }

	// Compress timestamp on server for delivery to client; high two bits are unused; byte order must be fixed before writing to message
	CAT_INLINE u16 EncodeServerTimestamp(u32 local_time) { return (u16)(local_time & 0x3fff); }

	// Decompress a timestamp on client from server; high two bits are unused; byte order must be fixed before decoding
	CAT_INLINE u32 DecodeServerTimestamp(u32 local_time, u16 timestamp) { return FromServerTime(BiasedReconstructCounter<14>(ToServerTime(local_time), TS_COMPRESS_FUTURE_TOLERANCE, timestamp & 0x3fff)); }

	CAT_INLINE void TransportDisconnected() { _disconnected = true; }
	CAT_INLINE bool IsDisconnected() { return _disconnected; }

	void TickTransport(SphynxTLS *tls, u32 now);
	void OnDatagram(SphynxTLS *tls, u32 send_time, u32 recv_time, u8 *data, u32 bytes);

private:
	void OnFragment(SphynxTLS *tls, u32 send_time, u32 recv_time, u8 *data, u32 bytes, u32 stream);

	void PostPacketList(TempSendNode *packet_send_head);
	void PostSendBuffer();
	u32 RetransmitLost(u32 now); // Returns estimated number of lost packets (granularity is 1 ms)

protected:
	virtual bool PostPacket(u8 *data, u32 buf_bytes, u32 msg_bytes) = 0;
	virtual void OnMessage(SphynxTLS *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes) = 0; // precondition: bytes > 0
	virtual void OnInternal(SphynxTLS *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes) = 0; // precondition: bytes > 0

protected:
	bool PostMTUProbe(SphynxTLS *tls, u32 mtu);

	CAT_INLINE bool PostDisconnect(u8 reason) { return WriteUnreliableOOB(IOP_DISCO, &reason, 1, SOP_INTERNAL); }
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
