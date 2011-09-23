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
#include <cat/io/Log.hpp>
#include <cat/io/Settings.hpp>
#include <cat/threads/WorkerThreads.hpp>
#include <cat/crypt/SecureEqual.hpp>
#include <cat/crypt/tunnel/Keys.hpp>
#include <cat/crypt/tunnel/TLS.hpp>
using namespace std;
using namespace cat;
using namespace sphynx;

static WorkerThreads *m_worker_threads = 0;
static Settings *m_settings = 0;


//// Server

bool Server::OnInitialize()
{
	Use(m_worker_threads, m_settings);

	return UDPEndpoint::OnInitialize();
}

void Server::OnDestroy()
{
	_conn_map.ShutdownAll();

	UDPEndpoint::OnDestroy();
}

void Server::OnRecvRouting(const BatchSet &buffers)
{
	u32 connect_worker = _connect_worker;
	u32 worker_count = m_worker_threads->GetWorkerCount();

	BatchSet garbage;
	garbage.Clear();
	u32 garbage_count = 0;

	BatchSet bins[MAX_WORKER_THREADS];

	static const u32 MAX_VALID_WORDS = CAT_CEIL_UNIT(MAX_WORKER_THREADS, 32);
	u32 valid[MAX_VALID_WORDS] = { 0 };

	RecvBuffer *prev_buffer = 0;

	Connexion *conn;
	u32 worker_id;

	// For each buffer in the batch,
	for (BatchHead *next, *node = buffers.head; node; node = next)
	{
		next = node->batch_next;
		RecvBuffer *buffer = static_cast<RecvBuffer*>( node );

		SetRemoteAddress(buffer);

		// If source address has changed,
		if (!prev_buffer || buffer->GetAddr() != prev_buffer->GetAddr())
		{
			// If close signal is received,
			if (buffer->data_bytes == 0)
				Destroy(CAT_REFOBJECT_TRACE);

			if (_conn_map.LookupCheckFlood(conn, buffer->GetAddr()))
			{
				// Flood detected on unconnected client, insert into garbage list
				garbage.PushBack(buffer);
				++garbage_count;
				continue;
			}
			else
			{
				// If connection matched address,
				if (conn)
				{
					worker_id = conn->GetWorkerID();
					buffer->callback.SetMember<Connexion, &Connexion::OnRecv>(conn);

					CAT_WARN("BTS-TEST") << "Connexion matched address " << conn << " with worker_id=" << worker_id;
				}
				else
				{
					// Pick the next connect worker
					if (++connect_worker >= worker_count)
						connect_worker = 0;

					worker_id = connect_worker;
					buffer->callback.SetMember<Server, &Server::OnRecv>(this);
				}

				// Compare to this buffer next time
				prev_buffer = buffer;
			}
		}
		else if (conn)
		{
			// Another packet from the same connexion
			conn->AddRef(CAT_REFOBJECT_TRACE);
			buffer->callback.SetMember<Connexion, &Connexion::OnRecv>(conn);
		}
		else
		{
			// Another packet from the same non-connection
			// Ignore these because the handshake protocol does not expect more than one packet
			// I think this is a good idea to handle connectionless floods better
			garbage.PushBack(buffer);
			++garbage_count;
			continue;
		}

		buffer->batch_next = 0;

		// If bin is already valid,
		if (BTS32(&valid[worker_id >> 5], worker_id & 31))
		{
			// Insert at the end of the bin
			bins[worker_id].tail->batch_next = buffer;
			bins[worker_id].tail = buffer;

			CAT_WARN("BTS-TEST") << "Insert at end of bin " << worker_id << " bytes=" << buffer->data_bytes;
		}
		else
		{
			// Start bin
			bins[worker_id].head = bins[worker_id].tail = buffer;

			CAT_WARN("BTS-TEST") << "Starting bin " << worker_id << " bytes=" << buffer->data_bytes;
		}
	}

	// For each valid word,
	for (u32 jj = 0, offset = 0; jj < MAX_VALID_WORDS; ++jj, offset += 32)
	{
		u32 v = valid[jj];

		while (v)
		{
			// Find next LSB index
			u32 worker_id = offset + BSF32(v);

			CAT_WARN("BTS-TEST") << "Delivering bin " << worker_id;

			// Deliver all buffers for this worker at once
			m_worker_threads->DeliverBuffers(WQPRIO_HI, worker_id, bins[worker_id]);

			// Clear LSB
			v ^= CAT_LSB32(v);
		}
	}

	// Store the final connect worker
	_connect_worker = connect_worker;

	// If garbage needs to be taken out,
	if (garbage_count > 0)
		ReleaseRecvBuffers(garbage, garbage_count);
}

