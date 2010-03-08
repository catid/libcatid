/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

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
#include <cat/threads/Mutex.hpp>

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
			+ Wake up every X milliseconds according to Transport::TICK_RATE
			+ Detect link loss due to silence timeout
			+ Update transport layer
				+ Retransmit lost messages
				+ Re-evaluate bandwidth and transmit queued messages
*/


//// sphynx::Connexion

// Derive from sphynx::Connexion and sphynx::Server to define server behavior
class Connexion : public Transport, public ThreadRefObject
{
	friend class Server;
	friend class Map;
	friend class ServerWorker;
	friend class ServerTimer;

public:
	Connexion();
	virtual ~Connexion() {}

private:
	volatile u32 _destroyed;

	Connexion *_next_delete;
	ServerWorker *_server_worker;

	u8 _first_challenge[64]; // First challenge seen from this client address
	u8 _cached_answer[128]; // Cached answer to this first challenge, to avoid eating server CPU time

private:
	// Return false to destroy this object
	bool Tick(ThreadPoolLocalStorage *tls, u32 now);

	void OnRawData(ThreadPoolLocalStorage *tls, u8 *data, u32 bytes);

	virtual bool PostPacket(u8 *buffer, u32 buf_bytes, u32 msg_bytes, u32 skip_bytes);

public:
	CAT_INLINE bool IsValid() { return _destroyed == 0; }

	void Destroy();

protected:
	NetAddr _client_addr;

	// Last time a packet was received from this user -- for disconnect timeouts
	u32 _last_recv_tsc;

	bool _seen_encrypted;
	AuthenticatedEncryption _auth_enc;

protected:
	virtual void OnConnect(ThreadPoolLocalStorage *tls) = 0;
	virtual void OnDestroy() = 0;
	virtual void OnTick(ThreadPoolLocalStorage *tls, u32 now) = 0;
};


//// sphynx::Collexion

template<class T>
class CollexionIterator;

template<class T>
struct CollexionElement
{
	u32 hash, next;
	T *conn;
};

template<class T>
class Collexion
{
	static const u32 COLLIDE_MASK = 0x80000000;
	static const u32 KILL_MASK = 0x40000000;
	static const u32 NEXT_MASK = 0x3fffffff;
	static const u32 MIN_ALLOCATED = 32;

private:
	u32 _used, _allocated, _first;
	CollexionElement<T> *_table;
	Mutex _lock;

protected:
	// Attempt to double size of hash table (does not hold lock)
	bool DoubleTable();

	static CAT_INLINE u32 HashPtr(T *ptr)
	{
		u64 key = 0xBADdecafDEADbeef;

#if defined(CAT_WORD_64)
		key ^= *(u64*)&ptr;
#else
		key ^= *(u32*)&ptr;
#endif

		key = (~key) + (key << 18);
		key = key ^ (key >> 31);
		key = key * 21;
		key = key ^ (key >> 11);
		key = key + (key << 6);
		key = key ^ (key >> 22);
		return (u32)key;
	}

public:
	Collexion()
	{
		_first = 0;
		_used = 0;
		_allocated = 0;
		_table = 0;
	}
	~Collexion();

	// Returns true if table is empty
	CAT_INLINE bool IsEmpty() { return _used == 0; }

	// Insert Connexion object, return false if already present or out of memory
	bool Insert(T *conn);

	// Remove Connexion object from list if it exists
	bool Remove(T *conn);

	// Begin iterating through list
	bool Begin(CollexionIterator<T> &iter);

	// Iterate
	bool Next(CollexionIterator<T> &iter);
};


//// sphynx::CollexionIterator

template<class T>
class CollexionIterator
{
	static const u32 COLLIDE_MASK = 0x80000000;
	static const u32 KILL_MASK = 0x40000000;
	static const u32 NEXT_MASK = 0x3fffffff;

public:
	CollexionElement<T> *_element;
	T *_conn;

public:
	CAT_INLINE T *Get() throw() { return _conn; }
	CAT_INLINE T *operator->() throw() { return _conn; }
	CAT_INLINE T &operator*() throw() { return *_conn; }
	CAT_INLINE operator T*() { return _conn; }
};


//// sphynx::Collexion

template<class T>
bool Collexion<T>::DoubleTable()
{
	u32 new_allocated = _allocated << 1;
	if (new_allocated < MIN_ALLOCATED) new_allocated = MIN_ALLOCATED;

	u32 new_bytes = sizeof(CollexionElement<T>) * new_allocated;
	CollexionElement<T> *new_table = reinterpret_cast<CollexionElement<T> *>(
		RegionAllocator::ii->Acquire(new_bytes) );
	if (!new_table) return false;

	CAT_CLR(new_table, new_bytes);

	u32 new_first = 0;

	CollexionElement<T> *old_table = _table;
	if (old_table)
	{
		// For each entry in the old table,
		u32 ii = _first;
		u32 mask = _allocated - 1;

		while (ii)
		{
			CollexionElement<T> *oe = &old_table[ii - 1];
			u32 key = oe->hash & mask;

			// While collisions occur,
			while (new_table[key].conn)
			{
				// Mark collision
				new_table[key].next |= COLLIDE_MASK;

				// Walk collision list
				key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & mask;
			}

			// Fill new table element
			new_table[key].conn = oe->conn;
			new_table[key].hash = oe->hash;
			new_table[key].next |= new_first;

			// Link new element to new list
			new_first = key + 1;

			// Get next old table entry
			ii = oe->next & NEXT_MASK;
		}

		RegionAllocator::ii->Release(old_table);
	}

	_table = new_table;
	_allocated = new_allocated;
	_first = new_first;
	return true;
}

