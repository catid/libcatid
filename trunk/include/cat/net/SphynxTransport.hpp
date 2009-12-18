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
#include <cat/port/FastDelegate.h>

namespace cat {


namespace sphynx {


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


class Connection;
class Map;
class Server;
class ServerWorker;
class Client;


// Handshake packet types
enum HandshakeTypes
{
	C2S_HELLO,
	S2C_COOKIE,
	C2S_CHALLENGE,
	S2C_ANSWER
};


/*
	Transport layer:

	Supports message clustering, several streams for
	four types of transport: {unreliable/reliable}, {ordered/unordered}
	and fragmentation for reliable, ordered messages.

	Unreliable
	0 stream(4) len(11) msg(x)
	stream = 0

	Unreliable, ordered
	0 stream(4) len(11) id(24) msg(x)
	stream = 1-15

	Reliable, unordered
	1 0 stream(3) len(11) id(15) unused(1) msg(x)
	1 1 stream(3) count-1(3) id(15) nack(1) id(15) nack(1) id(15) nack(1)
	stream = 0

	Reliable, ordered
	1 0 stream(3) len(11) id(15) frag(1) msg(x)
	1 1 stream(3) count-1(3) id(15) nack(1) id(15) nack(1) id(15) nack(1)
	stream = 1-7

	To transmit a large buffer over several packets, it must be reassembled in order.
	If any parts are lost, then the whole buffer is lost.
	Therefore only reliable, ordered streams make sense.
	This is implemented with the frag(ment) bit:
		frag = 1 : Fragment in a larger message
		frag = 0 : Final fragment or a whole message
*/

typedef fastdelegate::FastDelegate3<Connection*, u8*, int, void> MessageLayerHandler;


//// sphynx::Transport

class Transport
{
protected:
	u32 _recv_unreliable_id[16];
	u32 _recv_reliable_id[8];
	u32 _send_unreliable_id[16];
	u32 _send_reliable_id[8];

public:
	Transport();
	~Transport();

	void OnPacket(UDPEndpoint *endpoint, u8 *data, int bytes, Connection *conn, MessageLayerHandler handler);

	void Tick(UDPEndpoint *endpoint);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
