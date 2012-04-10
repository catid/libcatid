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

#ifndef CAT_SPHYNX_COMMON_HPP
#define CAT_SPHYNX_COMMON_HPP

#include <cat/net/Sockets.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>
#include <cat/mem/ResizableBuffer.hpp>
#include <cat/io/Buffers.hpp>
#include <cat/parse/BufferStream.hpp>

// TODO: periodically reset the average trip time to avoid skewing statistics
// TODO: make debug output optional with preprocessor flag
// TODO: evaluate all places that allocate memory to see if retry would help
// TODO: vulnerable to resource starvation attacks?
// TODO: fix a bug that eats all the buffers when none are available
// TODO: fix a bug that drops data on the floor when it arrives out of order

#define CAT_TRANSPORT_RANDOMIZE_LENGTH /* Add extra no-op bytes to the end of each datagram to mask true length */

#if defined(CAT_WORD_32)
#define CAT_PACK_TRANSPORT_STATE_STRUCTURES /* For 32-bit version, this allows fragments to fit in 32 bytes */
#else // 64-bit version:
//#define CAT_PACK_TRANSPORT_STATE_STRUCTURES /* No advantage for 64-bit version */
#endif

namespace cat {


namespace sphynx {


class Connexion;
class Map;
class Server;
class ServerWorker;
class ServerTimer;
class Client;
class Transport;
class FlowControl;


// Protocol constants
static const u32 PROTOCOL_MAGIC = 0xC47D0001;
static const int PUBLIC_KEY_BYTES = 64;
static const int PRIVATE_KEY_BYTES = 32;
static const int CHALLENGE_BYTES = PUBLIC_KEY_BYTES;
static const int ANSWER_BYTES = PUBLIC_KEY_BYTES*2;
static const int SPHYNX_OVERHEAD = AuthenticatedEncryption::OVERHEAD_BYTES;

// Client constants
static const int SESSION_KEY_BYTES = 32;
static const int HANDSHAKE_TICK_RATE = 100; // milliseconds
static const int INITIAL_HELLO_POST_INTERVAL = 200; // milliseconds
static const int CONNECT_TIMEOUT = 6000; // milliseconds
static const u32 MTU_PROBE_INTERVAL = 8000; // seconds
static const int CLIENT_THREAD_KILL_TIMEOUT = 10000; // seconds
static const int SILENCE_LIMIT = 4357; // Time silent before sending a keep-alive (0-length unordered reliable message), milliseconds

// Clock Synchronization
static const int TS_COMPRESS_FUTURE_TOLERANCE = 1000; // Milliseconds of time synch error before errors may occur in timestamp compression
static const int TS_INTERVAL = 10000; // Normal time synch interval, milliseconds
static const int TS_FAST_COUNT = 20; // Number of fast measurements at the start, milliseconds
static const int TS_FAST_PERIOD = 2000; // Interval during fast measurements, milliseconds
static const int TS_MAX_SAMPLES = 16; // Maximum timestamp sample memory
static const int TS_MIN_SAMPLES = 1; // Minimum number of timestamp samples

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
static const u32 S2C_ANSWER_LEN = 1 + ANSWER_BYTES;
static const u32 S2C_ERROR_LEN = 1 + 1;

// Handshake errors
enum SphynxError
{
	ERR_CLIENT_OUT_OF_MEMORY,
	ERR_CLIENT_INVALID_KEY,
	ERR_CLIENT_SERVER_ADDR,
	ERR_CLIENT_BROKEN_PIPE,
	ERR_CLIENT_TIMEOUT,
	ERR_NUM_INTERNAL_ERRORS,

	ERR_WRONG_KEY = 0x7f,
	ERR_SERVER_FULL = 0xa6,
	ERR_TAMPERING = 0xcc,
	ERR_BLOCKED = 0xb7,
	ERR_SHUTDOWN = 0x3a,
	ERR_SERVER_ERROR = 0x1f
};

// Convert handshake error string to user-readable error message
CAT_EXPORT const char *GetSphynxErrorString(SphynxError err);

// Disconnect reasons
enum DisconnectReasons
{
	DISCO_CONNECTED = 0,		// Not disconnected

