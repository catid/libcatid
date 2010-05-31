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

#include <cat/net/SphynxServer.hpp>
#include <cat/port/AlignedAlloc.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <cat/io/Settings.hpp>
#include <cat/io/Base64.hpp>
#include <cat/time/Clock.hpp>
#include <cat/hash/MurmurHash2.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/tunnel/KeyMaker.hpp>
#include <fstream>
using namespace std;
using namespace cat;
using namespace sphynx;


static CAT_INLINE u32 hash_addr_iponly(const NetAddr &addr, u32 salt)
{
	u32 key;

	// If address is IPv6,
	if (addr.Is6())
	{
		// Hash first 64 bits of 128-bit address to 32 bits
		// Right now the last 64 is easy to change if you actually have an IPv6 address
		key = MurmurHash32(addr.GetIP6(), (addr.CanDemoteTo4() ? NetAddr::IP6_BYTES : NetAddr::IP6_BYTES/2), salt);
	}
	else // assuming IPv4 and address is not invalid
	{
		key = addr.GetIP4();

		// Thomas Wang's integer hash function
		// http://www.cris.com/~Ttwang/tech/inthash.htm
		key = (key ^ 61) ^ (key >> 16);
		key = key + (key << 3);
		key = key ^ (key >> 4) ^ salt;
		key = key * 0x27d4eb2d;
		key = key ^ (key >> 15);
	}

	return key & HASH_TABLE_MASK;
}

static CAT_INLINE u32 hash_addr(const NetAddr &addr, u32 salt)
{
	u32 key;

	// If address is IPv6,
	if (addr.Is6())
	{
		// Hash 128-bit address to 32 bits
		key = MurmurHash32(addr.GetIP6(), NetAddr::IP6_BYTES, salt);
	}
	else // assuming IPv4 and address is not invalid
	{
		key = addr.GetIP4();

		// Thomas Wang's integer hash function
		// http://www.cris.com/~Ttwang/tech/inthash.htm
		key = (key ^ 61) ^ (key >> 16);
		key = key + (key << 3);
		key = key ^ (key >> 4) ^ salt;
		key = key * 0x27d4eb2d;
		key = key ^ (key >> 15);
	}

	// Hide this from the client-side to prevent users from generating
	// hash table collisions by changing their port number.
	const u32 SECRET_CONSTANT = 104729; // 1,000th prime number

	// Map 16-bit port 1:1 to a random-looking number
	key += (u32)addr.GetPort() * (SECRET_CONSTANT*4 + 1);

	return key & HASH_TABLE_MASK;
}


//// Connexion

Connexion::Connexion()
	: ThreadRefObject(REFOBJ_PRIO_0+2)
{
	_destroyed = 0;
	_seen_encrypted = false;
}

void Connexion::Destroy()
{
	if (Atomic::Set(&_destroyed, 1) == 0)
	{
		TransportDisconnected();
		OnDestroy();
	}
}

bool Connexion::Tick(ThreadPoolLocalStorage *tls, u32 now)
{
	// If no packets have been received,
	if ((s32)(now - _last_recv_tsc) >= TIMEOUT_DISCONNECT)
	{
		PostDisconnect();
		Destroy();
		return false;
	}
	else
	{
		TickTransport(tls, now);
	}

	OnTick(tls, now);

	return true;
}

void Connexion::OnRawData(ThreadPoolLocalStorage *tls, u8 *data, u32 bytes)
{
	u32 buf_bytes = bytes;

	// If packet is valid,
	if (_auth_enc.Decrypt(data, buf_bytes))
	{
		_last_recv_tsc = Clock::msec_fast();

		// Pass it to the transport layer
		OnDatagram(tls, data, buf_bytes);
	}
	else
	{
		WARN("Server") << "Ignoring invalid encrypted data";
	}
}

bool Connexion::PostPacket(u8 *buffer, u32 buf_bytes, u32 msg_bytes, u32 skip_bytes)
{
	buf_bytes -= skip_bytes;
	msg_bytes -= skip_bytes;

	if (!_auth_enc.Encrypt(buffer + skip_bytes, buf_bytes, msg_bytes))
	{
		WARN("Server") << "Encryption failure while sending packet";
		AsyncBuffer::Release(buffer);
		return false;
	}

	return _server_worker->Post(_client_addr, buffer, msg_bytes, skip_bytes);
}


