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

#ifndef CAT_SPHYNX_CONNEXION_HPP
#define CAT_SPHYNX_CONNEXION_HPP

#include <cat/sphynx/Transport.hpp>
#include <cat/threads/RWLock.hpp>
#include <cat/threads/Thread.hpp>
#include <cat/threads/WaitableFlag.hpp>
#include <cat/sphynx/Collexion.hpp>
#include <cat/crypt/cookie/CookieJar.hpp>
#include <cat/crypt/tunnel/KeyAgreementResponder.hpp>
#include <cat/threads/RefObject.hpp>

namespace cat {


namespace sphynx {


//// sphynx::Connexion

// Derive from sphynx::Connexion and sphynx::Server to define server behavior
class Connexion : public Transport, public RefObject, public WorkerCallbacks
{
	friend class Server;
	friend class ConnexionMap;

	virtual void OnShutdownRequest();
	virtual bool OnZeroReferences();

public:
	Connexion();
	CAT_INLINE virtual ~Connexion() {}

private:
	u32 _flood_key;
	u32 _key; // Map hash table index, unique for each active connection
	Connexion *_next_delete;
	u32 _server_worker_id;

	u8 _first_challenge[64]; // First challenge seen from this client address
	u8 _cached_answer[128]; // Cached answer to this first challenge, to avoid eating server CPU time

private:
	virtual void OnWorkerRead(WorkerTLS *tls, RecvBuffer *buffer_list_head);
	virtual void OnWorkerTick(WorkerTLS *tls, u32 now);

	virtual bool PostPacket(u8 *buffer, u32 buf_bytes, u32 msg_bytes);
	virtual void OnInternal(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes);

public:
	CAT_INLINE bool IsValid() { return _destroyed == 0; }
	CAT_INLINE u32 GetKey() { return _key; }
	CAT_INLINE u32 GetFloodKey() { return _flood_key; }
	CAT_INLINE u32 GetServerWorkerID() { return _server_worker_id; }

	void Disconnect(u8 reason, bool notify);

protected:
	NetAddr _client_addr;

	// Last time a packet was received from this user -- for disconnect timeouts
	u32 _last_recv_tsc;

	bool _seen_encrypted;
	AuthenticatedEncryption _auth_enc;

protected:
	virtual void OnConnect(ThreadPoolLocalStorage *tls) = 0;
	virtual void OnDisconnect(u8 reason) = 0;
	virtual void OnTick(ThreadPoolLocalStorage *tls, u32 now) = 0;
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_CONNEXION_HPP