void Server::OnRecv(const BatchSet &buffers)
{
	u32 buffer_count = 0;

	// For each buffer received,
	for (BatchHead *node = buffers.head; node; node = node->batch_next)
	{
		RecvBuffer *buffer = static_cast<RecvBuffer*>( node );
		++buffer_count;

		u32 bytes = buffer->data_bytes;
		u8 *data = GetTrailingBytes(buffer);

		// Process message by type and length
		if (bytes == C2S_HELLO_LEN && data[0] == C2S_HELLO)
		{
			// If magic does not match,
			u32 *protocol_magic = reinterpret_cast<u32*>( data + 1 );
			if (*protocol_magic != getLE(PROTOCOL_MAGIC))
			{
				CAT_WARN("Server") << "Ignoring hello: Bad magic";
				continue;
			}

			// Verify public key
			if (!SecureEqual(data + 1 + 4, _public_key.GetPublicKey(), PUBLIC_KEY_BYTES))
			{
				CAT_WARN("Server") << "Failing hello: Client public key does not match";
				PostConnectionError(buffer->GetAddr(), ERR_WRONG_KEY);
				continue;
			}

			CAT_WARN("Server") << "Accepted hello and posted cookie";

			PostConnectionCookie(buffer->GetAddr());
		}
		else if (bytes == C2S_CHALLENGE_LEN && data[0] == C2S_CHALLENGE)
		{
			// If magic does not match,
			u32 *protocol_magic = reinterpret_cast<u32*>( data + 1 );
			if (*protocol_magic != getLE(PROTOCOL_MAGIC))
			{
				CAT_WARN("Server") << "Ignoring challenge: Bad magic";
				continue;
			}

			// If cookie is invalid, ignore packet
			u32 *cookie = reinterpret_cast<u32*>( data + 1 + 4 );
			bool good_cookie = buffer->GetAddr().Is6() ?
				_cookie_jar.Verify(&buffer->GetAddr(), sizeof(buffer->GetAddr()), *cookie) :
				_cookie_jar.Verify(buffer->GetAddr().GetIP4(), buffer->GetAddr().GetPort(), *cookie);

			if (!good_cookie)
			{
				CAT_WARN("Server") << "Ignoring challenge: Stale cookie";
				continue;
			}

			if (IsShutdown())
			{
				CAT_WARN("Server") << "Ignoring challenge: Server is shutting down";
				PostConnectionError(buffer->GetAddr(), ERR_SHUTDOWN);
				continue;
			}

			// If the derived server object does not like this address,
			if (!AcceptNewConnexion(buffer->GetAddr()))
			{
				CAT_WARN("Server") << "Ignoring challenge: Source address is blocked";
				PostConnectionError(buffer->GetAddr(), ERR_BLOCKED);
				continue;
			}

			// If server is overpopulated,
			if (_conn_map.GetCount() >= ConnexionMap::MAX_POPULATION)
			{
				CAT_WARN("Server") << "Ignoring challenge: Server is full";
				PostConnectionError(buffer->GetAddr(), ERR_SERVER_FULL);
				continue;
			}

			TunnelTLS *tls = TunnelTLS::ref();
			if (!tls)
			{
				CAT_FATAL("Server") << "Ignoring challenge: Unable to get TLS object";
				PostConnectionError(buffer->GetAddr(), ERR_SERVER_ERROR);
				continue;
			}

			u8 *pkt = SendBuffer::Acquire(S2C_ANSWER_LEN);
			u8 *challenge = data + 1 + 4 + 4;

			Skein key_hash;
			AutoDestroy<Connexion> conn;

			// Verify that post buffer could be allocated
			if (!pkt)
			{
				CAT_WARN("Server") << "Ignoring challenge: Unable to allocate post buffer";
			}
			// If challenge is invalid,
			else if (!_key_agreement_responder.ProcessChallenge(tls, challenge, CHALLENGE_BYTES,
																pkt + 1, ANSWER_BYTES, &key_hash))
			{
				CAT_WARN("Server") << "Ignoring challenge: Invalid";

				pkt[0] = S2C_ERROR;
				pkt[1] = (u8)(ERR_TAMPERING);
				Write(pkt, S2C_ERROR_LEN, buffer->GetAddr());
			}
			// If out of memory for Connexion objects,
			else if (!(conn = NewConnexion()))
			{
				CAT_WARN("Server") << "Out of memory: Unable to allocate new Connexion";

				pkt[0] = S2C_ERROR;
				pkt[1] = (u8)(ERR_SERVER_ERROR);
				Write(pkt, S2C_ERROR_LEN, buffer->GetAddr());
			}
			// If unable to key encryption from session key,
			else if (!_key_agreement_responder.KeyEncryption(&key_hash, &conn->_auth_enc, _session_key))
			{
				CAT_WARN("Server") << "Ignoring challenge: Unable to key encryption";

				pkt[0] = S2C_ERROR;
				pkt[1] = (u8)(ERR_SERVER_ERROR);
				Write(pkt, S2C_ERROR_LEN, buffer->GetAddr());
			}
			else if (!conn->InitializeTransportSecurity(false, conn->_auth_enc))
			{
				CAT_WARN("Server") << "Ignoring challenge: Unable to initialize transport security";

				pkt[0] = S2C_ERROR;
				pkt[1] = (u8)(ERR_SERVER_ERROR);
				Write(pkt, S2C_ERROR_LEN, buffer->GetAddr());
			}
			else // Good so far:
			{
				// Finish constructing the answer packet
				pkt[0] = S2C_ANSWER;

				// Initialize Connexion object
				memcpy(conn->_first_challenge, challenge, CHALLENGE_BYTES);
				memcpy(conn->_cached_answer, pkt + 1 + 2, ANSWER_BYTES);
				conn->_client_addr = buffer->GetAddr();
				conn->_last_recv_tsc = buffer->event_msec;
				conn->_parent = this;
				conn->InitializePayloadBytes(SupportsIPv6());

				// If we have come this far, then there is now a reference to this Server object
				// in the Connexion.  So we need to add to our reference count at this point to
				// avoid a race condition.

				// Add a reference to the server on behalf of the Connexion
				// When the Connexion dies, it will release this reference
				AddRef(CAT_REFOBJECT_TRACE);

				// Assign to a worker
				conn->_worker_id = m_worker_threads->AssignTimer(conn, WorkerTimerDelegate::FromMember<Connexion, &Connexion::OnTick>(conn));

				if (!Write(pkt, S2C_ANSWER_LEN, buffer->GetAddr()))
				{
					CAT_WARN("Server") << "Ignoring challenge: Unable to post packet";
				}
				// If hash key could not be inserted,
				else if (!_conn_map.Insert(conn))
				{
					CAT_WARN("Server") << "Ignoring challenge: Same client already connected (race condition)";
				}
				// If server is still not shutting down,
				else if (!IsShutdown())
				{
					CAT_WARN("Server") << "Accepted challenge and posted answer.  Client connected";

					conn->OnConnect();

					// Do not shutdown the object
					conn.Forget();
				}
			}

			// If execution gets here, the Connexion object will be shutdown
		}
	}

	ReleaseRecvBuffers(buffers, buffer_count);
}

