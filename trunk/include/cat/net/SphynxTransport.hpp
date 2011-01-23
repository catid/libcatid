/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

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

#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/threads/Mutex.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>
#include <cat/parse/BufferStream.hpp>
#include <cat/time/Clock.hpp>
#include <cat/math/BitMath.hpp>

// TODO: do something with the extra two bits in the new timestamp field
// TODO: congestion control
// TODO: vulnerable to resource starvation attacks (out of sequence packets, etc)
// TODO: move all packet sending outside of locks

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
	How Rate Limiting Works

	Going to do rate limiting a little differently:

	A count of bytes recently sent is kept in a volatile 32-bit number.

	Whenever a PostPacket() call succeeds, it will increase the count of bytes
	by the packet size + estimated IP4/6 and UDP header sizes.  This addition is
	done atomically so no locking needs to be done.

	A thread will wake up every 20 ms, and if it has been 500 ms since the last
	time it has done so, and the count of bytes is positive, it will subtract
	off the rate limit over two (10KBPS = -5,000).  This is done atomically
	also, and may make the number go negative.  The negative allows the data
	flow to "burst" up and maintain the same rate if the rate of data flow is
	not constant.  I think this is good for games, since events are bursty.

	I chose 500 ms with a goal in mind.  For a data rate of about 10 KBPS, that
	would reduce by 5k every 500 ms, which allows for a burst of about 4 MSS
	sized messages to be transmitted.  That would allow 4 acks to be combined
	into one after the burst, which is about my target for combining acks.
	
	It just feels like the right amount, but I bet there would be a way to prove
	the "correct" number is somewhere around this value.

	Not all data is rate limited.  The following are not rate limited:
	Internal messages (out of band)
	Unreliable messages (probably contains data that cannot be delayed)

	The following ARE rate limited:
	Unordered, stream 1, 2 and 3 (these always contain less important data)

	Prioritization: If data are to be held off for the next epoch, then it will
	send across unordered always before stream 1 always before stream 2 always
	before stream 3.
	
	There is no fairness algorithm so if stream 1 is very noisy, you may never
	hear what is in stream 3.
*/

class Connexion;
class Map;
class Server;
class ServerWorker;
class ServerTimer;
class Client;
class Transport;

#define CAT_SEPARATE_ACK_LOCK /* Use a second mutex to serialize message acknowledgment data */

#if defined(CAT_SEPARATE_ACK_LOCK)
#define CAT_ACK_LOCK _ack_lock
#else
#define CAT_ACK_LOCK _big_lock
#endif

#if defined(CAT_WORD_32)
#define CAT_PACK_TRANSPORT_STATE_STRUCTURES /* For 32-bit version, this allows fragments to fit in 32 bytes */
#else // 64-bit version:
//#define CAT_PACK_TRANSPORT_STATE_STRUCTURES /* No advantage for 64-bit version */
#endif

// Protocol constants
static const u32 PROTOCOL_MAGIC = 0xC47D0001;
static const int PUBLIC_KEY_BYTES = 64;
static const int PRIVATE_KEY_BYTES = 32;
static const int CHALLENGE_BYTES = PUBLIC_KEY_BYTES;
static const int ANSWER_BYTES = PUBLIC_KEY_BYTES*2;
static const int HASH_TABLE_SIZE = 32768; // Power-of-2
static const int HASH_TABLE_MASK = HASH_TABLE_SIZE - 1;
static const int MAX_POPULATION = HASH_TABLE_SIZE / 2;
static const int CONNECTION_FLOOD_THRESHOLD = 10;

// (multiplier-1) divisible by all prime factors of table size
// (multiplier-1) is a multiple of 4 if table size is a multiple of 4
// These constants are from Press, Teukolsky, Vetterling and Flannery's
// "Numerical Recipes in FORTRAN: The Art of Scientific Computing"
static const u32 COLLISION_MULTIPLIER = 71*5861 * 4 + 1;
static const u32 COLLISION_INCREMENTER = 1013904223;