template<class T>
Collexion<T>::~Collexion()
{
	// If table doesn't exist, return
	if (!_table) return;

	// For each allocated table entry,
	for (u32 ii = 0; ii < _allocated; ++ii)
	{
		// Get Connexion object
		T *conn = _table[ii].conn;

		// If object is valid, release it
		if (conn) conn->ReleaseRef();
	}

	// Release table memory
	RegionAllocator::ii->Release(_table);
}

template<class T>
bool Collexion<T>::Insert(T *conn)
{
	u32 hash = HashPtr(conn);

	AutoMutex lock(_lock);

	// If more than half of the table will be used,
	if (_used >= (_allocated >> 1))
	{
		// Double the size of the table (O(1) allocation pattern)
		if (!DoubleTable())
		{
			// On growth failure, return false
			return false;
		}
	}

	// Mask off high bits to make table key from hash
	u32 mask = _allocated - 1;
	u32 key = hash & mask;

	// While empty table entry not found,
	while (_table[key].conn)
	{
		// If Connexion object is already in the table,
		if (_table[key].conn == conn)
		{
			// Return false on duplicate
			return false;
		}

		// Mark as a collision
		_table[key].next |= COLLIDE_MASK;

		// Walk collision list
		key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & mask;
	}

	_table[key].conn = conn;
	_table[key].hash = hash;
	_table[key].next |= _first;

	_first = key + 1;
	++_used;
	return true;
}

template<class T>
bool Collexion<T>::Remove(T *conn)
{
	u32 hash = HashPtr(conn);

	AutoMutex lock(_lock);

	// Mask off high bits to make table key from hash
	u32 mask = _allocated - 1;
	u32 key = hash & mask;

	// While target table entry not found,
	for (;;)
	{
		// If target was found,
		if (_table[key].conn == conn)
		{
			// Mark it killed
			_table[key].next |= KILL_MASK;

			// Return success
			return true;
		}

		if (0 == (_table[key].next & COLLIDE_MASK))
		{
			break; // End of collision list
		}

		// Walk collision list
		key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & mask;
	}

	// Return failure: not found
	return false;
}

template<class T>
bool Collexion<T>::Begin(CollexionIterator<T> &iter)
{
	AutoMutex lock(_lock);

	if (!_first)
	{
		iter._element = 0;
		iter._conn = 0;

		return false;
	}

	iter._element = &_table[_first];
	iter._conn = iter._element->conn;

	return true;
}

template<class T>
bool Collexion<T>::Next(CollexionIterator<T> &iter)
{
	if (!iter._element) return false;

	AutoMutex lock(_lock);

	u32 next = iter._element->next & NEXT_MASK;

	if (!next)
	{
		iter._conn = 0;
		return false;
	}

	iter._element = &_table[next - 1];
	iter._conn = iter._element->conn;

	return true;
}


//// sphynx::Map

class Map
{
protected:
	CAT_INLINE u32 hash_addr(const NetAddr &addr, u32 salt);
	CAT_INLINE u32 next_collision_key(u32 key);

public:
	struct Slot
	{
		Connexion *connection;
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

	Connexion *Lookup(const NetAddr &addr);

	// May return false if network address in Connexion is already in the map.
	// This averts a potential race condition but should never happen during
	// normal operation, so the Connexion allocation by caller won't be wasted.
	bool Insert(Connexion *conn);

	// Destroy a list described by the 'next' member of Slot
	void DestroyList(Map::Slot *kill_list);

	void Tick(ThreadPoolLocalStorage *tls);
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
	void OnWrite(u32 bytes) {}
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
	CAT_INLINE void Tick(ThreadPoolLocalStorage *tls);
	bool ThreadFunction(void *param);
};


//// sphynx::Server

class Server : public UDPEndpoint
{
public:
	Server();
	virtual ~Server();

	bool StartServer(ThreadPoolLocalStorage *tls, Port port, u8 *public_key, int public_bytes, u8 *private_key, int private_bytes, const char *session_key);

	u32 GetTotalPopulation();

	static bool GenerateKeyPair(ThreadPoolLocalStorage *tls, const char *public_key_file,
		const char *private_key_file, u8 *public_key,
		int public_bytes, u8 *private_key, int private_bytes);

private:
	static const int SESSION_KEY_BYTES = 32;
	char _session_key[SESSION_KEY_BYTES];

	Port _server_port;
	Map _conn_map;

	CookieJar _cookie_jar;
	KeyAgreementResponder _key_agreement_responder;
	u8 _public_key[PUBLIC_KEY_BYTES];

	static const int WORKER_LIMIT = 32; // Maximum number of workers
	ServerWorker **_workers;
	int _worker_count;

	ServerTimer **_timers;
	int _timer_count;

private:
	ServerWorker *FindLeastPopulatedPort();

	void OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();

protected:
	// Must return a new instance of your Connexion derivation
	virtual Connexion *NewConnexion() = 0;

	// IP address filter: Return true to allow the connection to be made
	virtual bool AcceptNewConnexion(const NetAddr &src) = 0;
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_SERVER_HPP
