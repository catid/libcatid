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

#ifndef CAT_SPHYNX_TRANSPORT_HPP
#define CAT_SPHYNX_TRANSPORT_HPP

#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/threads/Mutex.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>

namespace cat {


namespace sphynx {


/*
	Packet format on top of UDP header:

		E { HDR(2 bytes)|DATA || HDR(2 bytes)|DATA || ... || MAC(8 bytes) } || IV(3 bytes)

		E: ChaCha-12 stream cipher.
		IV: Initialization vector used by security layer (Randomly initialized).
		MAC: Message authentication code used by security layer (HMAC-MD5).

		HDR|DATA: A message block inside the datagram.

	Each message follows the same format.  A two-byte header followed by data:

		--- Message Header  (16 bits) ---
		 0 1 2 3 4 5 6 7 8 9 a b c d e f
		<-- LSB ----------------- MSB -->
		|   DATA_BYTES(11)    |I|R| SOP |
		---------------------------------

		DATA_BYTES: Number of bytes in data part of message.
		I: 1=Followed by ACK-ID field.
		R: 1=Reliable. 0=Unreliable.
		SOP: Super opcodes:
			0=Data (reliable or unreliable)
			1=Fragment (reliable)
			2=ACK (unreliable)
			3=MTU Probe (unreliable)
			4=MTU Set (unordered reliable)
			5=Time Ping (unreliable)
			6=Time Pong (unreliable)
			7=Disconnect (unreliable)

			When the I bit is set, the data part is preceded by an ACK-ID,
			which is then applied to all following reliable messages.
			This additional size is NOT accounted for in the DATA_BYTES field.

			When the FRAG opcode used for the first time in an ordered stream,
			the data part begins with a 16-bit Fragment Header.
			This additional size IS accounted for in the DATA_BYTES field.

		------------------- ACK-ID Field (32 bits) ----------------------
		 0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 8 9 a b c d e f
		<-- LSB ------------------------------------------------- MSB -->
		| S | IDA (5) |C|   IDB (7)   |C|  IDC (7)    |C|    IDD (8)    |
		-----------------------------------------------------------------

		C: Continues to next byte
		S: 0=Unordered stream, 1-3: Ordered streams.
		ID: IDD | IDC | IDB | IDA

		--- Fragment Header (16 bits) ---
		 0 1 2 3 4 5 6 7 8 9 a b c d e f
		<-- LSB ----------------- MSB -->
		|        TOTAL_BYTES(16)        |
		---------------------------------

		TOTAL_BYTES: Total bytes in data part of fragmented message, not including this header.
*/

/*
	ACK message format:

	Header: I=0, R=0, T=2
	Data: ROLLUP(3) || RANGE1 || RANGE2 || ...

	ROLLUP = Next expected ACK-ID.  Acknowledges every ID before this one.

	RANGE1:
		START(3) || END(3)

		START = First inclusive ACK-ID in a range to acknowledge.
		END = Final inclusive ACK-ID in a range to acknowledge.

	Negative acknowledgment can be inferred from the holes in the RANGEs.
*/


class Connection;
class Map;
class Server;
class ServerWorker;
class ServerTimer;
class Client;
class Transport;

// Protocol constants
static const u32 PROTOCOL_MAGIC = 0xC47D0001;
static const int PUBLIC_KEY_BYTES = 64;
static const int PRIVATE_KEY_BYTES = 32;
static const int CHALLENGE_BYTES = PUBLIC_KEY_BYTES;
static const int ANSWER_BYTES = PUBLIC_KEY_BYTES*2;
static const int HASH_TABLE_SIZE = 32768; // Power-of-2
static const int MAX_POPULATION = HASH_TABLE_SIZE / 2;

// (multiplier-1) divisible by all prime factors of table size
// (multiplier-1) is a multiple of 4 if table size is a multiple of 4
// These constants are from Press, Teukolsky, Vetterling and Flannery's
// "Numerical Recipes in FORTRAN: The Art of Scientific Computing"
static const int COLLISION_MULTIPLIER = 71*5861 * 4 + 1;
static const int COLLISION_INCREMENTER = 1013904223;


// Handshake types
enum HandshakeType
{
	C2S_HELLO,
	S2C_COOKIE,
	C2S_CHALLENGE,
	S2C_ANSWER,
	S2C_ERROR
};

// Handshake errors
enum HandshakeError
{
	ERR_SERVER_FULL
};

// Stream modes
enum StreamMode
{
	STREAM_UNORDERED = 0,	// Reliable, unordered stream 0
	STREAM_1 = 1,			// Reliable, ordered stream 1
	STREAM_2 = 2,			// Reliable, ordered stream 2
	STREAM_3 = 3			// Reliable, ordered stream 3
};

// Super Opcodes
enum SuperOpcode
{
	SOP_DATA,			// 0=Data (reliable or unreliable)
	SOP_FRAG,			// 1=Fragment (reliable)
	SOP_ACK,			// 2=ACK (unreliable)
	SOP_MTU_PROBE,		// 3=MTU Probe (unreliable)
	SOP_MTU_SET,		// 4=MTU Set (unordered reliable)
	SOP_TIME_PING,		// 5=Time Ping (unreliable)
	SOP_TIME_PONG,		// 6=Time Pong (unreliable)
	SOP_DISCO,			// 7=Disconnect (unreliable)
};


//// sphynx::Transport

class Transport
{
protected:
	static const u16 DATALEN_MASK = 0x7ff;
	static const u16 I_MASK = 1 << 11;
	static const u16 R_MASK = 1 << 12;
	static const u16 SOP_MASK = 7 << 13;
	static const u16 SOP_SHIFT = 13;