// If multiplier changes, this needs to be recalculated (multiplicative inverse of above)
static const u32 COLLISION_MULTINVERSE = 4276115653;
static const u32 COLLISION_INCRINVERSE = 0 - COLLISION_INCREMENTER;

// Handshake types
enum HandshakeType
{
	C2S_HELLO = 85,		// c2s 55 (magic[4]) (server public key[64])
	S2C_COOKIE = 24,	// s2c 18 (cookie[4])
	C2S_CHALLENGE = 9,	// c2s 09 (magic[4]) (cookie[4]) (challenge[64])
	S2C_ANSWER = 108,	// s2c 6c (data port[2]) (answer[128])
	S2C_ERROR = 162		// s2c a2 (error code[1])
};

// Handshake type lengths
static const u32 C2S_HELLO_LEN = 1 + 4 + PUBLIC_KEY_BYTES;
static const u32 S2C_COOKIE_LEN = 1 + 4;
static const u32 C2S_CHALLENGE_LEN = 1 + 4 + 4 + CHALLENGE_BYTES;
static const u32 S2C_ANSWER_LEN = 1 + sizeof(Port) + ANSWER_BYTES;
static const u32 S2C_ERROR_LEN = 1 + 1;

// Handshake errors
enum HandshakeError
{
	ERR_CLIENT_OUT_OF_MEMORY,
	ERR_CLIENT_BROKEN_PIPE,
	ERR_CLIENT_TIMEOUT,
	ERR_CLIENT_ICMP,
	ERR_NUM_CLIENT_ERRORS,

	ERR_WRONG_KEY = 0x7f,
	ERR_SERVER_FULL = 0xa6,
	ERR_FLOOD_DETECTED = 0x40,
	ERR_TAMPERING = 0xcc,
	ERR_SERVER_ERROR = 0x1f
};

// Convert handshake error string to user-readable error message
const char *GetHandshakeErrorString(HandshakeError err);

// Disconnect reasons
enum DisconnectReasons
{
	DISCO_TIMEOUT = 0xff,		// Remote host has not received data from us
	DISCO_TAMPERING = 0xfe,		// Remote host received a tampered packet
	DISCO_BROKEN_PIPE = 0xfd,	// Our socket got closed
	DISCO_USER_EXIT = 0xfc		// User closed the remote application

	// Feel free to define your own disconnect reasons.  Here is probably not the best place.
};

// Stream modes
enum StreamMode
{
	STREAM_UNORDERED = 0,	// Reliable, unordered stream 0
	STREAM_1 = 1,			// Reliable, ordered stream 1 (highest priority)
	STREAM_2 = 2,			// Reliable, ordered stream 2
	STREAM_3 = 3			// Reliable, ordered stream 3 (lowest priority)
};

// Super opcodes
enum SuperOpcode
{
	SOP_DATA,		// 0=Data (reliable or unreliable)
	SOP_FRAG,		// 1=Fragment (reliable)
	SOP_ACK,		// 2=ACK (unreliable)
	SOP_INTERNAL,	// 3=Internal
};

static const u32 TRANSPORT_HEADER_BYTES = 2; // Or 1 if data bytes < 8

// Internal opcodes
enum InternalOpcode
{
	IOP_C2S_MTU_PROBE = 187,	// c2s bb (random padding[MTU]) Large MTU test message
	IOP_S2C_MTU_SET = 244,		// s2c f4 (mtu[2]) MTU set message

	IOP_C2S_TIME_PING = 17,		// c2s 11 (client timestamp[4]) Time synchronization ping
	IOP_S2C_TIME_PONG = 138,	// c2s 8a (client timestamp[4]) (server timestamp[4]) Time synchronization pong

	IOP_DISCO = 84				// c2s 54 (reason[1]) Disconnection notification
};

