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

static const char *SERVER_KEY_FILE = "serverkey.bin";

static CAT_INLINE u32 ReconstructUnreliableOrderedID(u32 last_accepted_iv, u32 partial)
{
	static const u32 IV_MSB = (1 << 24);
	static const u32 IV_MASK = (IV_MSB - 1);

	s32 diff = partial - (u32)(last_accepted_iv & IV_MASK);

	return ((last_accepted_iv & ~(u32)IV_MASK) | partial)
		- (((IV_MSB >> 1) - (diff & IV_MASK)) & IV_MSB)
		+ (diff & IV_MSB);
}

static CAT_INLINE u32 ReconstructReliableID(u32 last_accepted_iv, u32 partial)
{
	static const u32 IV_MSB = (1 << 15);
	static const u32 IV_MASK = (IV_MSB - 1);

	s32 diff = partial - (u32)(last_accepted_iv & IV_MASK);

	return ((last_accepted_iv & ~(u32)IV_MASK) | partial)
		- (((IV_MSB >> 1) - (diff & IV_MASK)) & IV_MSB)
		+ (diff & IV_MSB);
}

static CAT_INLINE u32 hash_addr(IP ip, Port port)
{
	u32 hash = ip;

	// xorshift(a=5,b=17,c=13) with period 2^32-1:
	hash ^= hash << 13;
	hash ^= hash >> 17;
	hash ^= hash << 5;

	// Add the port into the hash
	hash += port;

	// xorshift(a=3,b=13,c=7) with period 2^32-1:
	hash ^= hash << 3;
	hash ^= hash >> 13;
	hash ^= hash << 7;

	return hash;
}


//// Connection Map

ConnectionMap::ConnectionMap()
{
	_table = 0;
}

ConnectionMap::~ConnectionMap()
{
	if (_table)
	{
		Aligned::Release(_table);
	}
}

bool ConnectionMap::Initialize()
{
	static const int TABLE_BYTES = sizeof(HashKey) * HASH_TABLE_SIZE;

	_table = reinterpret_cast<HashKey*>( Aligned::Acquire(TABLE_BYTES) );
	if (_table) return false;

	memset(_table, 0, TABLE_BYTES);

	return true;
}

ConnectionMap::HashKey *ConnectionMap::Get(IP ip, Port port)
{
	u32 hash = hash_addr(ip, port) % HASH_TABLE_SIZE;

	return 0;
}

ConnectionMap::HashKey *ConnectionMap::Insert(IP ip, Port port)
{
	u32 hash = hash_addr(ip, port) % HASH_TABLE_SIZE;

	return 0;
}

void ConnectionMap::Remove(IP ip, Port port)
{
	u32 hash = hash_addr(ip, port) % HASH_TABLE_SIZE;
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
	ConnectionMap::HashKey *key = _conn_map->Get(srcIP, srcPort);

	// TODO: Thread-safety

	// If no existing connection exists, ignore this packet
	if (key && key->conn)
	{
		Connection *conn = key->conn;

		// If the connection is on a different port, ignore this packet
		if (conn->_server_port == GetPort())
		{
			int msg_bytes = bytes;

			// If the data could not be decrypted, ignore this packet
			if (conn->_auth_enc.Decrypt(data, msg_bytes))
			{
				// Flag having seen an encrypted packet
				conn->_seen_first_encrypted_packet = true;

				// Handle the decrypted data
				HandleTransportLayer(conn, data, msg_bytes);
			}
		}
	}
}

void SessionEndpoint::OnWrite(u32 bytes)
{

}

void SessionEndpoint::OnClose()
{

}

