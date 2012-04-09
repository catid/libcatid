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

#ifndef CAT_SECURE_SERVER_DEMO_HPP
#define CAT_SECURE_SERVER_DEMO_HPP

#include <cat/AllTunnel.hpp>
#include <map>

namespace cat {

class Address;
class Connection;
class SecureClientDemo;
class SecureServerDemo;


#define CAT_DEMO_BITS 256
#define CAT_DEMO_BYTES (CAT_DEMO_BITS/8)
#define CAT_DEMO_PUBLIC_KEY_BYTES (CAT_DEMO_BYTES*2)
#define CAT_DEMO_PRIVATE_KEY_BYTES CAT_DEMO_BYTES
#define CAT_C2S_CHALLENGE_BYTES (CAT_DEMO_BYTES*2)
#define CAT_S2C_ANSWER_BYTES (CAT_DEMO_BYTES*4)
#define CAT_C2S_PROOF_BYTES CAT_DEMO_BYTES

#define CAT_C2S_HELLO_BYTES (4)
#define CAT_S2C_COOKIE_BYTES (4 + CAT_DEMO_BYTES*2)


// ip,port address pair
class Address
{
public:
    u32 ip;
    u16 port;

    Address()
    {
    }

    Address(const Address &addr)
    {
        ip = addr.ip;
        port = addr.port;
    }

    Address(u32 cip, u16 cport)
    {
        ip = cip;
        port = cport;
    }

    Address &operator=(const Address &rhs)
    {
        ip = rhs.ip;
        port = rhs.port;
        return *this;
    }

    bool operator<(const Address &rhs) const
    {
        if (ip < rhs.ip) return true;
        if (ip > rhs.ip) return false;
        return port < rhs.port;
    }

    bool operator==(const Address &rhs) const
    {
        return ip == rhs.ip && port == rhs.port;
    }

    bool operator!=(const Address &rhs) const
    {
        return ip != rhs.ip || port != rhs.port;
    }
};


// Connection context for a connected client
class Connection
{
    friend class SecureServerDemo;

    Address client_addr;
    AuthenticatedEncryption auth_enc;
    bool seen_proof;

public:
    Connection(const Address &caddr)
        : client_addr(caddr)
    {
        seen_proof = false;
    }
};


// Secure server demo object
class SecureServerDemo
{
    SecureClientDemo *client_ref;
    CookieJar cookie_jar;

    KeyAgreementResponder tun_server;
    std::map<Address, Connection*> connections;
    Address my_addr;

	void Cleanup();

protected:
    void OnHello(TunnelTLS *tls, const Address &source, u8 *buffer);
    void OnChallenge(TunnelTLS *tls, const Address &source, u8 *buffer);

    void OnSessionMessage(TunnelTLS *tls, Connection *client, u8 *buffer, u32 bytes);

public:
	SecureServerDemo();
	~SecureServerDemo();

    void Reset(TunnelTLS *tls, SecureClientDemo *client_ref, TunnelKeyPair &key_pair);
    void OnDatagram(TunnelTLS *tls, const Address &source, u8 *buffer, u32 bytes);

    Address GetAddress() { return my_addr; }
};


} // namespace cat

#endif // CAT_SECURE_SERVER_DEMO_HPP