	DISCO_SILENT = 0xff,		// Disconnect without transmitting a reason
	DISCO_TIMEOUT = 0xfe,		// Remote host has not received data from us
	DISCO_TAMPERING = 0xfd,		// Remote host received a tampered packet
	DISCO_BROKEN_PIPE = 0xfc,	// Our socket got closed
	DISCO_USER_EXIT = 0xfb		// User closed the remote application

	// Feel free to define your own disconnect reasons.  Here is probably not the best place.
};

// Stream modes
enum StreamMode
{
	STREAM_UNORDERED = 0,	// Reliable, unordered stream 0 (highest transmit priority)
	STREAM_1 = 1,			// Reliable, ordered stream 1 (slightly lower priority)
	STREAM_2 = 2,			// Reliable, ordered stream 2 (slightly lower priority)
	STREAM_BULK = 3			// Reliable, ordered stream 3 (lowest priority data sent after all others)
};

// Super opcodes
enum SuperOpcode
{
	SOP_INTERNAL,	// 0=Internal (reliable or unreliable)
	SOP_DATA,		// 1=Data (reliable or unreliable)
	SOP_FRAG,		// 2=Fragment (reliable)
	SOP_ACK			// 3=ACK (unreliable)
};

// Internal opcodes
enum InternalOpcode
{
	IOP_C2S_MTU_PROBE = 0,	// c2s 00 (random padding[MTU]) Large MTU test message
	IOP_S2C_MTU_SET = 0,	// s2c 00 (mtu[2]) MTU set message

	IOP_C2S_TIME_PING = 1,	// c2s 01 (client timestamp[4]) Time synchronization ping
	IOP_S2C_TIME_PONG = 1,	// s2c 01 (client timestamp[4]) (server timestamp[4]) Time synchronization pong

	IOP_FILE_PART = 2,		// a2a 02 (ID[3]) (data[MTU]) File part

	IOP_DISCO = 3			// a2a 03 (reason[1]) Disconnection notification
};

// Internal opcode lengths
static const u32 IOP_C2S_MTU_TEST_MINLEN = 1 + 200;
static const u32 IOP_S2C_MTU_SET_LEN = 1 + 2;
static const u32 IOP_C2S_TIME_PING_LEN = 1 + 4;
static const u32 IOP_S2C_TIME_PONG_LEN = 1 + 4 + 4 + 4;
static const u32 IOP_FILE_PART_MINLEN = 1 + 3 + 1;
static const u32 IOP_DISCO_LEN = 1 + 1;

// MTU discovery guesses
static const u32 MINIMUM_MTU = 576; // Dial-up
static const u32 MEDIUM_MTU = 1400; // Highspeed with unexpected overhead, maybe VPN
static const u32 MAXIMUM_MTU = 1500; // Highspeed


//// sphynx::Transport

static const u32 MAX_MESSAGE_SIZE = 65535;	// Past this size the messages must go through the WriteHuge() interface
static const int TIMEOUT_DISCONNECT = 15000; // milliseconds; NOTE: If this changes, the timestamp compression will stop working
static const u32 NUM_STREAMS = 4; // Number of reliable streams

// (multiplier-1) divisible by all prime factors of table size
// (multiplier-1) is a multiple of 4 if table size is a multiple of 4
// These constants are from Press, Teukolsky, Vetterling and Flannery's
// "Numerical Recipes in FORTRAN: The Art of Scientific Computing"
static const u32 COLLISION_MULTIPLIER = 71*5861 * 4 + 1;
static const u32 COLLISION_INCREMENTER = 1013904223;

// If multiplier changes, this needs to be recalculated (multiplicative inverse of above)
static const u32 COLLISION_MULTINVERSE = 0xfee058c5;
static const u32 COLLISION_INCRINVERSE = 0 - COLLISION_INCREMENTER;


// Interface for a huge data source > MAX_MESSAGE_SIZE
class IHugeSource
{
	friend class Transport;

protected:
	virtual void NextFilePart(Transport *transport, u32 available_bytes) = 0;
};

// Interface for a huge data sink > MAX_MESSAGE_SIZE
class IHugeSink
{
	friend class Transport;

protected:
	virtual void OnFilePart(u32 stream, u32 id, u8 *data, u32 bytes) = 0;
};

#if defined(CAT_PACK_TRANSPORT_STATE_STRUCTURES)
# pragma pack(push)
# pragma pack(1)
#endif

// Receive state: Fragmentation
struct RecvFrag
{
	u8 *buffer; // Buffer for accumulating fragment
	u16 length; // Number of bytes in fragment buffer
	u16 offset; // Current write offset in buffer
};

// Receive state: Receive queue
struct RecvQueue
{
	RecvQueue *next;	// Next message in list
	RecvQueue *eos;		// End of current sequence (forward)
	u32 id;				// Acknowledgment id
	u16 bytes;			// Data Bytes
	u8 sop;				// Super Opcode

