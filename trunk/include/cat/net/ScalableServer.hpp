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

#ifndef CAT_SCALABLE_SERVER_HPP
#define CAT_SCALABLE_SERVER_HPP

#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/AllTunnel.hpp>
#include <cat/parse/MessageRouter.hpp>
#include <cat/port/FastDelegate.h>

namespace cat {


/*
	Implements a network server that...
		runs one thread for each processor.
		runs a single socket for performing handshakes with new users.
			For now just process one handshake packet at a time.
				-> Handshake::Insert() only called from one thread at a time.
		runs one socket for each processor for handling established sessions.
			-> Handshake::Get() called from multiple threads at a time.
			-> Handshake::Remove() called from multiple threads at a time.

	handshake port:

	c2s 00 (protocol magic[4])
	s2c 01 (cookie[4]) (public key[64])
	c2s 02 (cookie[4]) (challenge[64])
	s2c 03 (server session port[2]) (answer[128])

	session port:

	c2s E{ 00 (proof[32]) (server ip[4]) (server port[2]) (ban data) }
	s2c E{ ... }
*/

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


struct Connection
{
	IP _remote_ip;
	Port _remote_port;
	Port _server_port;
	AuthenticatedEncryption _auth_enc;
	u8 _challenge[64];
	u8 _answer[128];
	bool _seen_first_encrypted_packet;
	u32 _recv_unreliable_id[16];
	u32 _recv_reliable_id[8];
	u32 _send_unreliable_id[16];
	u32 _send_reliable_id[8];
};


class ConnectionMap
{
public:
	static const int HASH_TABLE_SIZE = 10000;

	CAT_INLINE u32 hash_addr(IP ip, Port port, u32 salt);

	struct HashKey
	{
		volatile u32 references;
		Connection *conn;
		HashKey *next;
	};

protected:
	u32 _hash_salt;
	HashKey *_table;

public:
	ConnectionMap();
	~ConnectionMap();

	bool Initialize();

	HashKey *Get(IP ip, Port port);
	HashKey *Insert(IP ip, Port port);
	void Remove(IP ip, Port port);
};


class SessionEndpoint : public UDPEndpoint
{
	friend class HandshakeEndpoint;
	volatile u32 _session_count;
	ConnectionMap *_conn_map;

public:
	SessionEndpoint(ConnectionMap *conn_map);
	~SessionEndpoint();

protected:
	void OnRead(ThreadPoolLocalStorage *tls, IP srcIP, Port srcPort, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();

	void HandleTransportLayer(Connection *conn, u8 *data, int bytes);

	void HandleMessageLayer(Connection *conn, u8 *msg, int bytes);
};


class HandshakeEndpoint : public UDPEndpoint
{
	ConnectionMap _conn_map;
	CookieJar _cookie_jar;
	KeyAgreementResponder _key_agreement_responder;
	u8 _public_key[64];
	int _session_port_count;
	SessionEndpoint **_sessions;

public:
	HandshakeEndpoint();
	~HandshakeEndpoint();

	static const u32 PROTOCOL_MAGIC = 0xC47D0001;
	static const int PUBLIC_KEY_BYTES = 64;
	static const int PRIVATE_KEY_BYTES = 32;
	static const int CHALLENGE_BYTES = PUBLIC_KEY_BYTES;
	static const int ANSWER_BYTES = PUBLIC_KEY_BYTES*2;
	static const int SERVER_PORT = 22000;

	bool Initialize();

protected:
	void OnRead(ThreadPoolLocalStorage *tls, IP srcIP, Port srcPort, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
};


} // namespace cat

#endif // CAT_SCALABLE_SERVER_HPP
