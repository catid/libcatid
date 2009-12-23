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

#ifndef CAT_SPHYNX_SERVER_HPP
#define CAT_SPHYNX_SERVER_HPP

#include <cat/net/SphynxTransport.hpp>
#include <cat/AllTunnel.hpp>
#include <cat/threads/RWLock.hpp>

namespace cat {


namespace sphynx {


//// sphynx::Connection

class Connection
{
public:
	Connection();

public:
	bool delete_me;
	Connection *next_delete;
	NetAddr client_addr;
	ServerWorker *server_endpoint;

	// Last time a packet was received from this user -- for disconnect timeouts
	u32 last_recv_tsc;

public:
	u8 first_challenge[64]; // First challenge seen from this client address
	u8 cached_answer[128]; // Cached answer to this first challenge, to avoid eating server CPU time

public:
	bool seen_encrypted;
	AuthenticatedEncryption auth_enc;
	Mutex send_lock;

public:
	TransportSender transport_sender;
	TransportReceiver transport_receiver;

public:
	// Returns false iff connection should be deleted
	bool Tick(u32 now);

public:
	void HandleRawData(u8 *data, u32 bytes);
	void HandleMessage(u8 *msg, u32 bytes);

public:
	bool Post(u8 *msg, u32 bytes);
};


//// sphynx::Map

class Map
{
protected:
	CAT_INLINE u32 hash_addr(const NetAddr &addr, u32 salt);
	CAT_INLINE u32 next_collision_key(u32 key);

	struct Slot
	{
		Connection *connection;
		bool collision;
		Slot *next;
	};

	u32 _hash_salt;
	CAT_ALIGNED(16) Slot _table[HASH_TABLE_SIZE];
	RWLock _table_lock;

protected:
	Slot *_insert_head;
	Mutex _insert_lock;

protected:
	Slot *_active_head;

public:
	Map();
	virtual ~Map();

public:
	Connection *GetLock(const NetAddr &addr);
	void ReleaseLock();

public:
	void Insert(Connection *conn);

public:
	void Tick();
};


//// sphynx::ServerWorker

class ServerWorker : public UDPEndpoint
{
	friend class Server;
	volatile u32 _session_count;
	Map *_conn_map;
	MessageLayerHandler _handler;

public:
	ServerWorker(Map *conn_map);
	virtual ~ServerWorker();

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
};


//// sphynx::Server

class Server : LoopThread, public UDPEndpoint
{
private:
	Port _server_port;
	Map _conn_map;
	CookieJar _cookie_jar;
	KeyAgreementResponder _key_agreement_responder;
	u8 _public_key[PUBLIC_KEY_BYTES];
	int _session_port_count;
	ServerWorker **_sessions;

	ServerWorker *FindLeastPopulatedPort();
	u32 GetTotalPopulation();

	bool ThreadFunction(void *param);

public:
	Server();
	virtual ~Server();

	bool Initialize(ThreadPoolLocalStorage *tls, Port port);

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_SERVER_HPP