	static const u32 NUM_STREAMS = 4; // Number of reliable streams

	static const u32 TIMEOUT_DISCONNECT = 15000; // 15 seconds

	static const u32 MINIMUM_MTU = 576; // Dialup
	static const u32 MEDIUM_MTU = 1400; // Highspeed with unexpected overhead, maybe VPN
	static const u32 MAXIMUM_MTU = 1500; // Highspeed

	static const u32 IPV6_OPTIONS_BYTES = 40; // TODO: Not sure about this
	static const u32 IPV6_HEADER_BYTES = 40 + IPV6_OPTIONS_BYTES;

	static const u32 IPV4_OPTIONS_BYTES = 40;
	static const u32 IPV4_HEADER_BYTES = 20 + IPV4_OPTIONS_BYTES;

	static const u32 UDP_HEADER_BYTES = 8;

	static const u32 FRAG_THRESHOLD = 32; // Fragment if FRAG_THRESHOLD bytes would be in each fragment

	static const u32 MAX_MESSAGE_DATALEN = 65535; // Maximum number of bytes in the data part of a message

	// Maximum transfer unit (MTU) in UDP payload bytes, excluding the IP and UDP headers and encryption overhead
	u32 _max_payload_bytes;
	u32 _rtt;

public:
	void InitializePayloadBytes(bool ip6);

protected:
	// Receive state: Next expected ack id to receive
	u32 _next_recv_expected_id[NUM_STREAMS];

	// Receive state: Synchronization objects
	volatile bool _got_reliable[NUM_STREAMS];
	Mutex _recv_lock; // Just needs to protect writes OnDatagram() from messing up reads on tick

	// Receive state: Fragmentation
	u8 *_fragment_buffer[NUM_STREAMS]; // Buffer for accumulating fragment
	u32 _fragment_offset[NUM_STREAMS]; // Current write offset in buffer
	u32 _fragment_length[NUM_STREAMS]; // Number of bytes in fragment buffer

	static const u32 FRAG_MIN = 0;		// Min bytes for a fragmented message
	static const u32 FRAG_MAX = 65535;	// Max bytes for a fragmented message

	// Receive state: Receive queue
	struct RecvQueue
	{
		static const u32 FRAG_FLAG = 0x80000000;
		static const u32 BYTE_MASK = 0x7fffffff;

		RecvQueue *prev;	// Previous in queue
		RecvQueue *next;	// Next in queue
		u32 id;				// Acknowledgment id
		u32 bytes;			// High bit: Fragment?

		// Message contents follow
	};

	// Receive state: Receive queue head
	RecvQueue *_recv_queue_head[NUM_STREAMS], *_recv_queue_tail[NUM_STREAMS];

private:
	void RunQueue(u32 ack_id, u32 stream);
	void QueueRecv(u8 *data, u32 bytes, u32 ack_id, u32 stream, u32 super_opcode);

protected:
	// Send state: Synchronization objects
	volatile u32 _writer_count;
	Mutex _send_lock;

	// Send state: Next ack id to use
	u32 _next_send_id[NUM_STREAMS];

	// Send state: Combined writes
	u8 *_send_buffer;
	u32 _send_buffer_bytes;

	// Send state: Send queue
	struct SendQueue
	{
		SendQueue *prev; // Previous in queue
		SendQueue *next; // Next in queue
		u32 id; // Acknowledgment id, or number of sent bytes while fragmenting a large message
		u32 bytes; // Data bytes
		u32 offset; // Fragment data offset: If offset < bytes, then it is a SendFrag object
		u32 ts_firstsend; // Timestamp when it was sent
		u32 ts_lastsend; // Timestamp when it was last sent

		// Message contents follow
	};

	struct SendFrag : public SendQueue
	{
		SendQueue *full_data;
	};

	// Queue of messages that are waiting to be sent
	SendQueue *_send_queue_head[NUM_STREAMS], *_send_queue_tail[NUM_STREAMS];

	// List of messages that are waiting to be acknowledged
	SendQueue *_sent_list_head[NUM_STREAMS], *_sent_list_tail[NUM_STREAMS];

private:
	void Retransmit(SendQueue *node);
	void OnACK(u8 *data, u32 data_bytes);
	void OnMTUSet(u8 *data, u32 data_bytes);

public:
	Transport();
	virtual ~Transport();

	static const int TICK_RATE = 20; // 20 milliseconds

public:
	void BeginWrite();
	bool WriteUnreliable(u8 *msg, u32 bytes);
	bool WriteReliable(StreamMode, u8 *msg, u32 bytes, SuperOpcode super_opcode = SOP_DATA);
	void EndWrite();

protected:
	void TickTransport(ThreadPoolLocalStorage *tls, u32 now);

private:
	void OnDatagram(u8 *data, u32 bytes);
	void OnFragment(u8 *data, u32 bytes);
	void CombineNextWrite();

protected:
	virtual bool PostPacket(u8 *data, u32 buf_bytes, u32 msg_bytes) = 0;
	virtual void OnTimestampDeltaUpdate(u32 rtt, s32 delta) {}
	virtual void OnMessage(u8 *msg, u32 bytes) = 0;
	virtual void OnDisconnect() = 0;

protected:
	bool PostMTUProbe(ThreadPoolLocalStorage *tls, u16 payload_bytes);
	bool PostTimePing();
	bool PostTimePong(u32 client_ts);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
