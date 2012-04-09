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

#include "SecureClientDemo.hpp"
#include <iostream>
using namespace std;
using namespace cat;

static Clock *m_clock = 0;


//// SecureClientDemo

SecureClientDemo::SecureClientDemo()
{
	m_clock = Clock::ref();
}

void SecureClientDemo::OnCookie(TunnelTLS *tls, u8 *buffer)
{
    //cout << "Client: Got cookie from the server" << endl;

    double t1 = m_clock->usec();
	if (!tun_client.Verify(tls, buffer, 4, buffer + 4, CAT_DEMO_BYTES*2))
	{
        cout << "Client: Unable to verify signature" << endl;
        return;
	}
    double t2 = m_clock->usec();

    cout << "Client: Verifying signature time = " << (t2 - t1) << " usec" << endl;

    u8 challenge[CAT_C2S_CHALLENGE_BYTES + CAT_S2C_COOKIE_BYTES];

    t1 = m_clock->usec();
    if (!tun_client.GenerateChallenge(tls, challenge, CAT_C2S_CHALLENGE_BYTES))
    {
        cout << "Client: Unable to generate challenge" << endl;
        return;
    }
    memcpy(challenge + CAT_C2S_CHALLENGE_BYTES, buffer, CAT_S2C_COOKIE_BYTES); // copy cookie
    t2 = m_clock->usec();

    cout << "Client: Filling challenge message time = " << (t2 - t1) << " usec" << endl;

    //cout << "Client: Sending challenge to server" << endl;

    server_ref->OnDatagram(tls, my_addr, challenge, sizeof(challenge));
}

void SecureClientDemo::OnAnswer(TunnelTLS *tls, u8 *buffer)
{
    double t1 = m_clock->usec();
	Skein key_hash;
    if (!tun_client.ProcessAnswer(tls, buffer, CAT_S2C_ANSWER_BYTES, &key_hash) ||
		!tun_client.KeyEncryption(&key_hash, &auth_enc, "SecureDemoStream1"))
    {
        cout << "Client: Ignoring invalid answer from server" << endl;
        return;
    }
    double t2 = m_clock->usec();
    cout << "Client: Processing answer time = " << (t2 - t1) << " usec" << endl;

	tun_client.SecureErasePrivateKey();

    OnConnect(tls);
}

void SecureClientDemo::OnConnect(TunnelTLS *tls)
{
    //cout << "Client: Connected!  Sending first ping message" << endl;

    connected = true;

    u8 buffer[1500 + AuthenticatedEncryption::OVERHEAD_BYTES] = {0};

    double t1 = m_clock->usec();

    buffer[0] = 0; // type 0 message = proof at offset 5

    *(u32*)&buffer[1] = 1; // counter starts at 1

    // 32 bytes at offset 5 used for proof of key
    if (!auth_enc.GenerateProof(buffer + 5, CAT_C2S_PROOF_BYTES))
    {
        cout << "Client: Unable to generate proof" << endl;
        return;
    }

    // Encrypt it
	u32 bytes = 1500 + AuthenticatedEncryption::OVERHEAD_BYTES;
	u64 iv = auth_enc.GrabIVRange(1);
    auth_enc.Encrypt(iv, buffer, bytes);

    double t2 = m_clock->usec();

    cout << "Client: Message 0 construction time = " << (t2 - t1) << " usec" << endl;

    server_ref->OnDatagram(tls, my_addr, buffer, bytes);
}

void SecureClientDemo::OnSessionMessage(TunnelTLS *tls, u8 *buffer, u32 bytes)
{
    //cout << "Client: Got pong message from server (" << bytes << " bytes)" << endl;

    if (bytes != 1500)
    {
        cout << "Client: Ignoring truncated session message" << endl;
        return;
    }

    u8 type = buffer[0];

    u32 id = *(u32*)&buffer[1];

    if (id >= 5)
    {
        //cout << "Client: Got the last pong from the server!" << endl;
        success = true;
        return;
    }

    ++id;

    //cout << "Client: Sending ping message #" << id << endl;

    double t1 = m_clock->usec();

    u8 response[1500 + AuthenticatedEncryption::OVERHEAD_BYTES] = {0};

    response[0] = 1; // type 1 = no proof in this message, just the counter

    *(u32*)&response[1] = id;

	u32 response_bytes = 1500 + AuthenticatedEncryption::OVERHEAD_BYTES;
	u64 iv = auth_enc.GrabIVRange(1);
	auth_enc.Encrypt(iv, response, response_bytes);

    double t2 = m_clock->usec();

    cout << "Client: Message " << id << " construction time = " << (t2 - t1) << " usec" << endl;

    server_ref->OnDatagram(tls, my_addr, response, response_bytes);
}

void SecureClientDemo::Reset(TunnelTLS *tls, SecureServerDemo *cserver_ref, TunnelPublicKey &public_key)
{
    //cout << "Client: Reset!" << endl;

    server_ref = cserver_ref;
    server_addr = cserver_ref->GetAddress();
    connected = false;
    my_addr = Address(0x76543210, 0xcdef);
    success = false;

    double t1 = m_clock->usec();

    if (!tun_client.Initialize(tls, public_key))
    {
        cout << "Client: Unable to initialize" << endl;
        return;
    }

    double t2 = m_clock->usec();

    cout << "Client: Initialization time = " << (t2 - t1) << " usec" << endl;
}

void SecureClientDemo::SendHello(TunnelTLS *tls)
{
    //cout << "Client: Sending hello message" << endl;

    u8 buffer[CAT_C2S_HELLO_BYTES];

    *(u32*)buffer = getLE(0xca7eed);

    server_ref->OnDatagram(tls, my_addr, buffer, sizeof(buffer));
}

void SecureClientDemo::OnDatagram(TunnelTLS *tls, const Address &source, u8 *buffer, u32 bytes)
{
    //cout << "Client: Got packet (" << bytes << " bytes)" << endl;

	if (source != server_addr)
    {
        cout << "Client: Ignoring packet not from server" << endl;
        return;
    }

    if (connected)
    {
        double t1 = m_clock->usec();
        if (auth_enc.Decrypt(buffer, bytes))
        {
			bytes -= AuthenticatedEncryption::OVERHEAD_BYTES;
            double t2 = m_clock->usec();
            cout << "Client: Decryption overhead time = " << (t2 - t1) << " usec" << endl;
            OnSessionMessage(tls, buffer, bytes);
        }
        else
        {
            cout << "Client: Ignoring invalid session message" << endl;
        }
    }
    else
    {
        if (bytes == CAT_S2C_COOKIE_BYTES)
        {
            OnCookie(tls, buffer);
        }
        else if (bytes == CAT_S2C_ANSWER_BYTES)
        {
            OnAnswer(tls, buffer);
        }
        else
        {
            cout << "Client: Ignoring unrecognized length packet from server (before connection)" << endl;
        }
    }
}
