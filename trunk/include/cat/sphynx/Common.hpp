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

// TODO: SendQueue needs callback for file transfer and support for large external data array
// TODO: when sending a reliable message, pre-allocate the send queue node for zero-copy
// TODO: when reading data from a set of datagrams, produce a single callback for the user also with an array of data
// TODO: coalesce locks in TickTransport()
// TODO: periodically reset the average trip time to avoid skewing statistics
// TODO: make debug output optional with preprocessor flag
// TODO: evaluate all places that allocate memory to see if retry would help
// TODO: add 20ms x 2 fuzz factor in retransmits
// TODO: flow control
// TODO: add random bytes to length of each packet under MTU to mask function
// TODO: do something with the extra two bits in the new timestamp field
// TODO: vulnerable to resource starvation attacks (out of sequence packets, etc)
// TODO: move all packet sending outside of locks

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
	STREAM_UNORDERED = 0,	// Reliable, unordered stream 0 (highest transmit priority)
	STREAM_1 = 1,			// Reliable, ordered stream 1 (slightly lower priority)
	STREAM_2 = 2,			// Reliable, ordered stream 2 (slightly lower priority)
	STREAM_BULK = 3			// Reliable, ordered stream 3 (lowest priority data sent after all others)
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
	IOP_S2C_TIME_PONG = 138,	// s2c 8a (client timestamp[4]) (server timestamp[4]) Time synchronization pong

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


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_COMMON_HPP
