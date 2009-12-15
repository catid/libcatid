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

#include <cat/net/ScalableServer.hpp>
#include <cat/port/AlignedAlloc.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <fstream>
using namespace std;
using namespace cat;

static const char *SERVER_PRIVATE_KEY_FILE = "s_server_private_key.bin";
static const char *SERVER_PUBLIC_KEY_FILE = "u_server_public_key.c";
static const char *SESSION_KEY_NAME = "SessionKey";


//// Transport Layer

TransportLayer::TransportLayer()
{
	CAT_OBJCLR(_recv_reliable_id);
	CAT_OBJCLR(_recv_unreliable_id);
	CAT_OBJCLR(_send_reliable_id);
	CAT_OBJCLR(_send_unreliable_id);
}

TransportLayer::~TransportLayer()
{
}

void TransportLayer::OnPacket(UDPEndpoint *endpoint, u8 *data, int bytes, Connection *conn, MessageLayerHandler msg_handler)
{
	// See Transport Layer note in header.

	while (bytes >= 2)
	{
		u8 d0 = data[0];

		// reliable or unreliable?
		if (d0 & 1)
		{
			// Reliable:

			int stream = (d0 >> 2) & 7;

			// data or acknowledgment?
			if (d0 & 2)
			{
				// Acknowledgment:

				int count = (d0 >> 5) + 1;

				int chunk_len = 1 + (count << 1);

				if (chunk_len <= bytes)
				{
					u16 *ids = reinterpret_cast<u16*>( data + 1 );

					for (int ii = 0; ii < count; ++ii)
					{
						u32 id = getLE(ids[ii]);

						u32 &next_id = _send_reliable_id[stream];

						u32 nack = id & 1;

						id = ReconstructCounter<16>(next_id, id >> 1);

						// nack or ack?
						if (nack)
						{
							// Negative acknowledgment:

							// TODO
						}
						else
						{
							// Acknowledgment:

							// TODO
						}
					}
				}
			}
			else
			{
				// Data:

				int len = ((u32)data[1] << 3) | (d0 >> 5);

				int chunk_len = 4 + len;

				if (chunk_len <= bytes)
				{
					u16 *ids = reinterpret_cast<u16*>( data + 1 );

					u32 id = getLE(*ids);

					u32 nack = id & 1;

					u32 &next_id = _recv_reliable_id[stream];

					id = ReconstructCounter<16>(next_id, id >> 1);

					// ordered or unordered?
					if (stream == 0)
					{
						// Unordered:

						// TODO: Check if we've seen it already

						msg_handler(conn, data + 4, len);

						// TODO: Send ack and nacks
					}
					else
					{
						// Ordered:

						// TODO: Check if we've seen it already
						// TODO: Check if it is time to process it yet

						msg_handler(conn, data + 4, len);

						// TODO: Send ack and nacks
					}

					// Continue processing remaining chunks in packet
					data += chunk_len;
					bytes -= chunk_len;
					continue;
				}
			}
		}
		else
		{
			// Unreliable:

			int stream = (d0 >> 1) & 15;

			int len = ((u32)data[1] << 3) | (d0 >> 5);

			// ordered or unordered?
			if (stream == 0)
			{
				// Unordered:

				int chunk_len = 2 + len;

				if (chunk_len <= bytes)
				{
					msg_handler(conn, data + 2, len);

					// Continue processing remaining chunks in packet
					data += chunk_len;
					bytes -= chunk_len;
					continue;
				}
			}
			else
			{
				// Ordered:

				int chunk_len = 5 + len;

				if (chunk_len <= bytes)
				{
					u32 id = ((u32)data[2] << 16) | ((u32)data[3] << 8) | data[4];

					u32 &next_id = _recv_unreliable_id[stream];

					// Reconstruct the message id
					id = ReconstructCounter<24>(next_id, id);

					// If the ID is in the future,
					if ((s32)(id - next_id) >= 0)
					{
						next_id = id + 1;

						msg_handler(conn, data + 5, len);
					}

					// Continue processing remaining chunks in packet
					data += chunk_len;
					bytes -= chunk_len;
					continue;
				}
			}
		}

		// If execution reaches the end of this loop for any
		// reason, stop processing and return.
		return;
	}
}

