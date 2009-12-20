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

#include <cat/net/SphynxClient.hpp>
#include <cat/port/AlignedAlloc.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <cat/net/DNSClient.hpp>
#include <fstream>
using namespace std;
using namespace cat;
using namespace sphynx;


//// Encryption Key Constants

// Must match SphynxServer.cpp
static const char *SESSION_KEY_NAME = "SessionKey";


//// Client

Client::Client()
{
	_connected = false;
}

Client::~Client()
{
	if (!StopThread())
	{
		WARN("Client") << "Unable to stop timer thread.  Was it started?";
	}
}

bool Client::SetServerKey(ThreadPoolLocalStorage *tls, const void *server_key, int key_bytes)
{
	// Verify the key bytes are correct
	if (key_bytes != sizeof(_server_public_key))
	{
		WARN("Client") << "Failed to connect: Invalid server public key length provided";
		return false;
	}

	// Verify TLS is valid
	if (!tls->Valid())
	{
		WARN("Client") << "Failed to connect: Unable to create thread local storage";
		return false;
	}

	// Verify public key and initialize crypto library with it
	if (!_key_agreement_initiator.Initialize(tls->math, reinterpret_cast<const u8*>( server_key ), key_bytes))
	{
		WARN("Client") << "Failed to connect: Invalid server public key provided";
		return false;
	}

	// Generate a challenge for the server
	if (!_key_agreement_initiator.GenerateChallenge(tls->math, tls->csprng, _cached_challenge, CHALLENGE_BYTES))
	{
		WARN("Client") << "Failed to connect: Cannot generate challenge message";
		return false;
	}

	memcpy(_server_public_key, server_key, sizeof(_server_public_key));

	return true;
}

bool Client::Connect(const char *hostname, Port port)
{
	// Set port
	_server_addr.SetPort(port);

	// If DNS resolution fails,
	if (!DNSClient::ii->Resolve(hostname, fastdelegate::MakeDelegate(this, &Client::OnResolve), this))
	{
		WARN("Client") << "Failed to connect: Unable to resolve server hostname";
		return false;
	}

	return true;
}

bool Client::Connect(const NetAddr &addr)
{
	// Validate port
	if (addr.GetPort() == 0)
	{
		WARN("Client") << "Failed to connect: Invalid server port specified";
		return false;
	}

	// Attempt to bind to any port and accept ICMP errors initially
	if (!Bind(0, false))
	{
		WARN("Client") << "Failed to connect: Unable to bind to any port";
		return false;
	}

	// Cache server address
	_server_addr = addr;
	if (Is6()) _server_addr.PromoteTo6();

	// Attempt to post hello message
	if (!PostHello())
	{
		WARN("Client") << "Failed to connect: Post failure";
		Close();
		return false;
	}

	// Attempt to start the timer thread
	if (!StartThread())
	{
		WARN("Client") << "Failed to connect: Unable to start timer thread";
		Close();
		return false;
	}

	return true;
}

bool Client::OnResolve(const char *hostname, const NetAddr *array, int array_length)
{
	// If resolve failed,
	if (array_length <= 0)
	{
		WARN("Client") << "Failed to connect: Server hostname resolve failed";

		Close();
	}
	else
	{
		NetAddr addr = array[0];
		addr.SetPort(_server_addr.GetPort());

		INFO("Client") << "Connecting: Resolved '" << hostname << "' to " << addr.IPToString();

		if (!Connect(addr))
			Close();
	}

	return true;
}

void Client::OnUnreachable(const NetAddr &src)
{
	// If IP matches the server and we're not connected yet,
	if (!_connected && _server_addr.EqualsIPOnly(src))
	{
		WARN("Client") << "Failed to connect: ICMP error received from server address";

		// ICMP error from server means it is down
		Close();
	}
}

