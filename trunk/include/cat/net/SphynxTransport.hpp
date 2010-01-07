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
		|   DATA_BYTES(11)    |STM|F|I|D|
		---------------------------------

		DATA_BYTES: Number of bytes in data part of message.
		STM: 0=Unordered stream, 1-3: Ordered streams.
		F: 1=Fragmented when STM=1-3 or Unreliable when STM=0. 0=Not fragmented.
		I: 1=Followed by ACK-ID.
		D: 1=Data, 0=ACK.

		Message meanings in no particular order:

		 0 : Unreliable message
			F = 1, I = x, STM = 0, D=1 (x = don't care)
		 1 : Unordered stream 0 unfragmented message without ACKID
			F = 0, I = 0, STM = 0, D=1
		 2 : Unordered stream 0 unfragmented message with ACKID
			F = 0, I = 1, STM = 0, D=1
		 3 : Unordered stream 0 acknowledgment
			F = x, I = x, STM = 0, D=0 (x = don't care)
		 4 : Stream 1 unfragmented message without ACKID
			F = 0, I = 0, STM = 1, D=1
		 5 : Stream 1 unfragmented message with ACKID
			F = 0, I = 1, STM = 1, D=1
		 6 : Stream 1 fragmented message without ACKID
			F = 1, I = 0, STM = 1, D=1
		 7 : Stream 1 fragmented message with ACKID
			F = 1, I = 1, STM = 1, D=1
		 8 : Stream 1 acknowledgment
			F = x, I = x, STM = 1, D=0 (x = don't care)
		 9 : Stream 2 unfragmented message without ACKID
			F = 0, I = 0, STM = 2, D=1
		10 : Stream 2 unfragmented message with ACKID
			F = 0, I = 1, STM = 2, D=1
		11 : Stream 2 fragmented message without ACKID
			F = 1, I = 0, STM = 2, D=1
		12 : Stream 2 fragmented message with ACKID
			F = 1, I = 1, STM = 2, D=1
		13 : Stream 2 acknowledgment
			F = x, I = x, STM = 2, D=0 (x = don't care)
		14 : Stream 3 unfragmented message without ACKID
			F = 0, I = 0, STM = 3, D=1
		15 : Stream 3 unfragmented message with ACKID
			F = 0, I = 1, STM = 3, D=1
		16 : Stream 3 fragmented message without ACKID
			F = 1, I = 0, STM = 3, D=1
		17 : Stream 3 fragmented message with ACKID
			F = 1, I = 1, STM = 3, D=1
		18 : Stream 3 acknowledgment
			F = x, I = x, STM = 3, D=0 (x = don't care)
		19-31 : Undefined (contained in the don't care cases above)

		When the I bit is set, the data part is preceded by a 24-bit ACK-ID,
		which is then applied to all following reliable messages.
		This additional size is NOT accounted for in the DATA_BYTES field.

		When the F bit is set for the first time in an ordered stream,
		the data part begins with a 16-bit Total Message Length.
		This additional size IS accounted for in the DATA_BYTES field.

	TODO:
		Message router built into transport layer
		Message router used differently in client and server derivations
			FE: Time Ping/Pong
			FF: MTU Probe/Update
		EndWrite()
		ACK transmit
		ACK receive
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


// Handshake packet types
enum HandshakeTypes
{
	C2S_HELLO,
	S2C_COOKIE,
	C2S_CHALLENGE,
	S2C_ANSWER,
	S2C_ERROR
};

// Message opcodes
enum HandshakeErrors
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


//// sphynx::Transport

class Transport
{
protected:
	static const u16 DATALEN_MASK = 0x7ff;
	static const u16 F_MASK = 1 << 13;
	static const u16 I_MASK = 1 << 14;
	static const u16 D_MASK = 1 << 15;
	static const u16 STM_MASK = 3 << 11;
	static const u32 STM_OFFSET = 11;

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

	static const u32 FRAG_THRESHOLD = 64; // Fragment if 64 bytes would be in each fragment

	static const u32 MAX_MESSAGE_DATALEN = 65535; // Maximum number of bytes in the data part of a message

	// Maximum transfer unit (MTU) in UDP payload bytes, excluding the IP and UDP headers and encryption overhead
	u32 _max_payload_bytes;

public:
	void InitializePayloadBytes(bool ip6);

protected:
	// Receive state: Next expected ack id to receive
	u32 _next_recv_expected_id[NUM_STREAMS];

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

		RecvQueue *next;	// Next in queue
		u32 id;				// Acknowledgment id
		u32 bytes;			// High bit: Fragment?

		// Message contents follow
	};

	// Receive state: Receive queue head
	RecvQueue *_recv_queue_head[NUM_STREAMS]; // Head of queue for messages that are waiting on a lost message

protected:
	void QueueRecv(u8 *data, u32 bytes, u32 ack_id, bool frag);

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
		SendQueue *next; // Next in queue
		u32 id; // Acknowledgment id, or number of sent bytes while fragmenting a large message
		u32 bytes; // Data bytes
		u32 mode; // Transmit mode

		// Message contents follow
	};

	struct SendFrag
	{
		SendQueue *full_data;
		u32 offset;
	};

	// Queue of messages that are waiting to be sent
	SendQueue *_send_queue_head[NUM_STREAMS], *_send_queue_tail[NUM_STREAMS];

	// List of messages that are waiting to be acknowledged
	SendQueue *_sent_list_head[NUM_STREAMS];

public:
	Transport();
	virtual ~Transport();

	static const int TICK_RATE = 20; // 20 milliseconds

public:
	void BeginWrite();
	u8 *GetUnreliableBuffer(u32 data_bytes);
	u8 *GetReliableBuffer(StreamMode, u32 data_bytes);
	void EndWrite();

protected:
	void TickTransport(ThreadPoolLocalStorage *tls, u32 now);

private:
	void OnDatagram(u8 *data, u32 bytes);
	void OnFragment(u8 *data, u32 bytes);

protected:
	virtual void OnMessage(u8 *msg, u32 bytes) = 0;
	virtual bool PostPacket(u8 *data, u32 buf_bytes, u32 msg_bytes) = 0;
	virtual void OnTimestampDeltaUpdate(u32 rtt, s32 delta) {}
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
