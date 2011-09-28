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

#include "SecureServerDemo.hpp"
#include "SecureClientDemo.hpp"
#include <cat/crypt/rand/Fortuna.hpp>
#include <iostream>
using namespace std;
using namespace cat;

static Clock *m_clock = 0;


//// SecureServerDemo

SecureServerDemo::SecureServerDemo()
{
	m_clock = Clock::ref();
}

SecureServerDemo::~SecureServerDemo()
{
	Cleanup();
}

void SecureServerDemo::OnHello(TunnelTLS *tls, const Address &source, u8 *buffer)
{
    if (*(u32*)buffer != getLE(0xca7eed))
    {
        cout << "Server: Ignoring an invalid Hello message" << endl;
        return;
    }

    //cout << "Server: I got a valid Hello message. Sending a cookie back" << endl;

    u8 response[CAT_S2C_COOKIE_BYTES];
    *(u32*)response = getLE(cookie_jar.Generate(source.ip, source.port));

    double t1 = m_clock->usec();
	if (!tun_server.Sign(tls, response, 4, response + 4, CAT_DEMO_BYTES*2))
	{
		cout << "Server: Signature generation failure" << endl;
		return;
	}
    double t2 = m_clock->usec();

    cout << "Server: Signature generation time = " << (t2 - t1) << " usec" << endl;

    client_ref->OnDatagram(my_addr, response, sizeof(response));
}

void SecureServerDemo::OnChallenge(TunnelTLS *tls, const Address &source, u8 *buffer)
{
    u32 *cookie = (u32*)(buffer + CAT_C2S_CHALLENGE_BYTES);

    if (!cookie_jar.Verify(source.ip, source.port, getLE(*cookie)))
    {
        cout << "Server: Ignoring stale cookie" << endl;
        return;
    }

    //cout << "Server: Creating a new connection" << endl;

    // Create the connection
    Connection *client = new (std::nothrow) Connection(source);

    u8 answer[CAT_S2C_ANSWER_BYTES];

    double t1 = m_clock->usec();

	Skein key_hash;

	if (!tun_server.ProcessChallenge(tls, buffer, CAT_C2S_CHALLENGE_BYTES, answer, CAT_S2C_ANSWER_BYTES, &key_hash) ||
		!tun_server.KeyEncryption(&key_hash, &client->auth_enc, "SecureDemoStream1"))
    {
        cout << "Server: Ignoring invalid challenge message" << endl;
        delete client;
        return;
    }

    double t2 = m_clock->usec();

    cout << "Server: Processing challenge took " << (t2 - t1) << " usec" << endl;

    connections[source] = client;

    client_ref->OnDatagram(my_addr, answer, CAT_S2C_ANSWER_BYTES);
}

void SecureServerDemo::OnSessionMessage(Connection *client, u8 *buffer, u32 bytes)
{
    //cout << "Server: Processing valid message from client (" << bytes << " bytes)" << endl;

    u8 response[AuthenticatedEncryption::OVERHEAD_BYTES + 2560];
    if (bytes > 2560) bytes = 2560;
    memcpy(response, buffer, bytes);

    if (buffer[0] == 0)
    {
        // type 0 message includes a proof of key

        if (!client->auth_enc.ValidateProof(buffer + 5, CAT_C2S_PROOF_BYTES))
        {
            cout << "Server: Ignoring invalid proof of key" << endl;
            return;
        }

        client->seen_proof = true;
    }
    else if (!client->seen_proof)
    {
        cout << "Server: Ignoring session message before seeing proof of key" << endl;
        return;
    }

    double t1 = m_clock->usec();
	bytes += AuthenticatedEncryption::OVERHEAD_BYTES;
	u64 iv = client->auth_enc.GrabIVRange(1);
    client->auth_enc.Encrypt(iv, response, bytes);
    double t2 = m_clock->usec();
    cout << "Server: Encryption time = " << (t2 - t1) << " usec" << endl;

    //cout << "Server: Sending pong message back to client" << endl;

    client_ref->OnDatagram(my_addr, response, bytes);
}

void SecureServerDemo::Cleanup()
{
	for (std::map<Address, Connection*>::iterator ii = connections.begin(); ii != connections.end(); ++ii)
	{
		if (ii->second)
			delete ii->second;
	}

    connections.clear();
}

void SecureServerDemo::Reset(SecureClientDemo *cclient_ref, TunnelKeyPair &key_pair)
{
    //cout << "Server: Reset!" << endl;

	TunnelTLS *tls = TunnelTLS::ref();
	CAT_ENFORCE(tls && tls->Valid());

    client_ref = cclient_ref;
    my_addr = Address(0x11223344, 0x5566);
    cookie_jar.Initialize(tls->CSPRNG());

    if (!tun_server.Initialize(tls, key_pair))
    {
        cout << "Server: Unable to initialize" << endl;
        return;
    }

	Cleanup();
}

void SecureServerDemo::OnDatagram(const Address &source, u8 *buffer, u32 bytes)
{
    //cout << "Server: Got packet (" << bytes << " bytes)" << endl;
	TunnelTLS *tls = TunnelTLS::ref();
	CAT_ENFORCE(tls && tls->Valid());

    Connection *client = connections[source];

    if (client)
    {
        double t1 = m_clock->usec();
        if (!client->auth_enc.Decrypt(buffer, bytes))
        {
            cout << "Server: Ignoring invalid session message" << endl;
            return;
        }
		bytes -= AuthenticatedEncryption::OVERHEAD_BYTES;

        double t2 = m_clock->usec();
        cout << "Server: Decryption time = " << (t2 - t1) << " usec" << endl;

        OnSessionMessage(client, buffer, bytes);
    }
    else
    {
		if (bytes == CAT_C2S_HELLO_BYTES)
        {
            OnHello(tls, source, buffer);
        }
        else if (bytes == CAT_C2S_CHALLENGE_BYTES + CAT_S2C_COOKIE_BYTES)
        {
            OnChallenge(tls, source, buffer);
        }
        else
            cout << "Server: Ignoring unrecognized length packet from client (before connection)" << endl;
    }
}