void SessionEndpoint::HandleTransportLayer(Connection *conn, u8 *data, int bytes)
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

						u32 &next_id = conn->_send_reliable_id[stream];

						u32 nack = id & 1;

						id = ReconstructReliableID(next_id, id >> 1);

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

					u32 &next_id = conn->_recv_reliable_id[stream];

					id = ReconstructReliableID(next_id, id >> 1);

					// ordered or unordered?
					if (stream == 0)
					{
						// Unordered:

						// TODO: Check if we've seen it already

						HandleMessageLayer(conn, data + 4, len);

						// TODO: Send ack and nacks
					}
					else
					{
						// Ordered:

						// TODO: Check if we've seen it already
						// TODO: Check if it is time to process it yet

						HandleMessageLayer(conn, data + 4, len);

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
					HandleMessageLayer(conn, data + 2, len);

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

					u32 &next_id = conn->_recv_unreliable_id[stream];

					// Reconstruct the message id
					id = ReconstructUnreliableOrderedID(next_id, id);

					if (id >= next_id)
					{
						next_id = id + 1;

						HandleMessageLayer(conn, data + 5, len);
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

void SessionEndpoint::HandleMessageLayer(Connection *conn, u8 *msg, int bytes)
{
	INFO("SessionEndpoint") << "Got message with " << bytes << " bytes from " << IPToString(conn->_remote_ip) << ":" << conn->_remote_port;
}




//// Handshake Endpoint

HandshakeEndpoint::HandshakeEndpoint()
{
	_sessions = 0;
}

HandshakeEndpoint::~HandshakeEndpoint()
{
	if (_sessions) delete[] _sessions;
}

bool HandshakeEndpoint::Initialize()
{
	// Use the number of threads in the pool for the number of ports
	_session_port_count = ThreadPool::ref()->GetThreadCount();
	if (_session_port_count < 1) return false;
	if (_sessions) delete[] _sessions;
	_sessions = new SessionEndpoint*[_session_port_count];
	if (!_sessions) return false;

	// Create 256-bit math library instance
	BigTwistedEdwards *math = KeyAgreementCommon::InstantiateMath(256);

	// Create CSPRNG instance
	FortunaOutput *csprng = FortunaFactory::ref()->Create();

	bool success = false;

	// If objects were created,
	if (math && csprng)
	{
		// Initialize cookie jar
		_cookie_jar.Initialize(csprng);

		// Open server key file (if possible)
		MMapFile mmf(SERVER_KEY_FILE);

		// If the file was found and of the right size,
		if (mmf.good() && mmf.remaining() == PUBLIC_KEY_BYTES + PRIVATE_KEY_BYTES)
		{
			u8 *public_key = reinterpret_cast<u8*>( mmf.read(PUBLIC_KEY_BYTES) );
			u8 *private_key = reinterpret_cast<u8*>( mmf.read(PRIVATE_KEY_BYTES) );

			// Remember the public key so we can report it to connecting users
			memcpy(_public_key, public_key, PUBLIC_KEY_BYTES);

			// Initialize key agreement responder
			success = _key_agreement_responder.Initialize(math, csprng,
														  public_key, PUBLIC_KEY_BYTES,
														  private_key, PRIVATE_KEY_BYTES);
		}
		else
		{
			u8 public_key[PUBLIC_KEY_BYTES];
			u8 private_key[PRIVATE_KEY_BYTES];

			// Say hello to my little friend
			KeyMaker Bob;

			// Ask Bob to generate a key pair for the server
			if (Bob.GenerateKeyPair(math, csprng,
									public_key, PUBLIC_KEY_BYTES,
									private_key, PRIVATE_KEY_BYTES))
			{
				// Write the key file
				ofstream keyfile(SERVER_KEY_FILE, ios_base::out | ios_base::binary);

				// If the key file was successfully opened in output mode,
				if (!keyfile.fail())
				{
					// Write the key file contents
					keyfile.write((char*)public_key, PUBLIC_KEY_BYTES);
					keyfile.write((char*)private_key, PRIVATE_KEY_BYTES);
					keyfile.flush();

					// If the key file was successfully written,
					if (!keyfile.fail())
					{
						// Remember the public key so we can report it to connecting users
						memcpy(_public_key, public_key, PUBLIC_KEY_BYTES);

						// Initialize key agreement responder
						success = _key_agreement_responder.Initialize(math, csprng,
																	  public_key, PUBLIC_KEY_BYTES,
																	  private_key, PRIVATE_KEY_BYTES);
					}
				}
			}
		}
	}

	// Free temporary objects
	if (math) delete math;
	if (csprng) delete csprng;

	// If initialization of the handshake objects was successful,
	if (success)
	{
		// Attempt to bind to the server port
		success = Bind(SERVER_PORT);

		if (success)
		{
			// For each session port count,
			for (int ii = 0; ii < _session_port_count; ++ii)
			{
				// Create a new session endpoint
				SessionEndpoint *endpoint = new SessionEndpoint(&_conn_map);

				// Store it whether it is null or not
				_sessions[ii] = endpoint;

				// If allocation or bind failed, report failure after done
				if (!endpoint || endpoint->Bind(SERVER_PORT + ii + 1))
				{
					success = false;
				}
			}
		}
	}

	return success;
}

void HandshakeEndpoint::OnRead(ThreadPoolLocalStorage *tls, IP srcIP, Port srcPort, u8 *data, u32 bytes)
{
	// c2s 00 (protocol magic[4])
	if (bytes == 1+4 && data[0] == 0)
	{
		u32 *protocol_magic = reinterpret_cast<u32*>( data + 1 );

		// If magic matches,
		if (*protocol_magic == PROTOCOL_MAGIC)
		{
			// s2c 01 (cookie[4]) (public key[64])
			u8 *pkt1 = GetPostBuffer(1+4+PUBLIC_KEY_BYTES);

			// If packet buffer could be allocated,
			if (pkt1)
			{
				u32 *pkt1_cookie = reinterpret_cast<u32*>( pkt1 + 1 );
				u8 *pkt1_public_key = pkt1 + 1+4;

				// Construct packet 1
				pkt1[0] = 1;
				*pkt1_cookie = _cookie_jar.Generate(srcIP, srcPort);
				memcpy(pkt1_public_key, _public_key, PUBLIC_KEY_BYTES);

				// Attempt to post the packet, ignoring failures
				Post(srcIP, srcPort, pkt1, 1+4+PUBLIC_KEY_BYTES);
			}
		}
	}
	// c2s 02 (cookie[4]) (challenge[64])
	else if (bytes == 1+4+CHALLENGE_BYTES && data[0] == 2)
	{
		u32 *cookie = reinterpret_cast<u32*>( data + 1 );
		u8 *challenge = data + 1+4;

		// If cookie is correct,
		if (_cookie_jar.Verify(srcIP, srcPort, *cookie))
		{
			// s2c 03 (answer[128]) E{ (server session port[2]) } [13]
			const int PKT3_LEN = 1+2+ANSWER_BYTES;
			u8 *pkt3 = GetPostBuffer(PKT3_LEN);
			Port *pkt3_port = reinterpret_cast<Port*>( pkt3 + 1 );
			u8 *pkt3_answer = pkt3 + 1+2;

			// If packet buffer could be allocated,
			if (pkt3)
			{
				// They took the time to get the cookie right, might as well check if we know them
				ConnectionMap::HashKey *hash_key = _conn_map.Get(srcIP, srcPort);

				// If connection already exists,
				if (hash_key)
				{
					Connection *conn = hash_key->conn;

					// If we haven't seen the first encrypted packet,
					if (conn && !conn->_seen_first_encrypted_packet &&
						!memcmp(conn->_challenge, challenge, CHALLENGE_BYTES))
					{
						// Construct packet 3
						pkt3[0] = 3;
						*pkt3_port = getLE(conn->_server_port);
						memcpy(pkt3_answer, conn->_answer, ANSWER_BYTES);

						// Post packet without checking for errors
						Post(srcIP, srcPort, pkt3, PKT3_LEN);
					}
					else
					{
						ReleasePostBuffer(pkt3);
					}

					// TODO: Release hash_key here
				}
				else
				{
					Skein key_hash;

					// If challenge is valid,
					if (_key_agreement_responder.ProcessChallenge(tls->math, tls->csprng,
																  challenge, CHALLENGE_BYTES,
																  pkt3_answer, ANSWER_BYTES, &key_hash))
					{
						// Insert a hash key for this entry
						ConnectionMap::HashKey *hash_key = _conn_map.Insert(srcIP, srcPort);

						// Allocate a new connection
						Connection *conn = new (RegionAllocator::ii) Connection;

						// If connection was allocated,
						if (conn)
						{
							// Key auth_enc object from the handshake
							if (_key_agreement_responder.KeyEncryption(&key_hash, &conn->_auth_enc, "SessionKey"))
							{
								// Search through the list of session ports and find the lowest session count
								u32 best_count = (u32)~0;
								int best_port = 0;

								for (int ii = 0; ii < _session_port_count; ++ii)
								{
									u32 count = _sessions[ii]->_session_count;

									// If we found a lower session count,
									if (count < best_count)
									{
										// Use this one instead
										best_count = count;
										best_port = ii;
									}
								}

								// Write the found port
								Port server_port = SERVER_PORT + best_port + 1;
								*pkt3_port = getLE(server_port);

								// Remember challenge and answer so they can be re-used if this response gets lost
								memcpy(conn->_challenge, challenge, CHALLENGE_BYTES);
								memcpy(conn->_answer, pkt3_answer, ANSWER_BYTES);

								// Initialize connection
								conn->_remote_ip = srcIP;
								conn->_remote_port = srcPort;
								conn->_server_port = server_port;
								conn->_seen_first_encrypted_packet = false;
								CAT_OBJCLR(conn->_recv_unreliable_id);
								CAT_OBJCLR(conn->_recv_reliable_id);
								CAT_OBJCLR(conn->_send_unreliable_id);
								CAT_OBJCLR(conn->_send_reliable_id);

								// Link connection to hash table
								// TODO: Thread safety
								hash_key->conn = conn;

								// If packet 3 post fails,
								if (!Post(srcIP, srcPort, pkt3, PKT3_LEN))
								{
									hash_key->conn = 0;

									// Release the connection since we cannot key encryption for them
									RegionAllocator::ii->Delete(conn);
								}
							}
							else
							{
								// Release the connection since we cannot key encryption for them
								RegionAllocator::ii->Delete(conn);
							}
						}

						// TODO: Release hash_key here
					}
					else
					{
						ReleasePostBuffer(pkt3);
					}
				}
			}
		}
	}
}

void HandshakeEndpoint::OnWrite(u32 bytes)
{

}

void HandshakeEndpoint::OnClose()
{

}
