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

#ifndef CAT_SPHYNX_CLIENT_HPP
#define CAT_SPHYNX_CLIENT_HPP

#include <cat/net/SphynxTransport.hpp>
#include <cat/AllTunnel.hpp>

namespace cat {


namespace sphynx {


//// sphynx::Client

class Client : LoopThread, public UDPEndpoint
{
private:
	KeyAgreementInitiator _key_agreement_initiator;
	AuthenticatedEncryption _auth_enc;
	TransportSender _transport_sender;
	TransportReceiver _transport_receiver;
	NetAddr _server_addr;
	bool _connected;
	u8 _server_public_key[PUBLIC_KEY_BYTES];
	u8 _cached_challenge[CHALLENGE_BYTES];

	bool ThreadFunction(void *param);

public:
	Client();
	virtual ~Client();

	bool SetServerKey(ThreadPoolLocalStorage *tls, const void *server_key, int key_bytes);

	bool Connect(const char *hostname, Port port);
	bool Connect(const NetAddr &addr);

protected:
	// Return false to remove resolve from cache
	bool OnResolve(const char *hostname, const NetAddr *array, int array_length);

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
	void OnUnreachable(const NetAddr &src);

protected:
	bool PostHello();

protected:
	virtual void OnConnectFail();
	virtual void OnConnect();
	virtual void HandleMessageLayer(Connection *key, u8 *msg, int bytes);
	virtual void OnDisconnect(bool timeout);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_CLIENT_HPP
