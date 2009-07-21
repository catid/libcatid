/*
	Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
	published by the Free Software Foundation, either version 3 of
	the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
	License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "SecureServerDemo.hpp"
#include "SecureClientDemo.hpp"
#include <cat/crypt/rand/Fortuna.hpp>
#include <iostream>
using namespace std;
using namespace cat;

void SecureServerDemo::OnHello(const Address &source, u8 *buffer)
{
	if (*(u32*)buffer != getLE(0xca7eed))
	{
		cout << "Server: Ignoring an invalid Hello message" << endl;
		return;
	}

	cout << "Server: I got a valid Hello message. Sending a cookie back" << endl;

	u8 response[CAT_S2C_COOKIE_BYTES];
	*(u32*)response = getLE(cookie_jar.Generate(source.ip, source.port));

	client_ref->OnPacket(my_addr, response, sizeof(response));
}

void SecureServerDemo::OnChallenge(const Address &source, u8 *buffer)
{
	u32 *cookie = (u32*)(buffer + 128);
/*
	if (!cookie_jar.Verify(source.ip, source.port, getLE(*cookie)))
	{
		cout << "Server: Ignoring stale cookie" << endl;
		return;
	}
*/
	cout << "Server: Creating a new connection" << endl;

	// Create the connection
	Connection *client = new Connection(source);

	u8 answer[96];

	double t1 = Clock::usec();
	if (!tun_server.ProcessChallenge(buffer, 128, answer, 96, &client->auth_enc))
	{
		cout << "Server: Ignoring invalid challenge message" << endl;
		delete client;
		return;
	}
	double t2 = Clock::usec();

	cout << "Server: Processing challenge took " << (t2 - t1) << " usec" << endl;

	connections[source] = client;

	client_ref->OnPacket(my_addr, answer, 96);
}

void SecureServerDemo::OnSessionMessage(Connection *client, u8 *buffer, int bytes)
{
	cout << "Server: Processing valid message from client (" << bytes << " bytes)" << endl;

	u8 response[AuthenticatedEncryption::OVERHEAD_BYTES + 2560];
	if (bytes > 2560) bytes = 2560;
	memcpy(response, buffer, bytes);

	if (buffer[0] == 0)
	{
		// type 0 message includes a proof of key

		if (!client->auth_enc.ValidateProof(buffer + 5, 32))
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

	double t1 = Clock::usec();
	client->auth_enc.Encrypt(response, bytes);
	double t2 = Clock::usec();
	cout << "Server: Encryption time = " << (t2 - t1) << " usec" << endl;

	cout << "Server: Sending pong message back to client" << endl;

	client_ref->OnPacket(my_addr, response, AuthenticatedEncryption::OVERHEAD_BYTES + bytes);
}

void SecureServerDemo::Reset(SecureClientDemo *cclient_ref, const u8 *server_public_key, const u8 *server_private_key)
{
	cout << "Server: Reset!" << endl;

	client_ref = cclient_ref;
	my_addr = Address(0x11223344, 0x5566);
	cookie_jar.Initialize();

	if (!tun_server.Initialize(256, server_public_key, 128, server_private_key, 32))
	{
		cout << "Server: Unable to initialize" << endl;
		return;
	}

	connections.clear(); // WARN: leaks memory but i don't care
}

void SecureServerDemo::OnPacket(const Address &source, u8 *buffer, int bytes)
{
	cout << "Server: Got packet (" << bytes << " bytes)" << endl;

	Connection *client = connections[source];

	if (client)
	{
		double t1 = Clock::usec();
		if (!client->auth_enc.Decrypt(buffer, bytes))
		{
			cout << "Server: Ignoring invalid session message" << endl;
			return;
		}
		double t2 = Clock::usec();
		cout << "Server: Decryption time = " << (t2 - t1) << " usec" << endl;

		OnSessionMessage(client, buffer, bytes - AuthenticatedEncryption::OVERHEAD_BYTES);
	}
	else
	{
		if (bytes == CAT_C2S_HELLO_BYTES)
		{
			OnHello(source, buffer);
		}
		else if (bytes == 64 + CAT_S2C_COOKIE_BYTES)
		{
			OnChallenge(source, buffer);
		}
		else
			cout << "Server: Ignoring unrecognized length packet from client (before connection)" << endl;
	}
}