void TransportLayer::Tick(UDPEndpoint *endpoint)
{
}


//// Connection

Connection::Connection()
{
	flags = 0;
	next_inserted = 0;
	//references = 0;
}
/*
// Returns true iff this is the first reference lock
bool Connection::AddRef()
{
	return Atomic::Add(&references, 1) == 0;
}

// Returns true iff this is the last reference unlock
bool Connection::ReleaseRef()
{
	return Atomic::Add(&references, -1) == 1;
}

Connection::Ref::Ref(Connection *conn)
{
	_conn = conn;
}

Connection::Ref::~Ref()
{
	if (_conn) _conn->ReleaseRef();
}
*/
void Connection::ClearFlags()
{
	flags = 0;
}

bool Connection::IsFlagSet(int bit)
{
	return (flags & (1 << bit)) != 0;
}

bool Connection::IsFlagUnset(int bit)
{
	return (flags & (1 << bit)) == 0;
}

bool Connection::SetFlag(int bit)
{
	if (IsFlagSet(bit)) return false;
	return !Atomic::BTS(&flags, bit);
}

bool Connection::UnsetFlag(int bit)
{
	if (IsFlagUnset(bit)) return false;
	return !Atomic::BTR(&flags, bit);
}


//// Connection Map

ConnectionMap::ConnectionMap()
{
	// Initialize the hash salt to something that will
	// discourage hash-based DoS attacks against servers
	// running the protocol.
	// TODO: Determine if this needs stronger protection
	_hash_salt = (u32)s32(Clock::usec() * 1000.0);

	_insert_head_key1 = 0;
}

u32 ConnectionMap::hash_addr(IP ip, Port port, u32 salt)
{
	u32 a = salt ^ ip;

	// Thomas Wang's integer hash function
	// http://www.cris.com/~Ttwang/tech/inthash.htm
	a = (a ^ 61) ^ (a >> 16);
	a = a + (a << 3);
	a = a ^ (a >> 4);
	a = a * 0x27d4eb2d;
	a = a ^ (a >> 15);

	// Hide this from the client-side to prevent users from generating
	// hash table collisions by changing their port number.
	const int SECRET_CONSTANT = 2501; // > 0

	// Map 16-bit port 1:1 to a random-looking number
	a += ((u32)port * (SECRET_CONSTANT*4 + 1)) & 0xffff;

	// Seems to work well in practice, for power-of-two table sizes only
	return a % ConnectionMap::HASH_TABLE_SIZE;
}

u32 ConnectionMap::next_collision_key(u32 key)
{
	// LCG with period equal to the table size
	return (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) % HASH_TABLE_SIZE;
}

Connection *ConnectionMap::Get(IP ip, Port port)
{
	Connection *conn;

	// Hash IP:port:salt to get the hash table key
	u32 key = hash_addr(ip, port, _hash_salt);

	// Forever,
	for (;;)
	{
		// Grab the slot
		conn = &_table[key];

		// If the slot is used and the user address matches,
		if (conn->IsFlagSet(Connection::FLAG_USED) &&
			conn->remote_ip == ip &&
			conn->remote_port == port)
		{
			// Return this slot with reference still held
			return conn;
		}
		else
		{
			// If the slot indicates a collision,
			if (conn->IsFlagSet(Connection::FLAG_COLLISION))
			{
				// Calculate next collision key
				key = next_collision_key(key);

				// Loop around and process the next slot in the collision list
			}
			else
			{
				// Reached end of collision list, so the address was not found in the table
				break;
			}
		}
	}

	return 0;
}