//// Map

Map::Map()
{
	CAT_OBJCLR(_table);
}

Map::~Map()
{
	//WARN("Destroy") << "Killing Map";
}

void Map::Initialize(FortunaOutput *csprng)
{
	_hash_salt = csprng->Generate();
}

Connexion *Map::Lookup(u32 key)
{
	if (key >= HASH_TABLE_SIZE) return 0;

	AutoReadLock lock(_table_lock);

	Connexion *conn = _table[key].connection;

	if (conn)
	{
		conn->AddRef();

		return conn;
	}

	return 0;
}

Connexion *Map::Lookup(const NetAddr &addr)
{
	// Hash IP:port:salt to get the hash table key
	u32 key = hash_addr(addr, _hash_salt);

	AutoReadLock lock(_table_lock);

	// Forever,
	for (;;)
	{
		// Grab the slot
		Slot *slot = &_table[key];

		Connexion *conn = slot->connection;

		// If the slot is used and the user address matches,
		if (conn && conn->_client_addr == addr)
		{
			conn->AddRef();

			return conn;
		}
		else
		{
			// If the slot indicates a collision,
			if (slot->collision)
			{
				// Calculate next collision key
				key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & HASH_TABLE_MASK;

				// Loop around and process the next slot in the collision list
			}
			else
			{
				// Reached end of collision list, so the address was not found in the table
				return 0;
			}
		}
	}

	// Never gets here
	return 0;
}

/*
	Insertion is only done from a single thread, so it is guaranteed
	that the address does not already exist in the hash table.
*/
bool Map::Insert(Connexion *conn)
{
	// Hash IP:port:salt to get the hash table key
	u32 key = hash_addr(conn->_client_addr, _hash_salt);

	// Grab the slot
	Slot *slot = &_table[key];

	AutoWriteLock lock(_table_lock);

	// While collision keys are marked used,
	while (slot->connection)
	{
		// If client is already connected,
		if (slot->connection->_client_addr == conn->_client_addr)
			return false;

		// Set flag for collision
		slot->collision = true;

		// Iterate to next collision key
		key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & HASH_TABLE_MASK;
		slot = &_table[key];

		// NOTE: This will loop forever if every table key is marked used
	}

	// Mark used
	slot->connection = conn;
	conn->_key = key;

	// Insert into ServerWorker
	conn->_server_worker->IncrementPopulation();
	conn->_server_worker->_server_timer->InsertSlot(slot);

	return true;
}

// Destroy a list described by the 'next' member of Slot
void Map::DestroyList(Map::Slot *kill_list)
{
	Connexion *delete_head = 0;

	AutoWriteLock lock(_table_lock);

	// For each slot to kill,
	for (Map::Slot *slot = kill_list; slot; slot = slot->next)
	{
		Connexion *conn = slot->connection;

		if (conn)
		{
			conn->_next_delete = delete_head;
			delete_head = conn;

			slot->connection = 0;

			// If at a leaf in the collision list,
			if (!slot->collision)
			{
				// Find key for this slot
				u32 key = (u32)(slot - _table) / sizeof(Map::Slot);

				// Unset collision flags until first filled entry is found
				do 
				{
					// Roll backwards
					key = ((key + COLLISION_INCRINVERSE) * COLLISION_MULTINVERSE) & HASH_TABLE_MASK;

					// If collision list is done,
					if (!_table[key].collision)
						break;

					// Remove collision flag
					_table[key].collision = false;

				} while (!_table[key].connection);
			}
		}
	}

	lock.Release();

	// For each connection object to delete,
	for (Connexion *next, *conn = delete_head; conn; conn = next)
	{
		next = conn->_next_delete;

		INANE("ServerMap") << "Deleting connection " << conn;

		conn->_server_worker->DecrementPopulation();

		conn->ReleaseRef();
	}
}


//// FloodGuard

FloodGuard::FloodGuard()
{
	CAT_OBJCLR(_bins);
}

FloodGuard::~FloodGuard()
{
	//WARN("Destroy") << "Killing FloodGuard";
}

void FloodGuard::Initialize(FortunaOutput *csprng)
{
	_hash_salt = csprng->Generate();
}

