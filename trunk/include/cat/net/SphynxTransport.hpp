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


class Connection;
class Map;
class Server;
class ServerWorker;
class Client;
class TransportSender;
class TransportReceiver;

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
	S2C_ANSWER
};


typedef fastdelegate::FastDelegate3<Connection*, u8*, int, void> MessageLayerHandler;


//// sphynx::TransportSender

class TransportSender
{
public:
	TransportSender();
	~TransportSender();

	void Tick(UDPEndpoint *endpoint);
};


//// sphynx::TransportReceiver

class TransportReceiver
{
public:
	TransportReceiver();
	~TransportReceiver();

	void OnPacket(UDPEndpoint *endpoint, u8 *data, int bytes, Connection *conn, MessageLayerHandler handler);

	void Tick(UDPEndpoint *endpoint);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_TRANSPORT_HPP