/*
	Insertion is only done from a single thread, so it is guaranteed
	that the address does not already exist in the hash table.
*/
Connection *ConnectionMap::Insert(IP ip, Port port)
{
	// Hash IP:port:salt to get the hash table key
	u32 key = hash_addr(ip, port, _hash_salt);

	// Grab the slot
	Connection *conn = &_table[key];

	// While collision keys are marked used,
	while (conn->IsFlagSet(Connection::FLAG_USED))
	{
		WARN("ConnectionMap") << "COLLISION! " << key;
		// Set flag for collision
		conn->SetFlag(Connection::FLAG_COLLISION);

		// Iterate to next collision key
		key = next_collision_key(key);
		conn = &_table[key];

		// NOTE: This will loop forever if every table key is marked used
	}

	// Add to head of recently-inserted list
	conn->next_inserted = (_insert_head_key1 != key + 1) ? _insert_head_key1 : 0;
	_insert_head_key1 = key + 1;

	Connection *chosen_slot = conn;

	// Set used flag for chosen slot
	chosen_slot->SetFlag(Connection::FLAG_USED);

	// If collision list continues after this slot,
	if (conn->IsFlagSet(Connection::FLAG_COLLISION))
	{
		Connection *end_of_list = conn;

		// While collision list continues,
		do
		{
			// Iterate to next collision key
			key = next_collision_key(key);
			conn = &_table[key];

			// If this key is also used,
			if (conn->IsFlagSet(Connection::FLAG_USED))
			{
				// Remember it as the end of the collision list
				end_of_list = conn;
			}
		} while (conn->IsFlagSet(Connection::FLAG_COLLISION));

		// Truncate collision list at the detected end of the list
		conn = end_of_list;
		while (conn->UnsetFlag(Connection::FLAG_COLLISION))
		{
			// Iterate to next collision key
			key = next_collision_key(key);
			conn = &_table[key];
		}
	}

	return chosen_slot;
}

void ConnectionMap::Remove(Connection *conn)
{
	// Unset used flag
	conn->UnsetFlag(Connection::FLAG_USED);

	// NOTE: Collision lists are truncated lazily on insertion
}

Connection *ConnectionMap::GetFirstInserted()
{
	// Cache recently-inserted head
	u32 key = _insert_head_key1;

	// If there are no recently-inserted slots, return 0
	if (!key) return 0;

	// Return pointer to head recently-inserted slot
	return &_table[key - 1];
}

Connection *ConnectionMap::GetNextInserted(Connection *conn)
{
	// Cache next recently-inserted slot
	u32 next = conn->next_inserted;

	// If there are no more, return 0
	if (!next) return 0;

	// Unlink slot
	conn->next_inserted = 0;

	// Return pointer to next recently-inserted slot
	return &_table[next - 1];
}


//// Session Endpoint

SessionEndpoint::SessionEndpoint(ConnectionMap *conn_map)
{
	_conn_map = conn_map;
	_session_count = 0;
}

SessionEndpoint::~SessionEndpoint()
{

}

void SessionEndpoint::OnRead(ThreadPoolLocalStorage *tls, IP srcIP, Port srcPort, u8 *data, u32 bytes)
{
	// Look up an existing connection for this source address
	Connection *conn = _conn_map->Get(srcIP, srcPort);
	int buf_bytes = bytes;

	// If the packet is not full of fail,
	if (conn && conn->server_port == GetPort() &&
		conn->auth_enc.Decrypt(data, buf_bytes))
	{
		// Flag having seen an encrypted packet
		conn->SetFlag(Connection::FLAG_C2S_ENC);
		conn->last_recv_tsc = Clock::msec();

		// Handle the decrypted data
		conn->transport.OnPacket(this, data, buf_bytes, conn,
			fastdelegate::MakeDelegate(this, &SessionEndpoint::HandleMessageLayer));
	}
}

void SessionEndpoint::OnWrite(u32 bytes)
{

}

