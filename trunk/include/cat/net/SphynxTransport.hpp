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

enum HandshakeErrors
{
	ERR_SERVER_FULL
};

// Transport mode
enum TransportMode
{
	MODE_UNRELIABLE_NOW,	// Out-of-band, unreliable transport
	MODE_UNRELIABLE,		// In-band, unreliable transport
	MODE_RELIABLE			// In-band, reliable, ordered transport
};


//// sphynx::Transport

class Transport
{
protected:
	static const u32 MINIMUM_MTU = 576; // Dialup
	static const u32 MEDIUM_MTU = 1400; // Highspeed with unexpected overhead, maybe VPN
	static const u32 MAXIMUM_MTU = 1500; // Highspeed

	static const u32 IPV6_HEADER_BYTES = 40;
	static const u32 IPV4_HEADER_BYTES = 20;
	static const u32 UDP_HEADER_BYTES = 8;

	// Maximum transfer unit (MTU) in UDP payload bytes, excluding the IP and UDP headers and encryption overhead
	u32 _mtu;

public:
	CAT_INLINE void SetMTU(u32 mtu) { _mtu = mtu; }

protected:
	// Receive state: Next expected ack id to receive
	u32 _next_recv_expected_id;

	// Receive state: Fragmentation
	u8 *_fragment_buffer; // Buffer for accumulating fragment
	u32 _fragment_offset; // Current write offset in buffer
	u32 _fragment_length; // Number of bytes in fragment buffer

	static const u32 FRAG_MIN = 2;		// Min bytes for a fragmented message
	static const u32 FRAG_MAX = 256000;	// Max bytes for a fragmented message

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
	RecvQueue *_recv_queue_head; // Head of queue for messages that are waiting on a lost message

	void QueueRecv(u8 *data, u32 bytes, u32 ack_id, bool frag);

protected:
	// Send state: Next ack id to use
	u32 _next_send_id;

	// Send state: Send queue
	struct SendQueue
	{
		static const u32 FRAG_FLAG = 0x80000000;
		static const u32 BYTE_MASK = 0x7fffffff;

		SendQueue *next; // Next in queue
		u32 id; // Acknowledgment id
		u32 bytes; // High bit: Fragment?

		// Message contents follow
	};

	SendQueue *_send_queue_head; // Head of queue for messages that are waiting to be sent

	SendQueue *_sent_list_head; // Head of queue for messages that are waiting to be acknowledged

	Mutex _send_lock;

protected:
	// Called whenever a connection-related event occurs to simulate smooth
	// and consistent transmission of messages queued for delivery
	void TransmitQueued();

public:
	Transport();
	virtual ~Transport();

	static const int TICK_RATE = 20; // 20 milliseconds

protected:
	void TickTransport(ThreadPoolLocalStorage *tls, u32 now);
	void OnPacket(u8 *data, u32 bytes);
	void OnFragment(u8 *data, u32 bytes, bool frag);
	void SendMessage(TransportMode, u8 *data, u32 bytes);

protected:
	bool PostMTUDiscoveryRequest(ThreadPoolLocalStorage *tls, u32 payload_bytes);

protected:
	virtual void OnMessage(u8 *msg, u32 bytes) = 0;
	virtual bool SendPacket(u8 *data, u32 buf_bytes, u32 msg_bytes) = 0;
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
