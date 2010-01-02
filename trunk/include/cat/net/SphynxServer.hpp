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

/*
	Designed for server hardware with many processors.

	In order to handle many users, the Sphynx server opens up a single UDP port
	to accept new connections, and several other UDP data ports for data.

	For retransmissions and detecting link loss due to timeout, the server runs
	several additional threads that wake up periodically to perform timed tasks.

	Server uses thread pool to receive packets on connect and worker ports,
	meaning that packets are processed by any free CPU as soon as they arrive.

	Sphynx Server
		UDP Hello Port [1]
			+ In case this thread spins constantly, only use one CPU for new
			  connections since in-game experience is more important than login
		    + Assigns users to a data port after handshake completes

		UDP Data Ports [4 * (CPU Count)]
			+ Spread users evenly across several ports since
			  only one packet can be processed from a single port at a time
		    + Any free CPU will process incoming packets as fast as possible

		ServerTimer threads [(CPU Count) / 2]
			+ In case these threads spin constantly, they only consume
			  half of the CPU resources available
			+ Wake up every 10 milliseconds
				+ As per UDT (transport layer) spec
			+ Detect link loss due to silence timeout
			+ Update transport layer
				+ Retransmit lost messages
				+ Re-evaluate bandwidth and transmit queued messages
*/


//// sphynx::Connection

class Connection : public Transport
{
public:
	Connection();

protected:
	volatile u32 destroyed;

public:
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
	CAT_INLINE bool IsValid() { return destroyed == 0; }

	void Destroy();

public:
	// Return false to destroy this object
	bool Tick(u32 now);

public:
	void OnRawData(u8 *data, u32 bytes);
	void OnDestroy();

protected:
	virtual void OnMessage(u8 *msg, u32 bytes);
	virtual bool SendPacket(u8 *msg, u32 bytes);
};


//// sphynx::Map

class Map
{
protected:
	CAT_INLINE u32 hash_addr(const NetAddr &addr, u32 salt);
	CAT_INLINE u32 next_collision_key(u32 key);

public:
	struct Slot
	{
		Connection *connection;
		bool collision;
		Slot *next;
	};

protected:
	u32 _hash_salt;
	CAT_ALIGNED(16) Slot _table[HASH_TABLE_SIZE];
	RWLock _table_lock;

public:
	Map();
	virtual ~Map();

public:
	Connection *GetLock(const NetAddr &addr);
	void ReleaseLock();

public:
	void Insert(Connection *conn);

public:
	// Destroy a list described by the 'next' member of Slot
	void DestroyList(Map::Slot *kill_list);

public:
	void Tick();
};


//// sphynx::ServerWorker

class ServerWorker : public UDPEndpoint
{
	friend class Map;

protected:
	Map *_conn_map;
	ServerTimer *_server_timer;

protected:
	volatile u32 _session_count;

public:
	ServerWorker(Map *conn_map, ServerTimer *server_timer);
	virtual ~ServerWorker();

	void IncrementPopulation();
	void DecrementPopulation();
	u32 GetPopulation() { return _session_count; }

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
};


//// sphynx::ServerTimer

class ServerTimer : LoopThread
{
protected:
	Map *_conn_map;

protected:
	ServerWorker **_workers;
	int _worker_count;

protected:
	Map::Slot *_insert_head;
	Mutex _insert_lock;

protected:
	Map::Slot *_active_head;

public:
	ServerTimer(Map *conn_map, ServerWorker **workers, int worker_count);
	virtual ~ServerTimer();

	CAT_INLINE bool Valid() { return _worker_count > 0; }

public:
	void InsertSlot(Map::Slot *slot);

protected:
	CAT_INLINE void Tick();
	bool ThreadFunction(void *param);
};


//// sphynx::Server

class Server : public UDPEndpoint
{
protected:
	Port _server_port;
	Map _conn_map;

protected:
	CookieJar _cookie_jar;
	KeyAgreementResponder _key_agreement_responder;
	u8 _public_key[PUBLIC_KEY_BYTES];

protected:
	ServerWorker **_workers;
	int _worker_count;

protected:
	ServerTimer **_timers;
	int _timer_count;

protected:
	ServerWorker *FindLeastPopulatedPort();
	u32 GetTotalPopulation();

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