// Internal opcode lengths
static const u32 IOP_C2S_MTU_TEST_MINLEN = 1 + 200;
static const u32 IOP_S2C_MTU_SET_LEN = 1 + 2;
static const u32 IOP_C2S_TIME_PING_LEN = 1 + 4;
static const u32 IOP_S2C_TIME_PONG_LEN = 1 + 4 + 4;
static const u32 IOP_DISCO_LEN = 1 + 1;


//// sphynx::Transport

#if defined(CAT_PACK_TRANSPORT_STATE_STRUCTURES)
# pragma pack(push)
# pragma pack(1)
#endif

// Receive state: Fragmentation
struct RecvFrag
{
	u8 *buffer; // Buffer for accumulating fragment
	u32 length; // Number of bytes in fragment buffer
	u32 offset; // Current write offset in buffer
	u32 send_time; // Timestamp on first fragment piece
};

// Receive state: Receive queue
struct RecvQueue
{
	RecvQueue *next;	// Next in queue
	RecvQueue *prev;	// Previous in queue
	u32 id;				// Acknowledgment id
	u16 sop;			// Super Opcode
	u16 bytes;			// Data Bytes
	u32 send_time;		// Timestamp attached to packet

	// Message contents follow
};

// Send state: Send queue
struct SendQueue
{
	SendQueue *next;	// Next in queue
	SendQueue *prev;	// Previous in queue
	u32 ts_firstsend;	// Millisecond-resolution timestamp when it was first sent
	u32 ts_lastsend;	// Millisecond-resolution timestamp when it was last sent
	union
	{
		u32 sent_bytes;	// In send queue: Number of sent bytes while fragmenting a large message
		u32 id;			// In sent list: Acknowledgment id
	};
	u16 bytes;			// Data bytes
	u16 frag_count;		// Number of fragments remaining to be delivered
	u8 sop;				// Super opcode of message

	// Message contents follow
};

struct SendFrag : public SendQueue
{
	SendQueue *full_data;	// Object containing message data
	u16 offset;				// Fragment data offset
};

// Temporary send node structure, nestled in the encryption overhead of outgoing packets
struct TempSendNode // Size <= 11 bytes = AuthenticatedEncryption::OVERHEAD_BYTES
{
	static const u32 BYTE_MASK = 0xffff;

	TempSendNode *next;
	u16 negative_offset; // Number of bytes before this structure, and single flag
};

#if defined(CAT_PACK_TRANSPORT_STATE_STRUCTURES)
# pragma pack(pop)
#endif

class Transport
{
protected:
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

	static const int EPOCH_INTERVAL = 500; // Milliseconds per epoch

	static const u32 MINIMUM_MTU = 576; // Dial-up
	static const u32 MEDIUM_MTU = 1400; // Highspeed with unexpected overhead, maybe VPN
	static const u32 MAXIMUM_MTU = 1500; // Highspeed

	static const u32 IPV6_OPTIONS_BYTES = 40; // TODO: Not sure about this
	static const u32 IPV6_HEADER_BYTES = 40 + IPV6_OPTIONS_BYTES;

	static const u32 IPV4_OPTIONS_BYTES = 40;
	static const u32 IPV4_HEADER_BYTES = 20 + IPV4_OPTIONS_BYTES;

	static const u32 UDP_HEADER_BYTES = 8;

	static const u32 FRAG_THRESHOLD = 32; // Fragment if FRAG_THRESHOLD bytes would be in each fragment

	static const u32 MAX_MESSAGE_DATALEN = 65535-1; // Maximum number of bytes in the data part of a message (-1 for the opcode)

	static const u32 MIN_RATE_LIMIT = 100000; // Smallest data rate allowed

public:
	static const u32 TRANSPORT_OVERHEAD = 2; // Number of bytes added to each packet for the transport layer

protected:
	// Maximum transfer unit (MTU) in UDP payload bytes, excluding the IP and UDP headers and encryption overhead
	u32 _max_payload_bytes;