void Server::OnTick(u32 now)
{
	// Not synchronous with OnRecv() callback because offline events are distributed between threads
}

Server::Server()
{
	_connect_worker = 0;
}

Server::~Server()
{
}

bool Server::StartServer(Port port, TunnelKeyPair &key_pair, const char *session_key)
{
	TunnelTLS *tls = TunnelTLS::ref();
	if (!tls) return false;

	// Seed components
	_cookie_jar.Initialize(tls->CSPRNG());
	_conn_map.Initialize(tls->CSPRNG());

	// Initialize key agreement responder
	if (!_key_agreement_responder.Initialize(tls, key_pair))
	{
		CAT_WARN("Server") << "Failed to initialize: Key pair is invalid";
		return false;
	}

	// Copy session key
	CAT_STRNCPY(_session_key, session_key, SESSION_KEY_BYTES);

	// Copy public key
	_public_key = key_pair;

	// Get settings
	bool request_ip6 = m_settings->getInt("Sphynx.Server.RequestIPv6", 1) != 0;
	bool require_ip4 = m_settings->getInt("Sphynx.Server.RequireIPv4", 1) != 0;
	int kernelReceiveBufferBytes = m_settings->getInt("Sphynx.Server.KernelReceiveBuffer",
		DEFAULT_KERNEL_RECV_BUFFER, MIN_KERNEL_RECV_BUFFER, MAX_KERNEL_RECV_BUFFER);

	// Attempt to bind to the server port
	if (!Initialize(port, true, request_ip6, require_ip4, kernelReceiveBufferBytes))
	{
		CAT_WARN("Server") << "Failed to initialize: Unable to bind handshake port "
			<< port << ". " << Sockets::GetLastErrorString();
		return false;
	}

	return true;
}