u32 FloodGuard::TryNewConnexion(const NetAddr &addr)
{
	u32 flood_key = hash_addr_iponly(addr, _hash_salt);

	AutoMutex lock(_lock);

	if (_bins[flood_key] >= CONNECTION_FLOOD_THRESHOLD)
		return FLOOD_DETECTED;

	_bins[flood_key]++;

	return flood_key;
}

void FloodGuard::DeleteConnexion(u32 flood_key)
{
	AutoMutex lock(_lock);

	_bins[flood_key]--;
}

void FloodGuard::DestroyList(Map::Slot *kill_list)
{
	AutoMutex lock(_lock);

	// For each slot to kill,
	for (Map::Slot *slot = kill_list; slot; slot = slot->next)
	{
		Connexion *conn = slot->connection;

		if (conn)
		{
			u32 flood_key = conn->GetFloodKey();

			if (flood_key < HASH_TABLE_SIZE) _bins[flood_key]--;
		}
	}
}


//// ServerWorker

ServerWorker::ServerWorker(Map *conn_map, ServerTimer *server_timer)
	: UDPEndpoint(REFOBJ_PRIO_0+1)
{
	_server_timer = server_timer;
	_conn_map = conn_map;

	_session_count = 0;
}

ServerWorker::~ServerWorker()
{
	//WARN("Destroy") << "Killing Worker";
}

void ServerWorker::IncrementPopulation()
{
	Atomic::Add(&_session_count, 1);
}

void ServerWorker::DecrementPopulation()
{
	Atomic::Add(&_session_count, -1);
}

void ServerWorker::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{
	// Look up an existing connection for this source address
	AutoRef<Connexion> conn = _conn_map->Lookup(src);

	if (conn && conn->IsValid() && conn->_server_worker == this)
	{
		conn->OnRawData(tls, data, bytes);
	}
}

void ServerWorker::OnClose()
{

}


//// ServerTimer

ServerTimer::ServerTimer(Map *conn_map, FloodGuard *flood_guard, ServerWorker **workers, int worker_count)
{
	_conn_map = conn_map;
	_flood_guard = flood_guard;

	_workers = workers;
	_worker_count = worker_count;

	_insert_head = 0;
	_active_head = 0;

	// If unable to start the clock thread,
	if (!StartThread())
	{
		WARN("ServerTimer") << "Failed to initialize: Unable to start timer thread. LastError=" << GetLastError();

		// Note failure
		_worker_count = 0;
	}
}

ServerTimer::~ServerTimer()
{
	//WARN("Destroy") << "Killing Timer";

	_kill_flag.Set();

	if (!WaitForThread(TIMER_THREAD_KILL_TIMEOUT))
		AbortThread();

	Map::Slot *slot, *next;

	// Free all active connection objects
	for (slot = _active_head; slot; slot = next)
	{
		next = slot->next;

		ThreadRefObject::SafeRelease(slot->connection);
	}

	// Free all recently inserted connection objects
	for (slot = _insert_head; slot; slot = next)
	{
		next = slot->next;

		ThreadRefObject::SafeRelease(slot->connection);
	}
}

void ServerTimer::InsertSlot(Map::Slot *slot)
{
	AutoMutex lock(_insert_lock);

	slot->next = _insert_head;
	_insert_head = slot;
}

void ServerTimer::Tick(ThreadPoolLocalStorage *tls)
{
	u32 now = Clock::msec();

	Map::Slot *active_head = _active_head, *insert_head = 0;

	// Grab and reset the insert head
	if (_insert_head)
	{
		AutoMutex lock(_insert_lock);

		insert_head = _insert_head;
		_insert_head = 0;
	}

	// For each recently inserted slot,
	for (Map::Slot *next, *slot = insert_head; slot; slot = next)
	{
		next = slot->next;

		//INANE("ServerTimer") << "Linking new connection into active list";

		// Link into active list
		slot->next = active_head;
		active_head = slot;
	}

	Map::Slot *kill_list = 0, *last = 0;

	// For each active slot,
	for (Map::Slot *next, *slot = active_head; slot; slot = next)
	{
		next = slot->next;

		Connexion *conn = slot->connection;

		if (!conn || !conn->IsValid() || !conn->Tick(tls, now))
		{
			// Unlink from active list
			if (last) last->next = next;
			else active_head = next;

			//INANE("ServerTimer") << "Relinking dead connection into kill list";

			// Link into kill list
			slot->next = kill_list;
			kill_list = slot;
		}
		else
		{
			last = slot;
		}
	}

	// If some of the slots need to be killed,
	if (kill_list)
	{
		_flood_guard->DestroyList(kill_list);
		_conn_map->DestroyList(kill_list);
	}

	_active_head = active_head;
}

