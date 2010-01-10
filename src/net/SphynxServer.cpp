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

#include <cat/net/SphynxServer.hpp>
#include <cat/port/AlignedAlloc.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <fstream>
using namespace std;
using namespace cat;
using namespace sphynx;


//// Encryption Key Constants

static const char *SERVER_PRIVATE_KEY_FILE = "s_server_private_key.bin";
static const char *SERVER_PUBLIC_KEY_FILE = "u_server_public_key.c";
static const char *SESSION_KEY_NAME = "SphynxSessionKey";


//// Connection

Connection::Connection()
{
	destroyed = 0;
	seen_encrypted = false;
}

void Connection::Destroy()
{
	if (Atomic::Set(&destroyed, 1) == 0)
	{
		OnDestroy();
	}
}

bool Connection::Tick(ThreadPoolLocalStorage *tls, u32 now)
{
	const int DISCONNCT_TIMEOUT = 15000; // 15 seconds

	// If no packets have been received,
	if (now - last_recv_tsc >= DISCONNCT_TIMEOUT)
	{
		Destroy();
		return false;
	}
	else
	{
		TickTransport(tls, now);
	}

	return true;
}

void Connection::OnRawData(u8 *data, u32 bytes)
{
	u32 buf_bytes = bytes;

	// If packet is valid,
	if (auth_enc.Decrypt(data, buf_bytes))
	{
		last_recv_tsc = Clock::msec_fast();

		// Pass it to the transport layer
		OnDatagram(data, buf_bytes);
	}
}

void Connection::OnDestroy()
{

}

void Connection::OnMessage(u8 *msg, u32 bytes)
{

}

bool Connection::PostPacket(u8 *buffer, u32 buf_bytes, u32 msg_bytes)
{
	if (!auth_enc.Encrypt(buffer, buf_bytes, msg_bytes))
	{
		WARN("Server") << "Encryption failure while sending packet";
		ReleasePostBuffer(buffer);
		return false;
	}

	return server_worker->Post(client_addr, buffer, msg_bytes);
}

void Connection::OnDisconnect()
{
	WARN("Server") << "Disconnected by client";
}


//// Map

Map::Map()
{
	// Initialize the hash salt to something that will
	// discourage hash-based DoS attacks against servers
	// running the protocol.
	// TODO: Determine if this needs stronger protection
	_hash_salt = (u32)s32(Clock::usec() * 1000.0);

	CAT_OBJCLR(_table);
}

Map::~Map()
{
	WARN("Destroy") << "Killing Map";
}

u32 Map::hash_addr(const NetAddr &addr, u32 salt)
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
		key = key ^ (key >> 4);
		key = key * 0x27d4eb2d;
		key = key ^ (key >> 15);
	}

	// Hide this from the client-side to prevent users from generating
	// hash table collisions by changing their port number.
	const int SECRET_CONSTANT = 104729; // 1,000th prime number

	// Map 16-bit port 1:1 to a random-looking number
	key += ((u32)addr.GetPort() * (SECRET_CONSTANT*4 + 1)) & 0xffff;

	return key % HASH_TABLE_SIZE;
}

u32 Map::next_collision_key(u32 key)
{
	// LCG with period equal to the table size
	return (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) % HASH_TABLE_SIZE;
}

