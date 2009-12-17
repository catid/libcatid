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

class Connection;
class ConnectionMap;
class TransportLayer;
class SessionEndpoint;
class ScalableServer;
class ScalableClient;


typedef fastdelegate::FastDelegate3<Connection*, u8*, int, void> MessageLayerHandler;

class TransportLayer
{
protected:
	u32 _recv_unreliable_id[16];
	u32 _recv_reliable_id[8];
	u32 _send_unreliable_id[16];
	u32 _send_reliable_id[8];

public:
	TransportLayer();
	~TransportLayer();

	void OnPacket(UDPEndpoint *endpoint, u8 *data, int bytes, Connection *conn, MessageLayerHandler handler);

	void Tick(UDPEndpoint *endpoint);
};


class Connection
{
public:
	Connection();

private:
	volatile u32 flags;

public:
	// To give the timer thread a quick way to enumerate all connections
	Connection *next_timed;
	Connection *last_timed;

public:
	enum
	{
		FLAG_USED,		// Slot is used
		FLAG_COLLISION,	// Collision occurred in this slot
		FLAG_TIMED,		// Has been recognized by the timer thread
		FLAG_DELETE,	// Will be deleted next time the timer thread runs
		FLAG_C2S_ENC,	// Has seen first encrypted packet
	};

	CAT_INLINE void ClearFlags();
	CAT_INLINE bool IsFlagSet(int flag);
	CAT_INLINE bool IsFlagUnset(int flag);
	CAT_INLINE bool SetFlag(int flag); // Returns false iff flag was already set
	CAT_INLINE bool UnsetFlag(int flag); // Returns false iff flag was already unset

public:
	IP6 remote_ip;
	Port remote_port;
	Port server_port;
	SessionEndpoint *server_endpoint;
	volatile u32 next_inserted;
	u32 last_recv_tsc; // Last time a packet was received from this user -- for disconnect timeouts

public:
	u8 first_challenge[64]; // First challenge seen from this client address
	u8 cached_answer[128]; // Cached answer to this first challenge, to avoid eating server CPU time

public:
	AuthenticatedEncryption auth_enc;

public:
	TransportLayer transport;
};


class ConnectionMap
{
public:
	static const int HASH_TABLE_SIZE = 32768;

	// (multiplier-1) divisible by all prime factors of table size
	// (multiplier-1) is a multiple of 4 if table size is a multiple of 4
	// These constants are from Press, Teukolsky, Vetterling and Flannery's
	// "Numerical Recipes in FORTRAN: The Art of Scientific Computing"
	static const int COLLISION_MULTIPLIER = 71*5861 * 4 + 1;
	static const int COLLISION_INCREMENTER = 1013904223;

protected:
	CAT_INLINE u32 hash_addr(const sockaddr_in6 &addr, u32 salt);
	CAT_INLINE u32 next_collision_key(u32 key);

	u32 _hash_salt;
	CAT_ALIGNED(16) Connection _table[HASH_TABLE_SIZE];

public:
	ConnectionMap();

	Connection *Get(const sockaddr_in6 &addr);
	Connection *Insert(const sockaddr_in6 &addr);
	bool Remove(Connection *conn); // Return false iff connection was already removed

protected:
	// Actually key+1, so 0 can be used to indicate an empty list
	volatile u32 _insert_head_key1;

public:
	void CompleteInsertion(Connection *conn);

public:
	Connection *GetFirstInserted();
	// Get next recently-inserted slot and unlink it
	Connection *GetNextInserted(Connection *conn);
};


class SessionEndpoint : public UDPEndpoint
{
	friend class ScalableServer;
	volatile u32 _session_count;
	ConnectionMap *_conn_map;

public:
	SessionEndpoint(ConnectionMap *conn_map);
	~SessionEndpoint();

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const sockaddr_in6 &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();

	void HandleMessageLayer(Connection *conn, u8 *msg, int bytes);
};


class ScalableServer : LoopThread, public UDPEndpoint
{
public:
	static const u32 PROTOCOL_MAGIC = 0xC47D0001;
	static const int PUBLIC_KEY_BYTES = 64;
	static const int PRIVATE_KEY_BYTES = 32;
	static const int CHALLENGE_BYTES = PUBLIC_KEY_BYTES;
	static const int ANSWER_BYTES = PUBLIC_KEY_BYTES*2;
	static const int MAX_POPULATION = ConnectionMap::HASH_TABLE_SIZE / 2;

private:
	Port _server_port;
	ConnectionMap _conn_map;
	CookieJar _cookie_jar;
	KeyAgreementResponder _key_agreement_responder;
	u8 _public_key[PUBLIC_KEY_BYTES];
	int _session_port_count;
	SessionEndpoint **_sessions;

	SessionEndpoint *FindLeastPopulatedPort();
	u32 GetTotalPopulation();

	bool ThreadFunction(void *param);

public:
	ScalableServer();
	~ScalableServer();

	bool Initialize(ThreadPoolLocalStorage *tls, Port port);

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const sockaddr_in6 &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
};


class ScalableClient : LoopThread, public UDPEndpoint
{
public:
	static const u32 PROTOCOL_MAGIC = ScalableServer::PROTOCOL_MAGIC;
	static const int PUBLIC_KEY_BYTES = ScalableServer::PUBLIC_KEY_BYTES;
	static const int PRIVATE_KEY_BYTES = ScalableServer::PRIVATE_KEY_BYTES;
	static const int CHALLENGE_BYTES = ScalableServer::CHALLENGE_BYTES;
	static const int ANSWER_BYTES = ScalableServer::ANSWER_BYTES;

private:
	KeyAgreementInitiator _key_agreement_initiator;
	AuthenticatedEncryption _auth_enc;
	TransportLayer _transport;
	sockaddr_in6 _server_addr;
	bool _connected;
	u8 _server_public_key[PUBLIC_KEY_BYTES];
	u8 _cached_challenge[CHALLENGE_BYTES];

	bool ThreadFunction(void *param);

public:
	ScalableClient();
	~ScalableClient();

	bool Connect(ThreadPoolLocalStorage *tls, const sockaddr_in6 &addr, const void *server_key, int key_bytes);

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const sockaddr_in6 &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
	void OnUnreachable(const sockaddr_in6 &src);

protected:
	bool PostHello();

protected:
	virtual void OnConnectFail();
	virtual void OnConnect();
	virtual void HandleMessageLayer(Connection *key, u8 *msg, int bytes);
	virtual void OnDisconnect(bool timeout);
};


} // namespace cat

#endif // CAT_SCALABLE_SERVER_HPP