bool ServerTimer::ThreadFunction(void *)
{
	ThreadPoolLocalStorage tls;

	if (!tls.Valid())
	{
		WARN("ServerTimer") << "Unable to create thread pool local storage";
		return false;
	}

	// While quit signal is not flagged,
	while (!_kill_flag.Wait(Transport::TICK_RATE))
	{
		Tick(&tls);
	}

	return true;
}


//// Server

Server::Server()
	: UDPEndpoint(REFOBJ_PRIO_0)
{
	_workers = 0;
	_worker_count = 0;

	_timers = 0;
	_timer_count = 0;
}

Server::~Server()
{
	//WARN("Destroy") << "Killing Server";

	// Delete timer objects
	if (_timers)
	{
		for (int ii = 0; ii < _timer_count; ++ii)
		{
			ServerTimer *timer = _timers[ii];

			if (timer)
			{
				delete timer;
			}
		}

		delete[] _timers;
	}

	// Delete worker object array
	if (_workers)
	{
		for (int ii = 0; ii < _worker_count; ++ii)
		{
			ThreadRefObject::SafeRelease(_workers[ii]);
		}

		delete[] _workers;
	}
}

bool Server::StartServer(ThreadPoolLocalStorage *tls, Port port, u8 *public_key, int public_bytes, u8 *private_key, int private_bytes, const char *session_key)
{
	// If objects were not created,
	if (!tls->Valid())
	{
		WARN("Server") << "Failed to initialize: Unable to create thread local storage";
		return false;
	}

	// Allocate worker array
	_worker_count = ThreadPool::ref()->GetProcessorCount() * 4;
	if (_worker_count > WORKER_LIMIT) _worker_count = WORKER_LIMIT;
	if (_worker_count < 1) _worker_count = 1;

	if (_workers) delete[] _workers;

	_workers = new ServerWorker*[_worker_count];
	if (!_workers)
	{
		WARN("Server") << "Failed to initialize: Unable to allocate " << _worker_count << " workers";
		return false;
	}

	for (int ii = 0; ii < _worker_count; ++ii)
		_workers[ii] = 0;

	// Allocate timer array
	_timer_count = _worker_count / 8;
	if (_timer_count < 1) _timer_count = 1;

	if (_timers) delete[] _timers;

	_timers = new ServerTimer*[_timer_count];
	if (!_timers)
	{
		WARN("Server") << "Failed to initialize: Unable to allocate " << _timer_count << " timers";
		return false;
	}

	for (int ii = 0; ii < _timer_count; ++ii)
		_timers[ii] = 0;

	// Seed components
	_cookie_jar.Initialize(tls->csprng);
	_conn_map.Initialize(tls->csprng);
	_flood_guard.Initialize(tls->csprng);

	// Initialize key agreement responder
	if (!_key_agreement_responder.Initialize(tls->math, tls->csprng,
											 public_key, public_bytes,
											 private_key, private_bytes))
	{
		WARN("Server") << "Failed to initialize: Key pair is invalid";
		return false;
	}

	// Copy session key
	CAT_STRNCPY(_session_key, session_key, SESSION_KEY_BYTES);

	// Copy public key
	memcpy(_public_key, public_key, sizeof(_public_key));

	// Get SupportIPv6 flag from settings
	bool only_ipv4 = Settings::ii->getInt("Sphynx.Server.SupportIPv6", 0) == 0;

	// Get kernel receive buffer size
	int kernelReceiveBufferBytes = Settings::ii->getInt("Sphynx.Server.KernelReceiveBuffer", 8000000);

	// Attempt to bind to the server port
	_server_port = port;
	if (!Bind(only_ipv4, port, true, kernelReceiveBufferBytes))
	{
		WARN("Server") << "Failed to initialize: Unable to bind handshake port "
			<< port << ". " << SocketGetLastErrorString();
		return false;
	}

	bool success = true;

	int workers_per_timer = _worker_count / _timer_count;

	// For each timer,
	for (int ii = 0; ii < _timer_count; ++ii)
	{
		int first = ii * workers_per_timer;
		int range = workers_per_timer;

		if (first + range > _worker_count)
		{
			range = _worker_count - first;
		}

		ServerTimer *timer = new ServerTimer(&_conn_map, &_flood_guard, &_workers[first], range);

		_timers[ii] = timer;

		if (!timer || !timer->Valid())
		{
			WARN("Server") << "Failed to initialize: Unable to create server timer object";

			success = false;
		}
	}

	// For each data port,
	for (int ii = 0; ii < _worker_count; ++ii)
	{
		// Create a new session endpoint
		ServerWorker *worker = new ServerWorker(&_conn_map, _timers[ii / workers_per_timer]);

		// Add a ref right away to avoid deletion until server is destroyed
		worker->AddRef();

		// Store it whether it is null or not
		_workers[ii] = worker;

		Port worker_port = port + ii + 1;

		// If allocation or bind failed, report failure after done
		if (!worker || !worker->Bind(only_ipv4, worker_port, true, kernelReceiveBufferBytes))
		{
			WARN("Server") << "Failed to initialize: Unable to bind to data port " << worker_port << ": "
				<< SocketGetLastErrorString();

			// Note failure
			success = false;
		}
	}

	return success;
}

