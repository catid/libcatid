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

#ifndef CAT_SPHYNX_SERVER_HPP
#define CAT_SPHYNX_SERVER_HPP

#include <cat/io/IOLayer.hpp>
#include <cat/crypt/cookie/CookieJar.hpp>
#include <cat/crypt/tunnel/KeyAgreementResponder.hpp>

namespace cat {


namespace sphynx {


// Server port
class Server : public UDPEndpoint, public WorkerCallbacks
{
	static const u32 FLOOD_MASK = 0x80000000;

	static const int SESSION_KEY_BYTES = 32;
	char _session_key[SESSION_KEY_BYTES];

	ConnexionMap _conn_map;

	CookieJar _cookie_jar;

	KeyAgreementResponder _key_agreement_responder;
	u8 _public_key[PUBLIC_KEY_BYTES];

	// Yes to really insure fairness this should be synchronized,
	// but I am trying hard to eliminate locks everywhere and this
	// should still round-robin spin pretty well without locks.
	// TODO: Revisit this
	u32 _connect_worker;

	bool PostConnectionCookie(const NetAddr &dest);
	bool PostConnectionError(const NetAddr &dest, HandshakeError err);

public:
	Server();
	virtual ~Server();

	bool StartServer(ThreadPoolLocalStorage *tls, Port port, u8 *public_key, int public_bytes, u8 *private_key, int private_bytes, const char *session_key);

	u32 GetTotalPopulation();

	static bool GenerateKeyPair(ThreadPoolLocalStorage *tls, const char *public_key_file,
								const char *private_key_file, u8 *public_key,
								int public_bytes, u8 *private_key, int private_bytes);

protected:
	// Must return a new instance of your Connexion derivation
	virtual Connexion *NewConnexion() = 0;

	// IP address filter: Return true to allow the connection to be made
	virtual bool AcceptNewConnexion(const NetAddr &src) = 0;

	// LookupConnexion client by key
	CAT_INLINE Connexion *LookupConnexion(u32 key) { return _conn_map.Lookup(key); }

	virtual void OnShutdownRequest();
	virtual bool OnZeroReferences();

	virtual void OnReadRouting(const BatchSet &buffers);

	virtual void OnWorkerRead(IWorkerTLS *tls, const BatchSet &buffers);
	virtual void OnWorkerTick(IWorkerTLS *tls, u32 now);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_SERVER_HPP
