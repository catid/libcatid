/*
	Copyright (c) 2009-2011 Christopher A. Taylor.  All rights reserved.

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

#include <cat/sphynx/Server.hpp>
#include <cat/mem/AlignedAllocator.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <cat/io/Settings.hpp>
#include <cat/io/Base64.hpp>
#include <cat/time/Clock.hpp>
#include <cat/hash/Murmur.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/tunnel/KeyMaker.hpp>
#include <fstream>
using namespace std;
using namespace cat;
using namespace sphynx;

void Server::OnShutdownRequest()
{
}

bool Server::OnZeroReferences()
{
}

Server::Server()
{
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

void Server::OnRead(RecvBuffer *buffers[], u32 count, u32 event_msec)
{
	// If there is only one buffer to process,
	if (count == 1)
	{
		NetAddr addr;
		buffers[0]->GetAddr(addr);

		buffers[0]->_next_buffer = 0;

		Connexion *conn = _conn_map.Lookup(addr);
		u32 worker_id;

		buffers[0]->_conn = conn;

		// If Connexion object was found,
		if (conn)
			worker_id = conn->GetServerWorkerID();
		else
		{
			// Yes this should be synchronized, and it will cause some unfairness
			// but I think it will not be too bad.  Right?
			worker_id = _next_connect_worker + 1;
			if (worker_id >= _worker_threads->GetWorkerCount())
				worker_id = 0;
			_next_connect_worker = worker_id;
		}

		_worker_threads->DeliverBuffers(worker_id, buffers[0], buffers[0]);

		return;
	}

	u32 worker_count = _worker_threads->GetWorkerCount();

	struct 
	{
		RecvBuffer *head, *tail;
	} *bins[MAX_WORKERS];

	static const u32 MAX_VALID_WORDS = CAT_CEIL_UNIT(MAX_WORKERS, 32);
	u32 valid[MAX_VALID_WORDS];
	CAT_OBJCLR(valid);

	Connexion *conn = 0;
	NetAddr prev_addr;
	u32 ii, prev_bin;

	// Hunt for first buffer from a Connexion
	for (ii = 0; ii < count; ++ii)
	{
		buffers[ii]->GetAddr(prev_addr);
		conn = _conn_map.Lookup(prev_addr);

		if (conn)
		{
			prev_bin = conn->GetServerWorkerID();
			break;
		}

		// Handle leading non-conn here
	}

	// For each buffer in the batch,
	for (; ii < count; ++ii)
	{
		NetAddr addr;
		buffers[ii]->GetAddr(addr);

		// If source connexion has not changed,
		if (addr == prev_addr)
		{
			continue;
		}

		// Queue a set of buffers
		conn = _conn_map.Lookup(addr);
		prev_addr = addr;
		prev_bin = conn->GetServerWorkerID();
	}

	// They took the time to get the cookie right, might as well check if we know them
	Connexion *existing_conn = _conn_map.Lookup(ov_rf->ov.addr);

	if (existing_conn)
	{
		ov_rf->allocator = ov_rf->ov.allocator;
		ov_rf->bytes = bytes;
		ov_rf->callback = fastdelegate::MakeDelegate(existing_conn, &Connexion::OnRecvFrom);
		ov_rf->event_time = event_time;

		existing_conn->GetWorkerThread()->QueueRecvFrom(ov_rf);
	}
	else
	{
	}
}

void Server::OnWorkerRead(RecvBuffer *buffer_list_head)
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
				conn->InitializePayloadBytes(Is6());

				if (!conn->InitializeTransportSecurity(false, conn->_auth_enc))
				{
					WARN("Server") << "Ignoring challenge: Unable to initialize transport security";
				}

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

void Server::OnWorkerTick(u32 now)
{
	// Can't think of anything for the server to do here yet
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
