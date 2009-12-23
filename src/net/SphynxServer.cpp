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
	delete_me = false;
}

bool Connection::Tick(u32 now)
{
	const int DISCONNCT_TIMEOUT = 15000; // 15 seconds

	// If no packets have been received,
	if (now - last_recv_tsc >= DISCONNCT_TIMEOUT)
	{
		// Return false to destroy this connection
		return false;
	}

	transport_sender.Tick(server_endpoint, now);
	transport_receiver.Tick(server_endpoint, now);

	return true;
}

void Connection::HandleRawData(u8 *data, u32 bytes)
{
	u32 buf_bytes = bytes;

	// If packet is valid,
	if (auth_enc.Decrypt(data, buf_bytes))
	{
		// Pass it to the transport layer
		transport_receiver.OnPacket(server_endpoint, data, buf_bytes,
			fastdelegate::MakeDelegate(this, &Connection::HandleMessage));
	}
}

void Connection::HandleMessage(u8 *msg, u32 bytes)
{
}

bool Post(u8 *msg, u32 bytes)
{
	return false;
}


//// Map

Map::Map()
{
	// Initialize the hash salt to something that will
	// discourage hash-based DoS attacks against servers
	// running the protocol.
	// TODO: Determine if this needs stronger protection
	_hash_salt = (u32)s32(Clock::usec() * 1000.0);

	_active_head = 0;
	_insert_head = 0;

	CAT_OBJCLR(_table);
}

Map::~Map()
{
	Slot *slot, *next;

	INANE("SphynxServerMap") << "Freeing connection objects";

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

	_insert_lock.Enter();
	slot->next = _insert_head;
	_insert_head = slot;
	_insert_lock.Leave();

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

void Map::Tick()
{
	u32 now = Clock::msec_fast();

	Slot *active_head = _active_head;
	Slot *insert_head = 0;

	// Grab and reset the insert head
	if (_insert_head)
	{
		_insert_lock.Enter();
		insert_head = _insert_head;
		_insert_head = 0;
		_insert_lock.Leave();
	}

	// For each recently inserted slot,
	for (Slot *next, *slot = insert_head; slot; slot = next)
	{
		next = slot->next;

		INANE("SphynxServerMap") << "Linking new connection into active list";

		// Link into active list
		slot->next = active_head;
		active_head = slot;
	}

	Slot *kill_list = 0;
	Slot *last = 0;

	// For each active slot,
	for (Slot *next, *slot = active_head; slot; slot = next)
	{
		next = slot->next;

		Connection *conn = slot->connection;

		if (!conn || conn->delete_me || !conn->Tick(now))
		{
			// Unlink from active list
			if (last) last->next = next;
			else active_head = next;

			INANE("SphynxServerMap") << "Moving active connection to kill list";

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
		AutoWriteLock lock(_table_lock);

		Connection *delete_head = 0;

		// For each slot to kill,
		for (Slot *next, *slot = kill_list; slot; slot = next)
		{
			next = slot->next;

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

			INANE("SphynxServerMap") << "Deleting connection";

			delete conn;
		}
	}

	_active_head = active_head;
}


//// ServerWorker

ServerWorker::ServerWorker(Map *conn_map)
{
	_conn_map = conn_map;
	_session_count = 0;
}

ServerWorker::~ServerWorker()
{

}

void ServerWorker::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{
	// Look up an existing connection for this source address
	Connection *conn = _conn_map->GetLock(src);
	int buf_bytes = bytes;

	// If connection is valid and on the right port,
	if (conn && !conn->delete_me && conn->server_endpoint == this)
	{
		conn->HandleRawData(data, bytes);
	}

	_conn_map->ReleaseLock();
}

void ServerWorker::OnWrite(u32 bytes)
{

}

void ServerWorker::OnClose()
{

}




//// Server

Server::Server()
{
	_sessions = 0;
}

Server::~Server()
{
	if (_sessions) delete[] _sessions;

	if (!StopThread())
	{
		WARN("Server") << "Unable to stop timer thread.  Was it started?";
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

	// Use the number of processors we have access to as the number of ports
	_session_port_count = ThreadPool::ref()->GetProcessorCount() * 4;
	if (_session_port_count < 1)
	{
		WARN("Server") << "Failed to initialize: Thread pool does not have at least 1 thread running";
		return false;
	}

	if (_sessions) delete[] _sessions;

	_sessions = new ServerWorker*[_session_port_count];
	if (!_sessions)
	{
		WARN("Server") << "Failed to initialize: Unable to allocate " << _session_port_count << " session endpoint objects";
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

	// For each session port,
	bool success = true;

	for (int ii = 0; ii < _session_port_count; ++ii)
	{
		// Create a new session endpoint
		ServerWorker *endpoint = new ServerWorker(&_conn_map);

		// Store it whether it is null or not
		_sessions[ii] = endpoint;

		// If allocation or bind failed, report failure after done
		if (!endpoint || !endpoint->Bind(port + ii + 1))
		{
			WARN("Server") << "Failed to initialize: Unable to bind session port. "
				<< SocketGetLastErrorString();

			// Note failure
			success = false;
		}
	}

	// If unable to start the timer thread,
	if (success && !StartThread())
	{
		WARN("Server") << "Failed to initialize: Unable to start timer thread";

		// Note failure
		success = false;
	}

	return success;
}

ServerWorker *Server::FindLeastPopulatedPort()
{
	// Search through the list of session ports and find the lowest session count
	u32 best_count = (u32)~0;
	ServerWorker *best_port = 0;

	// For each port,
	for (int ii = 0; ii < _session_port_count; ++ii)
	{
		// Grab the session count for this port
		ServerWorker *port = _sessions[ii];
		u32 count = port->_session_count;

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
	for (int ii = 0; ii < _session_port_count; ++ii)
	{
		// Accumulate population
		population += _sessions[ii]->_session_count;
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
			if (conn->delete_me)
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
			*pkt3_port = getLE(conn->server_endpoint->GetPort());
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
			ServerWorker *server_endpoint = FindLeastPopulatedPort();
			Port server_port = server_endpoint->GetPort();

			// Construct packet 3
			pkt3[0] = S2C_ANSWER;
			*pkt3_port = getLE(server_port);

			// Initialize Connection object
			memcpy(conn->first_challenge, challenge, CHALLENGE_BYTES);
			memcpy(conn->cached_answer, pkt3_answer, ANSWER_BYTES);
			conn->server_endpoint = server_endpoint;
			conn->last_recv_tsc = Clock::msec_fast();

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
				Atomic::Add(&server_endpoint->_session_count, 1);

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

bool Server::ThreadFunction(void *)
{
	const int TICK_RATE = 10; // milliseconds

	// While quit signal is not flagged,
	while (WaitForQuitSignal(TICK_RATE))
	{
		_conn_map.Tick();
	}

	return true;
}