Connexion *Server::Lookup(u32 key)
{
	return _conn_map.Lookup(key);
}

ServerWorker *Server::FindLeastPopulatedPort()
{
	// Search through the list of session ports and find the lowest session count
	u32 best_count = (u32)~0;
	ServerWorker *best_port = 0;

	// For each port,
	for (int ii = 0; ii < _worker_count; ++ii)
	{
		// Grab the session count for this port
		ServerWorker *port = _workers[ii];
		u32 count = port->GetPopulation();

		// If we found a lower session count,
		if (count < best_count)
		{
			// Use this one instead
			best_count = count;
			best_port = port;
		}
	}

	return best_port;
}

u32 Server::GetTotalPopulation()
{
	u32 population = 0;

	// For each port,
	for (int ii = 0; ii < _worker_count; ++ii)
	{
		// Accumulate population
		population += _workers[ii]->GetPopulation();
	}

	return population;
}

void Server::PostConnectionCookie(const NetAddr &dest)
{
	u8 *pkt = AsyncBuffer::Acquire(S2C_COOKIE_LEN);

	// Verify that post buffer could be allocated
	if (!pkt)
	{
		WARN("Server") << "Unable to post connection cookie: Unable to allocate post buffer";
		return;
	}

	// Construct packet
	pkt[0] = S2C_COOKIE;

	// Endianness does not matter since we will read it back the same way
	u32 *pkt_cookie = reinterpret_cast<u32*>( pkt + 1 );
	*pkt_cookie = dest.Is6() ? _cookie_jar.Generate(&dest, sizeof(dest))
							 : _cookie_jar.Generate(dest.GetIP4(), dest.GetPort());

	// Attempt to post the packet, ignoring failures
	Post(dest, pkt, S2C_COOKIE_LEN);
}

void Server::PostConnectionError(const NetAddr &dest, HandshakeError err)
{
	u8 *pkt = AsyncBuffer::Acquire(S2C_ERROR_LEN);

	// Verify that post buffer could be allocated
	if (!pkt)
	{
		WARN("Server") << "Unable to post connection error: Unable to allocate post buffer";
		return;
	}

	// Construct packet
	pkt[0] = S2C_ERROR;
	pkt[1] = (u8)(err);

	// Post packet without checking for errors
	Post(dest, pkt, S2C_ERROR_LEN);
}