void SessionEndpoint::OnClose()
{

}

void SessionEndpoint::HandleMessageLayer(Connection *conn, u8 *msg, int bytes)
{
	INFO("SessionEndpoint") << "Got message with " << bytes << " bytes from "
		<< IPToString(conn->remote_ip) << ":" << conn->remote_port;
}




//// Handshake Endpoint

ScalableServer::ScalableServer()
{
	_sessions = 0;
}

ScalableServer::~ScalableServer()
{
	if (_sessions) delete[] _sessions;

	if (!StopThread())
	{
		WARN("ScalableServer") << "Unable to stop timer thread.  Was it started?";
	}
}

bool ScalableServer::Initialize(ThreadPoolLocalStorage *tls)
{
	// If objects were not created,
	if (!tls->Valid())
	{
		WARN("ScalableServer") << "Failed to initialize: Unable to create thread local storage";
		return false;
	}

	// Use the number of processors we have access to as the number of ports
	_session_port_count = ThreadPool::ref()->GetProcessorCount();
	if (_session_port_count < 1)
	{
		WARN("ScalableServer") << "Failed to initialize: Thread pool does not have at least 1 thread running";
		return false;
	}

	if (_sessions) delete[] _sessions;

	_sessions = new SessionEndpoint*[_session_port_count];
	if (!_sessions)
	{
		WARN("ScalableServer") << "Failed to initialize: Unable to allocate " << _session_port_count << " session endpoint objects";
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
			WARN("ScalableServer") << "Failed to initialize: Key from key file is invalid";
			return false;
		}
	}
	else
	{
		INFO("ScalableServer") << "Key file not present.  Creating a new key pair...";

		u8 public_key[PUBLIC_KEY_BYTES];
		u8 private_key[PRIVATE_KEY_BYTES];

		// Say hello to my little friend
		KeyMaker Bob;

		// Ask Bob to generate a key pair for the server
		if (!Bob.GenerateKeyPair(tls->math, tls->csprng,
								 public_key, PUBLIC_KEY_BYTES,
								 private_key, PRIVATE_KEY_BYTES))
		{
			WARN("ScalableServer") << "Failed to initialize: Unable to generate key pair";
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
				WARN("ScalableServer") << "Failed to initialize: Unable to open key file(s) for writing";
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
				WARN("ScalableServer") << "Failed to initialize: Unable to write key file";
				return false;
			}

			// Remember the public key so we can report it to connecting users
			memcpy(_public_key, public_key, PUBLIC_KEY_BYTES);

			// Initialize key agreement responder
			if (!_key_agreement_responder.Initialize(tls->math, tls->csprng,
													 public_key, PUBLIC_KEY_BYTES,
													 private_key, PRIVATE_KEY_BYTES))
			{
				WARN("ScalableServer") << "Failed to initialize: Key we just generated is invalid";
				return false;
			}
		}
	}

	// Attempt to bind to the server port
	if (!Bind(SERVER_PORT))
	{
		WARN("ScalableServer") << "Failed to initialize: Unable to bind handshake port "
			<< SERVER_PORT << ". " << SocketGetLastErrorString();
		return false;
	}

	// For each session port,
	bool success = true;

	for (int ii = 0; ii < _session_port_count; ++ii)
	{
		// Create a new session endpoint
		SessionEndpoint *endpoint = new SessionEndpoint(&_conn_map);

		// Store it whether it is null or not
		_sessions[ii] = endpoint;

		// If allocation or bind failed, report failure after done
		if (!endpoint || !endpoint->Bind())
		{
			WARN("ScalableServer") << "Failed to initialize: Unable to bind session port. "
				<< SocketGetLastErrorString();

			// Note failure
			success = false;
		}
	}

	// If unable to start the timer thread,
	if (success && !StartThread())
	{
		WARN("ScalableServer") << "Failed to initialize: Unable to start timer thread";

		// Note failure
		success = false;
	}

	return success;
}