void Client::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{
	// If packet source is not the server, ignore this packet
	if (_server_addr != src)
		return;

	// If connection has completed
	if (_connected)
	{
		int buf_bytes = bytes;

		// If the data could not be decrypted, ignore this packet
		if (_auth_enc.Decrypt(data, buf_bytes))
		{
			// Pass the packet to the transport layer
			_transport_receiver.OnPacket(this, data, buf_bytes, 0,
				fastdelegate::MakeDelegate(this, &Client::HandleMessageLayer));
		}
	}
	// s2c 01 (cookie[4]) (public key[64])
	else if (bytes == 1+4+PUBLIC_KEY_BYTES && data[0] == S2C_COOKIE)
	{
		u32 *in_cookie = reinterpret_cast<u32*>( data + 1 );
		u8 *in_public_key = data + 1+4;

		// Verify public key
		if (!SecureEqual(in_public_key, _server_public_key, PUBLIC_KEY_BYTES))
		{
			WARN("Client") << "Unable to connect: Server public key does not match expected key";
			Close();
			return;
		}

		// Allocate a post buffer
		static const int response_len = 1+4+CHALLENGE_BYTES;
		u8 *response = GetPostBuffer(response_len);

		if (!response)
		{
			WARN("Client") << "Unable to connect: Cannot allocate buffer for challenge message";
			Close();
			return;
		}

		// Construct challenge packet
		u32 *out_cookie = reinterpret_cast<u32*>( response + 1 );
		u8 *out_challenge = response + 1+4;

		response[0] = C2S_CHALLENGE;
		*out_cookie = *in_cookie;
		memcpy(out_challenge, _cached_challenge, CHALLENGE_BYTES);

		// Start ignoring ICMP unreachable messages now that we've seen a response from the server
		if (!IgnoreUnreachable())
		{
			WARN("Client") << "ICMP ignore unreachable failed";
		}

		// Attempt to post a response
		if (!Post(_server_addr, response, response_len))
		{
			WARN("Client") << "Unable to connect: Cannot post response to cookie";
			Close();
		}
		else
		{
			INANE("Client") << "Accepted cookie and posted challenge";
		}
	}
	// s2c 03 (server session port[2]) (answer[128])
	else if (bytes == 1+2+ANSWER_BYTES && data[0] == S2C_ANSWER)
	{
		Port *port = reinterpret_cast<Port*>( data + 1 );
		u8 *answer = data + 3;

		Port server_session_port = getLE(*port);

		// Ignore packet if the port doesn't make sense
		if (server_session_port > _server_addr.GetPort())
		{
			Skein key_hash;

			// Process answer from server, ignore invalid
			if (_key_agreement_initiator.ProcessAnswer(tls->math, answer, ANSWER_BYTES, &key_hash) &&
				_key_agreement_initiator.KeyEncryption(&key_hash, &_auth_enc, SESSION_KEY_NAME))
			{
				_connected = true;

				// Note: Will now only listen to packets from the session port
				_server_addr.SetPort(server_session_port);

				OnConnect();
			}
		}
	}
}

void Client::OnWrite(u32 bytes)
{

}

void Client::OnClose()
{
	if (!_connected)
	{
		OnConnectFail();
	}
	else
	{
		WARN("Client") << "Socket CLOSED.";
	}
}

void Client::OnConnectFail()
{
	WARN("Client") << "Connection failed.";
}

bool Client::PostHello()
{
	if (_connected)
	{
		WARN("Client") << "Refusing to post hello after connected";
		return false;
	}

	// Allocate space for a post buffer
	static const int hello_len = 1+4;
	u8 *hello = GetPostBuffer(hello_len);

	// If unable to allocate,
	if (!hello)
	{
		WARN("Client") << "Cannot allocate a post buffer for hello packet";
		return false;
	}

	u32 *magic = reinterpret_cast<u32*>( hello + 1 );

	// Construct packet
	hello[0] = C2S_HELLO;
	*magic = getLE(PROTOCOL_MAGIC);

	// Attempt to post packet
	if (!Post(_server_addr, hello, hello_len))
	{
		WARN("Client") << "Unable to post hello packet";
		return false;
	}

	INANE("Client") << "Posted hello packet";

	return true;
}

void Client::OnConnect()
{
	INFO("Client") << "Connected";
}

void Client::HandleMessageLayer(Connection *key, u8 *msg, int bytes)
{
	INFO("Client") << "Got message with " << bytes << " bytes";
}

void Client::OnDisconnect(bool timeout)
{
	WARN("Client") << "Disconnected. Timeout=" << timeout;
}

bool Client::ThreadFunction(void *)
{
	const int TICK_RATE = 10; // milliseconds
	const int HELLO_POST_INTERVAL = 200; // milliseconds
	const int CONNECT_TIMEOUT = 6000; // milliseconds

	u32 now = Clock::msec_fast();

	u32 first_hello_post = now;
	u32 last_hello_post = now;

	// While still not connected,
	while (!_connected)
	{
		// Wait for quit signal
		if (!WaitForQuitSignal(TICK_RATE))
			return false;

		// If now connected, break out
		if (_connected)
			break;

		u32 now = Clock::msec_fast();

		// If connection timed out,
		if (now - first_hello_post >= CONNECT_TIMEOUT)
		{
			// NOTE: Connection can complete before or after OnConnectFail()
			WARN("Client") << "Unable to connect: Timeout";
			Close();
			return false;
		}

		// If time to repost hello packet,
		if (now - last_hello_post >= HELLO_POST_INTERVAL)
		{
			if (!PostHello())
			{
				WARN("Client") << "Unable to connect: Post failure";
				return false;
			}

			last_hello_post = now;
		}
	}

	// While waiting for quit signal,
	while (WaitForQuitSignal(TICK_RATE))
	{
		_transport_receiver.Tick(this);
		_transport_sender.Tick(this);
	}

	return true;
}