bool Server::PostConnectionCookie(const NetAddr &dest)
{
	u8 *pkt = SendBuffer::Acquire(S2C_COOKIE_LEN);
	if (!pkt)
	{
		CAT_WARN("Server") << "Unable to post connection cookie: Unable to allocate post buffer";
		return false;
	}

	// Construct packet
	pkt[0] = S2C_COOKIE;

	// Endianness does not matter since we will read it back the same way
	u32 *pkt_cookie = reinterpret_cast<u32*>( pkt + 1 );
	*pkt_cookie = dest.Is6() ? _cookie_jar.Generate(&dest, sizeof(dest))
							 : _cookie_jar.Generate(dest.GetIP4(), dest.GetPort());

	// Attempt to post the packet, ignoring failures
	return Write(pkt, S2C_COOKIE_LEN, dest);
}

bool Server::PostConnectionError(const NetAddr &dest, SphynxError err)
{
	u8 *pkt = SendBuffer::Acquire(S2C_ERROR_LEN);
	if (!pkt)
	{
		CAT_WARN("Server") << "Out of memory: Unable to allocate send buffer";
		return false;
	}

	// Construct packet
	pkt[0] = S2C_ERROR;
	pkt[1] = (u8)(err);

	// Post packet without checking for errors
	return Write(pkt, S2C_ERROR_LEN, dest);
}

bool Server::InitializeKey(TunnelKeyPair &key_pair, const char *pair_path, const char *public_path)
{
	TunnelTLS *tls = TunnelTLS::ref();
	if (!tls) return false;

	if (key_pair.LoadFile(pair_path))
	{
		CAT_INFO("Server") << "Key pair loaded successfully from disk";
		return true;
	}

	if (!key_pair.Generate(tls))
	{
		CAT_INFO("Server") << "Generating new key pair failed";
		return false;
	}

	if (!key_pair.SaveFile(pair_path))
	{
		CAT_WARN("Server") << "Unable to save key pair to file " << pair_path;
	}

	TunnelPublicKey public_key(key_pair);

	if (!public_key.SaveFile(public_path))
	{
		CAT_WARN("Server") << "Unable to save public key to file " << public_path;
	}

	return true;
}