SessionEndpoint *ScalableServer::FindLeastPopulatedPort()
{
	// Search through the list of session ports and find the lowest session count
	u32 best_count = (u32)~0;
	SessionEndpoint *best_port = 0;

	for (int ii = 0; ii < _session_port_count; ++ii)
	{
		SessionEndpoint *port = _sessions[ii];
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

void ScalableServer::OnRead(ThreadPoolLocalStorage *tls, IP srcIP, Port srcPort, u8 *data, u32 bytes)
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
				*pkt1_cookie = _cookie_jar.Generate(srcIP, srcPort);
				memcpy(pkt1_public_key, _public_key, PUBLIC_KEY_BYTES);

				// Attempt to post the packet, ignoring failures
				Post(srcIP, srcPort, pkt1, 1+4+PUBLIC_KEY_BYTES);

				INANE("ScalableServer") << "Accepted hello and posted cookie";
			}
		}
	}
	// c2s 02 (cookie[4]) (challenge[64])
	else if (bytes == 1+4+CHALLENGE_BYTES && data[0] == C2S_CHALLENGE)
	{
		u32 *cookie = reinterpret_cast<u32*>( data + 1 );
		u8 *challenge = data + 1+4;

		// If cookie is invalid, ignore packet
		if (!_cookie_jar.Verify(srcIP, srcPort, *cookie))
		{
			WARN("ScalableServer") << "Ignoring challenge: Stale cookie";
			return;
		}

		// s2c 03 (answer[128]) E{ (server session port[2]) } [13]
		const int PKT3_LEN = 1+2+ANSWER_BYTES;
		u8 *pkt3 = GetPostBuffer(PKT3_LEN);

		// Verify that post buffer could be allocated
		if (!pkt3)
		{
			WARN("ScalableServer") << "Ignoring challenge: Unable to allocate post buffer";
			return;
		}

		Port *pkt3_port = reinterpret_cast<Port*>( pkt3 + 1 );
		u8 *pkt3_answer = pkt3 + 1+2;

		// They took the time to get the cookie right, might as well check if we know them
		Connection *conn = _conn_map.Get(srcIP, srcPort);

		// If connection already exists,
		if (conn)
		{
			// If we have seen the first encrypted packet already,
			if (conn->IsFlagSet(Connection::FLAG_C2S_ENC))
			{
				WARN("ScalableServer") << "Ignoring challenge: Already in session";
				ReleasePostBuffer(pkt3);
				return;
			}

			// If we the challenge does not match the previous one,
			if (!SecureEqual(conn->first_challenge, challenge, CHALLENGE_BYTES))
			{
				WARN("ScalableServer") << "Ignoring challenge: Challenge not replayed";
				ReleasePostBuffer(pkt3);
				return;
			}

			// Construct packet 3
			pkt3[0] = S2C_ANSWER;
			*pkt3_port = getLE(conn->server_port);
			memcpy(pkt3_answer, conn->cached_answer, ANSWER_BYTES);

			// Post packet without checking for errors
			Post(srcIP, srcPort, pkt3, PKT3_LEN);

			INANE("ScalableServer") << "Replayed lost answer to client challenge";
		}
		else
		{
			Skein key_hash;

			// If challenge is invalid,
			if (!_key_agreement_responder.ProcessChallenge(tls->math, tls->csprng,
														   challenge, CHALLENGE_BYTES,
														   pkt3_answer, ANSWER_BYTES, &key_hash))
			{
				WARN("ScalableServer") << "Ignoring challenge: Invalid";
				ReleasePostBuffer(pkt3);
				return;
			}

			// Insert a hash key
			conn = _conn_map.Insert(srcIP, srcPort);

			// If unable to insert a hash key,
			if (!conn)
			{
				WARN("ScalableServer") << "Ignoring challenge: Unable to insert into hash table";
				ReleasePostBuffer(pkt3);
				return;
			}

			// If unable to key encryption from session key,
			if (!_key_agreement_responder.KeyEncryption(&key_hash, &conn->auth_enc, SESSION_KEY_NAME))
			{
				WARN("ScalableServer") << "Ignoring challenge: Unable to key encryption";
				ReleasePostBuffer(pkt3);
				_conn_map.Remove(conn);
				return;
			}

			// Find the least populated port
			SessionEndpoint *server_endpoint = FindLeastPopulatedPort();
			Port server_port = server_endpoint->GetPort();

			// Construct packet 3
			pkt3[0] = S2C_ANSWER;
			*pkt3_port = getLE(server_port);

			// Initialize Connection object
			memcpy(conn->first_challenge, challenge, CHALLENGE_BYTES);
			memcpy(conn->cached_answer, pkt3_answer, ANSWER_BYTES);
			conn->remote_ip = srcIP;
			conn->remote_port = srcPort;
			conn->server_port = server_port;
			conn->server_endpoint = server_endpoint;
			conn->last_recv_tsc = Clock::msec();

			// If packet 3 post fails,
			if (!Post(srcIP, srcPort, pkt3, PKT3_LEN))
			{
				WARN("ScalableServer") << "Ignoring challenge: Unable to post packet";
				_conn_map.Remove(conn);
			}
			else
			{
				INANE("ScalableServer") << "Accepted challenge and posted answer.  Client connected";
			}
		}
	}
}