void Server::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{
	if (bytes == C2S_HELLO_LEN && data[0] == C2S_HELLO)
	{
		// If magic does not match,
		u32 *protocol_magic = reinterpret_cast<u32*>( data + 1 );
		if (*protocol_magic != getLE(PROTOCOL_MAGIC))
		{
			WARN("Server") << "Ignoring hello: Bad magic";
			return;
		}

		// Verify public key
		if (!SecureEqual(data + 1 + 4, _public_key, PUBLIC_KEY_BYTES))
		{
			WARN("Server") << "Failing hello: Client public key does not match";
			PostConnectionError(src, ERR_WRONG_KEY);
			return;
		}

		WARN("Server") << "Accepted hello and posted cookie";

		PostConnectionCookie(src);
	}
	else if (bytes == C2S_CHALLENGE_LEN && data[0] == C2S_CHALLENGE)
	{
		// If magic does not match,
		u32 *protocol_magic = reinterpret_cast<u32*>( data + 1 );
		if (*protocol_magic != getLE(PROTOCOL_MAGIC))
		{
			WARN("Server") << "Ignoring challenge: Bad magic";
			return;
		}

		// If cookie is invalid, ignore packet
		u32 *cookie = reinterpret_cast<u32*>( data + 1 + 4 );
		bool good_cookie = src.Is6() ?
			_cookie_jar.Verify(&src, sizeof(src), *cookie) :
			_cookie_jar.Verify(src.GetIP4(), src.GetPort(), *cookie);

		if (!good_cookie)
		{
			WARN("Server") << "Ignoring challenge: Stale cookie";
			return;
		}

		u8 *challenge = data + 1 + 4 + 4;

		// They took the time to get the cookie right, might as well check if we know them
		AutoRef<Connexion> conn = _conn_map.Lookup(src);

		// If connection already exists,
		if (conn)
		{
			// If the connection exists but has recently been deleted,
			// If we have seen the first encrypted packet already,
			// If we the challenge does not match the previous one,
			if (!conn->IsValid() || conn->_seen_encrypted ||
				!SecureEqual(conn->_first_challenge, challenge, CHALLENGE_BYTES))
			{
				WARN("Server") << "Ignoring challenge: Replay challenge in bad state";
				return;
			}

			u8 *pkt = AsyncBuffer::Acquire(S2C_ANSWER_LEN);

			// Verify that post buffer could be allocated
			if (!pkt)
			{
				WARN("Server") << "Ignoring challenge: Unable to allocate post buffer";
				return;
			}

			// Construct packet
			pkt[0] = S2C_ANSWER;

			Port *pkt_port = reinterpret_cast<Port*>( pkt + 1 );
			*pkt_port = getLE(conn->_server_worker->GetPort());

			u8 *pkt_answer = pkt + 1 + sizeof(Port);
			memcpy(pkt_answer, conn->_cached_answer, ANSWER_BYTES);

			// Post packet without checking for errors
			Post(src, pkt, S2C_ANSWER_LEN);

			INANE("Server") << "Replayed lost answer to client challenge";
		}
		else // Connexion did not exist yet:
		{
			// If server is overpopulated,
			if (GetTotalPopulation() >= MAX_POPULATION)
			{
				WARN("Server") << "Ignoring challenge: Server is full";
				PostConnectionError(src, ERR_SERVER_FULL);
				return;
			}

			// If flood is detected,
			u32 flood_key = _flood_guard.TryNewConnexion(src);
			if (flood_key == FloodGuard::FLOOD_DETECTED)
			{
				WARN("Server") << "Ignoring challenge: Flood detected";
				PostConnectionError(src, ERR_FLOOD_DETECTED);
				return;
			}

			Skein key_hash;

			u8 *pkt = AsyncBuffer::Acquire(S2C_ANSWER_LEN);

			// Verify that post buffer could be allocated
			if (!pkt)
			{
				WARN("Server") << "Ignoring challenge: Unable to allocate post buffer";
			}
			// If challenge is invalid,
			else if (!_key_agreement_responder.ProcessChallenge(tls->math, tls->csprng,
														   challenge, CHALLENGE_BYTES,
														   pkt + 1 + 2, ANSWER_BYTES, &key_hash))
			{
				WARN("Server") << "Ignoring challenge: Invalid";

				pkt[0] = S2C_ERROR;
				pkt[1] = (u8)(ERR_TAMPERING);
				Post(src, pkt, S2C_ERROR_LEN);
			}
			// If out of memory for Connexion objects,
			else if (!(conn = NewConnexion()))
			{
				WARN("Server") << "Out of memory: Unable to allocate new Connexion";

				pkt[0] = S2C_ERROR;
				pkt[1] = (u8)(ERR_SERVER_ERROR);
				Post(src, pkt, S2C_ERROR_LEN);
			}
			// If unable to key encryption from session key,
			else if (!_key_agreement_responder.KeyEncryption(&key_hash, &conn->_auth_enc, _session_key))
			{
				WARN("Server") << "Ignoring challenge: Unable to key encryption";

				pkt[0] = S2C_ERROR;
				pkt[1] = (u8)(ERR_SERVER_ERROR);
				Post(src, pkt, S2C_ERROR_LEN);
			}
			else // Good so far:
			{
				// Find the least populated port
				ServerWorker *server_worker = FindLeastPopulatedPort();
				Port server_port = server_worker->GetPort();

				// Construct packet 3
				pkt[0] = S2C_ANSWER;

				Port *pkt_port = reinterpret_cast<Port*>( pkt + 1 );
				*pkt_port = getLE(server_port);

				// Initialize Connexion object
				memcpy(conn->_first_challenge, challenge, CHALLENGE_BYTES);
				memcpy(conn->_cached_answer, pkt + 1 + 2, ANSWER_BYTES);
				conn->_client_addr = src;
				conn->_server_worker = server_worker;
				conn->_last_recv_tsc = Clock::msec_fast();
				conn->_flood_key = flood_key;
				WARN("Flood Key") << flood_key;
				conn->InitializePayloadBytes(Is6());

				// If packet post fails,
				if (!Post(src, pkt, S2C_ANSWER_LEN))
				{
					WARN("Server") << "Ignoring challenge: Unable to post packet";
				}
				// If hash key could not be inserted,
				else if (!_conn_map.Insert(conn))
				{
					WARN("Server") << "Ignoring challenge: Same client already connected (race condition)";
				}
				else
				{
					WARN("Server") << "Accepted challenge and posted answer.  Client connected";

					conn->OnConnect(tls);

					conn.Forget();

					return;
				}
			}

			_flood_guard.DeleteConnexion(flood_key);
		}
	}
}