Connection *Map::GetLock(const NetAddr &addr)
{
	Slot *slot;

	// Hash IP:port:salt to get the hash table key
	u32 key = hash_addr(addr, _hash_salt);

	_table_lock.ReadLock();

	// Forever,
	for (;;)
	{
		// Grab the slot
		slot = &_table[key];

		Connection *conn = slot->connection;

		// If the slot is used and the user address matches,
		if (conn && conn->client_addr == addr)
		{
			return conn;
		}
		else
		{
			// If the slot indicates a collision,
			if (slot->collision)
			{
				// Calculate next collision key
				key = next_collision_key(key);

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

void Map::ReleaseLock()
{
	_table_lock.ReadUnlock();
}

/*
	Insertion is only done from a single thread, so it is guaranteed
	that the address does not already exist in the hash table.
*/
void Map::Insert(Connection *conn)
{
	// Hash IP:port:salt to get the hash table key
	u32 key = hash_addr(conn->client_addr, _hash_salt);

	// Grab the slot
	Slot *slot = &_table[key];

	AutoWriteLock lock(_table_lock);

	// While collision keys are marked used,
	while (slot->connection)
	{
		// Set flag for collision
		slot->collision = true;

		// Iterate to next collision key
		key = next_collision_key(key);
		slot = &_table[key];

		// NOTE: This will loop forever if every table key is marked used
	}

	// Mark used
	slot->connection = conn;

	conn->server_worker->_server_timer->InsertSlot(slot);

	// If collision list continues after this slot,
	if (slot->collision)
	{
		Slot *end_of_list = slot;

		// While collision list continues,
		do
		{
			// Iterate to next collision key
			key = next_collision_key(key);
			slot = &_table[key];

			// If this key is also used,
			if (slot->connection)
			{
				// Remember it as the end of the collision list
				end_of_list = slot;
			}
		} while (slot->collision);

		// Truncate collision list at the detected end of the list
		slot = end_of_list;
		while (slot->collision)
		{
			// Iterate to next collision key
			key = next_collision_key(key);
			slot = &_table[key];
		}
	}
}

// Destroy a list described by the 'next' member of Slot
void Map::DestroyList(Map::Slot *kill_list)
{
	Connection *delete_head = 0;

	AutoWriteLock lock(_table_lock);

	// For each slot to kill,
	for (Map::Slot *slot = kill_list; slot; slot = slot->next)
	{
		Connection *conn = slot->connection;

		if (conn)
		{
			conn->next_delete = delete_head;
			delete_head = conn;

			slot->connection = 0;
		}
	}

	lock.Release();

	// For each connection object to delete,
	for (Connection *next, *conn = delete_head; conn; conn = next)
	{
		next = conn->next_delete;

		INANE("ServerMap") << "Deleting connection " << conn;

		conn->server_worker->DecrementPopulation();

		delete conn;
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
	WARN("Destroy") << "Killing Worker";
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
	Connection *conn = _conn_map->GetLock(src);
	int buf_bytes = bytes;

	// If connection is valid and on the right port,
	if (conn && conn->IsValid() && conn->server_worker == this)
	{
		conn->OnRawData(data, bytes);
	}

	_conn_map->ReleaseLock();
}

void ServerWorker::OnWrite(u32 bytes)
{

}

void ServerWorker::OnClose()
{

}


//// ServerTimer

ServerTimer::ServerTimer(Map *conn_map, ServerWorker **workers, int worker_count)
{
	_conn_map = conn_map;
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
	WARN("Destroy") << "Killing Timer";

	Map::Slot *slot, *next;

	if (!StopThread())
	{
		WARN("ServerTimer") << "Unable to stop timer thread.  Was it started?";
	}

	INANE("ServerTimer") << "Freeing connection objects";

	// Free all active connection objects
	for (slot = _active_head; slot; slot = next)
	{
		next = slot->next;

		if (slot->connection)
		{
			delete slot->connection;
			slot->connection = 0;
		}
	}

	// Free all recently inserted connection objects
	for (slot = _insert_head; slot; slot = next)
	{
		next = slot->next;

		if (slot->connection)
		{
			delete slot->connection;
			slot->connection = 0;
		}
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

	Map::Slot *active_head = _active_head;
	Map::Slot *insert_head = 0;

	// Grab and reset the insert head
	if (_insert_head)
	{
		_insert_lock.Enter();
		insert_head = _insert_head;
		_insert_head = 0;
		_insert_lock.Leave();
	}

	// For each recently inserted slot,
	for (Map::Slot *next, *slot = insert_head; slot; slot = next)
	{
		next = slot->next;

		INANE("ServerTimer") << "Linking new connection into active list";

		// Link into active list
		slot->next = active_head;
		active_head = slot;
	}

	Map::Slot *kill_list = 0;
	Map::Slot *last = 0;

	// For each active slot,
	for (Map::Slot *next, *slot = active_head; slot; slot = next)
	{
		next = slot->next;

		Connection *conn = slot->connection;

		if (!conn || !conn->IsValid() || !conn->Tick(tls, now))
		{
			// Unlink from active list
			if (last) last->next = next;
			else active_head = next;

			INANE("ServerTimer") << "Relinking dead connection into kill list";

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
		_conn_map->DestroyList(kill_list);
	}

	_active_head = active_head;
}

bool ServerTimer::ThreadFunction(void *param)
{
	ThreadPoolLocalStorage tls;

	if (!tls.Valid())
	{
		WARN("ServerTimer") << "Unable to create thread pool local storage";
		return false;
	}

	// While quit signal is not flagged,
	while (WaitForQuitSignal(Transport::TICK_RATE))
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
	WARN("Destroy") << "Killing Server";

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
			ServerWorker *worker = _workers[ii];

			if (worker)
			{
				// Release final ref
				worker->ReleaseRef();
			}
		}

		delete[] _workers;
	}
}

bool Server::Initialize(ThreadPoolLocalStorage *tls, Port port)
{
	// If objects were not created,
	if (!tls->Valid())
	{
		WARN("Server") << "Failed to initialize: Unable to create thread local storage";
		return false;
	}

	// Allocate worker array
	_worker_count = ThreadPool::ref()->GetProcessorCount() * 4;
	if (_worker_count < 1) _worker_count = 1;

	if (_workers) delete[] _workers;

	_workers = new ServerWorker*[_worker_count];
	if (!_workers)
	{
		WARN("Server") << "Failed to initialize: Unable to allocate " << _worker_count << " workers";
		return false;
	}

	// Allocate timer array
	_timer_count = ThreadPool::ref()->GetProcessorCount() / 2;
	if (_timer_count < 1) _timer_count = 1;

	if (_timers) delete[] _timers;

	_timers = new ServerTimer*[_timer_count];
	if (!_timers)
	{
		WARN("Server") << "Failed to initialize: Unable to allocate " << _timer_count << " timers";
		return false;
	}

	// Initialize cookie jar
	_cookie_jar.Initialize(tls->csprng);

	// Open server key file (if possible)
	MMapFile mmf(SERVER_PRIVATE_KEY_FILE);

	// If the file was found and of the right size,
	if (mmf.good() && mmf.remaining() == PUBLIC_KEY_BYTES + PRIVATE_KEY_BYTES)
	{
		u8 *public_key = reinterpret_cast<u8*>( mmf.read(PUBLIC_KEY_BYTES) );
		u8 *private_key = reinterpret_cast<u8*>( mmf.read(PRIVATE_KEY_BYTES) );

		// Remember the public key so we can report it to connecting users
		memcpy(_public_key, public_key, PUBLIC_KEY_BYTES);

		// Initialize key agreement responder
		if (!_key_agreement_responder.Initialize(tls->math, tls->csprng,
												 public_key, PUBLIC_KEY_BYTES,
												 private_key, PRIVATE_KEY_BYTES))
		{
			WARN("Server") << "Failed to initialize: Key from key file is invalid";
			return false;
		}
	}
	else
	{
		INFO("Server") << "Key file not present.  Creating a new key pair...";

		u8 public_key[PUBLIC_KEY_BYTES];
		u8 private_key[PRIVATE_KEY_BYTES];

		// Say hello to my little friend
		KeyMaker Bob;

		// Ask Bob to generate a key pair for the server
		if (!Bob.GenerateKeyPair(tls->math, tls->csprng,
								 public_key, PUBLIC_KEY_BYTES,
								 private_key, PRIVATE_KEY_BYTES))
		{
			WARN("Server") << "Failed to initialize: Unable to generate key pair";
			return false;
		}
		else
		{
			// Thanks Bob!  Now, write the key file
			ofstream private_keyfile(SERVER_PRIVATE_KEY_FILE, ios_base::out | ios_base::binary);
			ofstream public_keyfile(SERVER_PUBLIC_KEY_FILE, ios_base::out);

			// If the key file was successfully opened in output mode,
			if (public_keyfile.fail() || private_keyfile.fail())
			{
				WARN("Server") << "Failed to initialize: Unable to open key file(s) for writing";
				return false;
			}

			// Write private keyfile contents
			public_keyfile << "unsigned char SERVER_PUBLIC_KEY[" << PUBLIC_KEY_BYTES << "] = {" << endl;
			for (int ii = 0; ii < PUBLIC_KEY_BYTES; ++ii)
			{
				if (ii)
				{
					public_keyfile << ",";
					if (ii % 16 == 0) public_keyfile << endl;
				}

				public_keyfile << (u32)public_key[ii];
			}
			public_keyfile << endl << "};" << endl;
			public_keyfile.flush();

			// Write public keyfile contents
			private_keyfile.write((char*)public_key, PUBLIC_KEY_BYTES);
			private_keyfile.write((char*)private_key, PRIVATE_KEY_BYTES);
			private_keyfile.flush();

			// If the key file was successfully written,
			if (public_keyfile.fail() || private_keyfile.fail())
			{
				WARN("Server") << "Failed to initialize: Unable to write key file";
				return false;
			}

			// Remember the public key so we can report it to connecting users
			memcpy(_public_key, public_key, PUBLIC_KEY_BYTES);

			// Initialize key agreement responder
			if (!_key_agreement_responder.Initialize(tls->math, tls->csprng,
													 public_key, PUBLIC_KEY_BYTES,
													 private_key, PRIVATE_KEY_BYTES))
			{
				WARN("Server") << "Failed to initialize: Key we just generated is invalid";
				return false;
			}
		}
	}

	// Attempt to bind to the server port
	_server_port = port;
	if (!Bind(port))
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

		ServerTimer *timer = new ServerTimer(&_conn_map, &_workers[first], range);

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
		if (!worker || !worker->Bind(worker_port))
		{
			WARN("Server") << "Failed to initialize: Unable to bind to data port " << worker_port << ": "
				<< SocketGetLastErrorString();

			// Note failure
			success = false;
		}
	}

	return success;
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

void Server::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{
	// c2s 00 (protocol magic[4])
	if (bytes == 1+4 && data[0] == C2S_HELLO)
	{
		u32 *protocol_magic = reinterpret_cast<u32*>( data + 1 );

		// If magic matches,
		if (*protocol_magic == getLE(PROTOCOL_MAGIC))
		{
			// s2c 01 (cookie[4]) (public key[64])
			u8 *pkt1 = GetPostBuffer(1+4+PUBLIC_KEY_BYTES);

			// If packet buffer could be allocated,
			if (pkt1)
			{
				u32 *pkt1_cookie = reinterpret_cast<u32*>( pkt1 + 1 );
				u8 *pkt1_public_key = pkt1 + 1+4;

				// Construct packet 1
				pkt1[0] = S2C_COOKIE;
				if (src.Is6())
					*pkt1_cookie = _cookie_jar.Generate(&src, sizeof(src));
				else
					*pkt1_cookie = _cookie_jar.Generate(src.GetIP4(), src.GetPort());
				memcpy(pkt1_public_key, _public_key, PUBLIC_KEY_BYTES);

				// Attempt to post the packet, ignoring failures
				Post(src, pkt1, 1+4+PUBLIC_KEY_BYTES);

				INANE("Server") << "Accepted hello and posted cookie";
			}
		}
	}
	// c2s 02 (cookie[4]) (challenge[64])
	else if (bytes == 1+4+CHALLENGE_BYTES && data[0] == C2S_CHALLENGE)
	{
		u32 *cookie = reinterpret_cast<u32*>( data + 1 );
		u8 *challenge = data + 1+4;

		// If cookie is invalid, ignore packet
		bool good_cookie = src.Is6() ?
			_cookie_jar.Verify(&src, sizeof(src), *cookie) :
			_cookie_jar.Verify(src.GetIP4(), src.GetPort(), *cookie);

		if (!good_cookie)
		{
			WARN("Server") << "Ignoring challenge: Stale cookie";
			return;
		}

		// s2c 03 (answer[128]) E{ (server session port[2]) } [13]
		const int PKT3_LEN = 1+2+ANSWER_BYTES;
		u8 *pkt3 = GetPostBuffer(PKT3_LEN);

		// Verify that post buffer could be allocated
		if (!pkt3)
		{
			WARN("Server") << "Ignoring challenge: Unable to allocate post buffer";
			return;
		}

		Port *pkt3_port = reinterpret_cast<Port*>( pkt3 + 1 );
		u8 *pkt3_answer = pkt3 + 1+2;

		// They took the time to get the cookie right, might as well check if we know them
		Connection *conn = _conn_map.GetLock(src);

		// If connection already exists,
		if (conn)
		{
			// If the connection exists but has recently been deleted,
			if (!conn->IsValid())
			{
				_conn_map.ReleaseLock();
				INANE("Server") << "Ignoring challenge: Connection recently deleted";
				ReleasePostBuffer(pkt3);
				return;
			}

			// If we have seen the first encrypted packet already,
			if (conn->seen_encrypted)
			{
				_conn_map.ReleaseLock();
				WARN("Server") << "Ignoring challenge: Already in session";
				ReleasePostBuffer(pkt3);
				return;
			}

			// If we the challenge does not match the previous one,
			if (!SecureEqual(conn->first_challenge, challenge, CHALLENGE_BYTES))
			{
				_conn_map.ReleaseLock();
				INANE("Server") << "Ignoring challenge: Challenge not replayed";
				ReleasePostBuffer(pkt3);
				return;
			}

			// Construct packet 3
			pkt3[0] = S2C_ANSWER;
			*pkt3_port = getLE(conn->server_worker->GetPort());
			memcpy(pkt3_answer, conn->cached_answer, ANSWER_BYTES);

			_conn_map.ReleaseLock();

			// Post packet without checking for errors
			Post(src, pkt3, PKT3_LEN);

			INANE("Server") << "Replayed lost answer to client challenge";
		}
		else
		{
			_conn_map.ReleaseLock();

			Skein key_hash;

			// If server is overpopulated,
			if (GetTotalPopulation() >= MAX_POPULATION)
			{
				WARN("Server") << "Ignoring challenge: Server is full";

				// Construct packet 4
				u16 *error_field = reinterpret_cast<u16*>( pkt3 + 1 );
				pkt3[0] = S2C_ERROR;
				*error_field = getLE(ERR_SERVER_FULL);

				// Post packet without checking for errors
				Post(src, pkt3, 3);

				return;
			}

			// If challenge is invalid,
			if (!_key_agreement_responder.ProcessChallenge(tls->math, tls->csprng,
														   challenge, CHALLENGE_BYTES,
														   pkt3_answer, ANSWER_BYTES, &key_hash))
			{
				WARN("Server") << "Ignoring challenge: Invalid";
				ReleasePostBuffer(pkt3);
				return;
			}

			conn = new Connection;
			conn->client_addr = src;

			// If unable to key encryption from session key,
			if (!_key_agreement_responder.KeyEncryption(&key_hash, &conn->auth_enc, SESSION_KEY_NAME))
			{
				WARN("Server") << "Ignoring challenge: Unable to key encryption";
				ReleasePostBuffer(pkt3);
				delete conn;
				return;
			}

			// Find the least populated port
			ServerWorker *server_worker = FindLeastPopulatedPort();
			Port server_port = server_worker->GetPort();

			// Construct packet 3
			pkt3[0] = S2C_ANSWER;
			*pkt3_port = getLE(server_port);

			// Initialize Connection object
			memcpy(conn->first_challenge, challenge, CHALLENGE_BYTES);
			memcpy(conn->cached_answer, pkt3_answer, ANSWER_BYTES);
			conn->server_worker = server_worker;
			conn->last_recv_tsc = Clock::msec_fast();
			conn->InitializePayloadBytes(Is6());

			// If packet 3 post fails,
			if (!Post(src, pkt3, PKT3_LEN))
			{
				WARN("Server") << "Ignoring challenge: Unable to post packet";

				delete conn;
			}
			else
			{
				INANE("Server") << "Accepted challenge and posted answer.  Client connected";

				// Increment session count for this endpoint (only done here)
				server_worker->IncrementPopulation();

				// Insert a hash key
				_conn_map.Insert(conn);
			}
		}
	}
}

void Server::OnWrite(u32 bytes)
{

}

void Server::OnClose()
{

}