void ScalableServer::OnWrite(u32 bytes)
{

}

void ScalableServer::OnClose()
{

}

bool ScalableServer::ThreadFunction(void *)
{
	const int TICK_RATE = 20; // milliseconds
	Connection *timed_head = 0;

	// While quit signal is not flagged,
	while (WaitForQuitSignal(TICK_RATE))
	{
		const int DISCONNECT_TIMEOUT = 15000; // milliseconds
		u32 now = Clock::msec();
		Connection *conn;
		Connection *next_timed;

		// For each recently inserted slot,
		for (conn = _conn_map.GetFirstInserted(); conn; conn = _conn_map.GetNextInserted(conn))
		{
			// Ignore unused slots
			if (conn->IsFlagUnset(Connection::FLAG_USED))
				continue;

			// Ignore slots already in the timed list
			if (!conn->SetFlag(Connection::FLAG_TIMED))
				continue;

			WARN("ScalableServer") << "Added " << conn << " to timed list";

			// Insert into linked list
			conn->next_timed = timed_head;
			conn->last_timed = 0;
			timed_head = conn;
		}

		// For each timed slot,
		for (conn = timed_head; conn; conn = next_timed)
		{
			// Cache next timed slot because this one may be removed
			next_timed = conn->next_timed;

			// If slot is now unused,
			if (conn->IsFlagUnset(Connection::FLAG_USED))
			{
				WARN("ScalableServer") << "Removing unused slot " << conn << " from timed list";

				// Remove from the timed list
				conn->UnsetFlag(Connection::FLAG_TIMED);

				// Linked list remove
				Connection *next = conn->next_timed;
				Connection *last = conn->last_timed;
				if (next) next->last_timed = last;
				if (last) last->next_timed = next;
				else timed_head = next;

				continue;
			}

			// If we haven't received any data from the user,
			if (now - conn->last_recv_tsc >= DISCONNECT_TIMEOUT)
			{
				WARN("ScalableServer") << "Removing timeout slot " << conn;

				_conn_map.Remove(conn);

				continue;
			}

			// If seen first encrypted packet already,
			if (conn->IsFlagSet(Connection::FLAG_C2S_ENC))
			{
				// Tick the transport layer for this connection
				conn->transport.Tick(conn->server_endpoint);
			}
		}
	}

	return true;
}


//// Scalable Client

ScalableClient::ScalableClient()
{
	_connected = false;
}