void Server::OnWrite(u32 bytes)
{

}

void Server::OnClose()
{
	WARN("Server") << "CLOSED";
}


bool Server::GenerateKeyPair(ThreadPoolLocalStorage *tls, const char *public_key_file,
							 const char *private_key_file, u8 *public_key,
							 int public_bytes, u8 *private_key, int private_bytes)
{
	if (PUBLIC_KEY_BYTES != public_bytes || PRIVATE_KEY_BYTES != private_bytes)
		return false;

	// Open server key file (if possible)
	MMapFile mmf(private_key_file);

	// If the file was found and of the right size,
	if (mmf.good() && mmf.remaining() == PUBLIC_KEY_BYTES + PRIVATE_KEY_BYTES)
	{
		u8 *cp_public_key = reinterpret_cast<u8*>( mmf.read(PUBLIC_KEY_BYTES) );
		u8 *cp_private_key = reinterpret_cast<u8*>( mmf.read(PRIVATE_KEY_BYTES) );

		// Remember the public key so we can report it to connecting users
		memcpy(public_key, cp_public_key, PUBLIC_KEY_BYTES);
		memcpy(private_key, cp_private_key, PRIVATE_KEY_BYTES);
	}
	else
	{
		INFO("KeyGenerator") << "Key file not present.  Creating a new key pair...";

		// Say hello to my little friend
		KeyMaker Bob;

		// Ask Bob to generate a key pair for the server
		if (!Bob.GenerateKeyPair(tls->math, tls->csprng,
			public_key, PUBLIC_KEY_BYTES,
			private_key, PRIVATE_KEY_BYTES))
		{
			WARN("KeyGenerator") << "Failed to initialize: Unable to generate key pair";
			return false;
		}
		else
		{
			// Thanks Bob!  Now, write the key file
			ofstream private_keyfile(private_key_file, ios_base::out | ios_base::binary);
			ofstream public_keyfile(public_key_file, ios_base::out);

			// If the key file was NOT successfully opened in output mode,
			if (public_keyfile.fail() || private_keyfile.fail())
			{
				WARN("KeyGenerator") << "Failed to initialize: Unable to open key file(s) for writing";
				return false;
			}

			// Write public key file in Base64 encoding
			WriteBase64(public_key, PUBLIC_KEY_BYTES, public_keyfile);
			public_keyfile.flush();

			// Write private key file
			private_keyfile.write((char*)public_key, PUBLIC_KEY_BYTES);
			private_keyfile.write((char*)private_key, PRIVATE_KEY_BYTES);
			private_keyfile.flush();

			// If the key files were NOT successfully written,
			if (public_keyfile.fail() || private_keyfile.fail())
			{
				WARN("KeyGenerator") << "Failed to initialize: Unable to write key file(s)";
				return false;
			}
		}
	}

	return true;
}