	// Message contents follow
};

// Send state: Send queue
struct OutgoingMessage : ResizableBuffer<OutgoingMessage>
{
	OutgoingMessage *next;	// Next in queue

	union
	{
		// In send queue:
		struct
		{
			u64 huge_remaining;	// Number of bytes remaining in a huge transfer
			u32 frag_count;		// Number of fragments remaining to be delivered
			u32 send_bytes;		// Number of bytes to send this time, calculated in DequeueBandwidth()
			u16 sent_bytes;		// Number of bytes sent so far in a small fragmented message
		};

		// In sent list:
		struct
		{
			OutgoingMessage *prev;	// Previous in queue
			u32 id;					// Acknowledgment id
			u32 ts_firstsend;		// Millisecond-resolution timestamp when it was first sent
			u32 ts_lastsend;		// Millisecond-resolution timestamp when it was last sent
		};
	};

	// Shared members:
	u8 sop;		// Super opcode of message
	u8 loss_on;	// 1=Represents a packet loss on retransmit, 0=Not representative, other values invalid

	/*
		loss_on : Converting messageloss to packetloss 1:1
		Only one reliable message in each packet has loss_on=1.

		When retransmits occur, the loss_on method of detecting
		packetloss from messageloss breaks down.  For instance,
		if an unreliable message is present in the original
		packet it will not be retransmitted.  This may draw in
		the first reliable message from the next packet if the
		second packet is also lost.  If the retransmitted packet
		gets lost too, then two packetloss events are recorded
		instead of one.

		However, this method works great when a packet is only
		lost once and even in most cases when a packet is lost
		several times.  Plus it just adds one byte overhead to
		the outgoing message data.

		An alternative method to using loss_on flag would be to
		acknowledge the encryption IV of each packet instead of
		the message ids.  I have seen this approach used in
		several "my first game protocol" types of articles.
		The problem is that it takes a lot more ACK bandwidth
		because you would have to send all of the recent ACKs
		with each new ACK instead of a simple roll-up if there
		are no holes in the sequence.  Additionally, mapping
		from IV to message sequence number per stream would be
		simply hideous to code.

		Another alternative is to use the millisecond transmit
		time to group clustered messages and detect loss of
		each timestamp group separately.  This is unacceptably
		inaccurate since this netcode intentionally batches
		outgoing packet transmission, so would under-represent
		real loss rates.
	*/
};

struct SendFrag : OutgoingMessage
{
	OutgoingMessage *full_data;	// Object containing message data
	u16 offset;					// Fragment data offset
};

struct SendCluster
{
	u8 *front;	// Pointer to front of the send cluster
	u32 ack_id;	// Next ACK-ID: Used to compress ACK-ID by setting I=0 after the first reliable message
	u16 bytes;	// Number of bytes written to the send cluster so far
	u8 stream;	// Active stream
	u8 loss_on;	// Loss representation flag is set already?  Used to mark just one message as a loss rep

	CAT_INLINE void Clear()
	{
		front = 0;
		bytes = 0;
		stream = NUM_STREAMS;
		loss_on = 0;
	}

	CAT_INLINE u8 *Grow(u32 added)
	{
		bytes += added;

		front = SendBuffer::Resize(front, bytes + SPHYNX_OVERHEAD);
		if (!front)
		{
			bytes = 0;
			stream = NUM_STREAMS;
			return 0;
		}

		return front + bytes - added;
	}
};

#if defined(CAT_PACK_TRANSPORT_STATE_STRUCTURES)
# pragma pack(pop)
#endif

// The following are always packed on a byte boundary so that they will be compatible
// with other languages like C#:
#pragma pack(push)
#pragma pack(1)

// Incoming message data passed to user layer
struct IncomingMessage
{
	BufferStream data;
	u32 bytes;
	u32 stream;
};

#pragma pack(pop)


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_COMMON_HPP