ScalableClient::~ScalableClient()
{
	if (!StopThread())
	{
		WARN("ScalableServer") << "Unable to stop timer thread.  Was it started?";
	}
}

bool ScalableClient::Connect(ThreadPoolLocalStorage *tls, IP server_ip, const void *server_key, int key_bytes)
{
	// Verify that we are not already connected
	if (_connected)
	{
		WARN("ScalableClient") << "Failed to connect: Already connected";
		return false;
	}

	// Verify the key bytes are correct
	if (key_bytes != sizeof(_server_public_key))
	{
		WARN("ScalableClient") << "Failed to connect: Invalid server public key length provided";
		return false;
	}

	// Verify TLS is valid
	if (!tls->Valid())
	{
		WARN("ScalableClient") << "Failed to connect: Unable to create thread local storage";
		return false;
	}

	// Verify public key and initialize crypto library with it
	if (!_key_agreement_initiator.Initialize(tls->math, reinterpret_cast<const u8*>( server_key ), key_bytes))
	{
		WARN("ScalableClient") << "Failed to connect: Invalid server public key provided";
		return false;
	}

	// Generate a challenge for the server
	if (!_key_agreement_initiator.GenerateChallenge(tls->math, tls->csprng, _cached_challenge, CHALLENGE_BYTES))
	{
		WARN("ScalableClient") << "Failed to connect: Cannot generate challenge message";
		return false;
	}

	// Cache public key and IP
	memcpy(_server_public_key, server_key, sizeof(_server_public_key));
	_server_ip = server_ip;

	// Attempt to bind to any port and accept ICMP errors initially
	if (!Bind(0, false))
	{
		WARN("ScalableClient") << "Failed to connect: Unable to bind to any port";
		return false;
	}

	// Attempt to post hello message
	if (!PostHello())
	{
		WARN("ScalableClient") << "Failed to connect: Post failure";
		Close();
		return false;
	}

	// Attempt to start the timer thread
	if (!StartThread())
	{
		WARN("ScalableClient") << "Failed to connect: Unable to start timer thread";
		Close();
		return false;
	}

	return true;
}

void ScalableClient::OnUnreachable(IP srcIP)
{
	// If IP matches the server and we're not connected yet,
	if (srcIP == _server_ip && !_connected)
	{
		WARN("ScalableClient") << "Failed to connect: ICMP error received from server address";

		// ICMP error from server means it is down
		OnConnectFail();

		Close();
	}
}