	// Overhead bytes
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

	static const u32 FRAG_MIN = 0;		// Min bytes for a fragmented message
	static const u32 FRAG_MAX = 65535;	// Max bytes for a fragmented message

	// Receive state: Receive queue head
	RecvQueue *_recv_queue_head[NUM_STREAMS], *_recv_queue_tail[NUM_STREAMS];

private:
	void RunQueue(ThreadPoolLocalStorage *tls, u32 recv_time, u32 ack_id, u32 stream);
	void QueueRecv(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, u8 *data, u32 bytes, u32 ack_id, u32 stream, u32 super_opcode);

protected:
	// Send state: Synchronization objects
	Mutex _big_lock;

	// Send state: Next ack id to use
	u32 _next_send_id[NUM_STREAMS];

	// Send state: Estimated round-trip time
	u32 _rtt; // milliseconds

	// Send state: Last rollup ack id from remote receiver
	u32 _send_next_remote_expected[NUM_STREAMS];

	// Send state: Combined writes
	u8 *_send_buffer;
	u32 _send_buffer_bytes;
	u32 _send_buffer_stream, _send_buffer_ack_id; // Used to compress ACK-ID by setting I=0 after the first reliable message
	u32 _send_buffer_msg_count; // Used to compress datagrams with a single message by omitting the header's BLO field

	// Send state: Number of bytes sent during the current epoch, atomically synchronized
	volatile u32 _send_epoch_bytes;

	// Queue of messages that are waiting to be sent
	SendQueue *_send_queue_head[NUM_STREAMS], *_send_queue_tail[NUM_STREAMS];

	// List of messages that are waiting to be acknowledged
	SendQueue *_sent_list_head[NUM_STREAMS], *_sent_list_tail[NUM_STREAMS];

protected:
	bool _disconnected; // true = no longer connected

	s32 _max_epoch_bytes; // Bytes per epoch maximum (0 = none)
	u32 _next_epoch_time; // Time when next epoch will start

	u32 _loss_timeout; // Milliseconds without receiving acknowledgment that a message will be considered lost

	// TODO: Does not need to be on the server side, can we find a way to move it out of this base class?
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
	bool WriteUnreliableOOB(u8 msg_opcode, const void *msg_data = 0, u32 data_bytes = 0, SuperOpcode super_opcode = SOP_DATA);
	bool WriteUnreliable(u8 msg_opcode, const void *msg_data = 0, u32 data_bytes = 0, SuperOpcode super_opcode = SOP_DATA);
	bool WriteReliable(StreamMode, u8 msg_opcode, const void *msg_data = 0, u32 data_bytes = 0, SuperOpcode super_opcode = SOP_DATA);
	void FlushWrite();

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

protected:
	// Notify transport layer of disconnect to halt message processing
	CAT_INLINE void TransportDisconnected() { _disconnected = true; }
	CAT_INLINE bool IsDisconnected() { return _disconnected; }

	void TickTransport(ThreadPoolLocalStorage *tls, u32 now);
	void OnDatagram(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, u8 *data, u32 bytes);

private:
	void OnFragment(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, u8 *data, u32 bytes, u32 stream);

	void PostPacketList(TempSendNode *packet_send_head);
	void PostSendBuffer();

protected:
	virtual bool PostPacket(u8 *data, u32 buf_bytes, u32 msg_bytes) = 0;
	virtual void OnMessage(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes) = 0; // precondition: bytes > 0
	virtual void OnInternal(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes) = 0; // precondition: bytes > 0

protected:
	bool PostMTUProbe(ThreadPoolLocalStorage *tls, u32 mtu);

	CAT_INLINE bool PostDisconnect(u8 reason) { return WriteUnreliable(IOP_DISCO, &reason, 1, SOP_INTERNAL); FlushWrite(); }
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