void ScalableClient::OnRead(ThreadPoolLocalStorage *tls, IP srcIP, Port srcPort, u8 *data, u32 bytes)
{
	// If packet source is not the server, ignore this packet
	if (srcIP == _server_ip && srcPort == SERVER_PORT)
	{
		// If connection has completed
		if (_connected)
		{
			int buf_bytes = bytes;

			// If the data could not be decrypted, ignore this packet
			if (_auth_enc.Decrypt(data, buf_bytes))
			{
				// Pass the packet to the transport layer
				_transport.OnPacket(this, data, buf_bytes, 0,
					fastdelegate::MakeDelegate(this, &ScalableClient::HandleMessageLayer));
			}
		}
		// s2c 01 (cookie[4]) (public key[64])
		else if (bytes == 1+4+PUBLIC_KEY_BYTES && data[0] == S2C_COOKIE)
		{
			u32 *in_cookie = reinterpret_cast<u32*>( data + 1 );
			u8 *in_public_key = data + 1+4;

			// Verify public key
			if (!SecureEqual(in_public_key, _server_public_key, PUBLIC_KEY_BYTES))
			{
				WARN("ScalableClient") << "Unable to connect: Server public key does not match expected key";
				OnConnectFail();
				Close();
				return;
			}

			// Allocate a post buffer
			static const int response_len = 1+4+CHALLENGE_BYTES;
			u8 *response = GetPostBuffer(response_len);

			if (!response)
			{
				WARN("ScalableClient") << "Unable to connect: Cannot allocate buffer for challenge message";
				OnConnectFail();
				Close();
				return;
			}

			// Construct challenge packet
			u32 *out_cookie = reinterpret_cast<u32*>( response + 1 );
			u8 *out_challenge = response + 1+4;

			response[0] = C2S_CHALLENGE;
			*out_cookie = *in_cookie;
			memcpy(out_challenge, _cached_challenge, CHALLENGE_BYTES);

			// Start ignoring ICMP unreachable messages now that we've seen a response from the server
			if (!IgnoreUnreachable())
			{
				WARN("ScalableClient") << "ICMP ignore unreachable failed";
			}

			// Attempt to post a response
			if (!Post(_server_ip, SERVER_PORT, response, response_len))
			{
				WARN("ScalableClient") << "Unable to connect: Cannot post response to cookie";
				OnConnectFail();
				Close();
			}
			else
			{
				INANE("ScalableClient") << "Accepted cookie and posted challenge";
			}
		}
		// s2c 03 (server session port[2]) (answer[128])
		else if (bytes == 1+2+ANSWER_BYTES && data[0] == S2C_ANSWER)
		{
			Port *port = reinterpret_cast<Port*>( data + 1 );
			u8 *answer = data + 3;

			Port server_session_port = getLE(*port);

			// Ignore packet if the port doesn't make sense
			if (server_session_port > SERVER_PORT)
			{
				Skein key_hash;

				// Process answer from server, ignore invalid
				if (_key_agreement_initiator.ProcessAnswer(tls->math, answer, ANSWER_BYTES, &key_hash))
				{
					if (_key_agreement_initiator.KeyEncryption(&key_hash, &_auth_enc, SESSION_KEY_NAME))
					{
						_connected = true;
						_server_session_port = server_session_port;

						OnConnect();
					}
				}
			}
		}
	}
}

void ScalableClient::OnWrite(u32 bytes)
{

}

void ScalableClient::OnClose()
{

}

void ScalableClient::OnConnectFail()
{
	WARN("ScalableClient") << "Connection failed.";
}

bool ScalableClient::PostHello()
{
	// Allocate space for a post buffer
	static const int hello_len = 1+4;
	u8 *hello = GetPostBuffer(hello_len);

	// If unable to allocate,
	if (!hello)
	{
		WARN("ScalableClient") << "Cannot allocate a post buffer for hello packet";
		return false;
	}

	u32 *magic = reinterpret_cast<u32*>( hello + 1 );

	// Construct packet
	hello[0] = C2S_HELLO;
	*magic = getLE(PROTOCOL_MAGIC);

	// Attempt to post packet
	if (!Post(_server_ip, SERVER_PORT, hello, hello_len))
	{
		WARN("ScalableClient") << "Unable to post hello packet";
		return false;
	}

	INANE("ScalableClient") << "Posted hello packet";

	return true;
}

void ScalableClient::OnConnect()
{
	INFO("ScalableClient") << "Connected";
}

void ScalableClient::HandleMessageLayer(Connection *key, u8 *msg, int bytes)
{
	INFO("ScalableClient") << "Got message with " << bytes << " bytes";
}

void ScalableClient::OnDisconnect(bool timeout)
{
	WARN("ScalableClient") << "Disconnected. Timeout=" << timeout;
}

bool ScalableClient::ThreadFunction(void *)
{
	u32 last_hello_post = Clock::msec();
	const int HELLO_POST_INTERVAL = 200; // milliseconds

	// Process timers every 20 milliseconds
	while (WaitForQuitSignal(20))
	{
		if (!_connected)
		{
			u32 now = Clock::msec();

			if (now - last_hello_post >= HELLO_POST_INTERVAL)
			{
				if (!PostHello())
				{
					WARN("ScalableClient") << "Unable to connect: Post failure";
					return false;
				}

				last_hello_post = now;
			}
		}
		else
		{
			_transport.Tick(this);
		}
	}

	return true;
}
