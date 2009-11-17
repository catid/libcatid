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

#include <cat/crypt/tunnel/EasyHandshake.hpp>
#include <cat/crypt/tunnel/KeyMaker.hpp>
using namespace cat;


//// EasyHandshake

EasyHandshake::EasyHandshake()
{
	// We really only need one of these per thread
	tls_math = KeyAgreementCommon::InstantiateMath(BITS);

	// Since ref() is not thread-safe usually it is called once
	// during startup before multiple threads start using this object
	tls_csprng = FortunaFactory::ref()->Create();
}

EasyHandshake::~EasyHandshake()
{
	if (tls_math) delete tls_math;
	if (tls_csprng) delete tls_csprng;
}

bool EasyHandshake::GenerateServerKey(u8 *public_key, u8 *private_key)
{
	KeyMaker bob;

	// Generate a random key pair
	return bob.GenerateKeyPair(tls_math, tls_csprng,
							   public_key, PUBLIC_KEY_BYTES,
							   private_key, PRIVATE_KEY_BYTES);
}


//// ServerEasyHandshake

ServerEasyHandshake::ServerEasyHandshake()
{
}

ServerEasyHandshake::~ServerEasyHandshake()
{
}

void ServerEasyHandshake::FillCookieJar(CookieJar *jar)
{
	jar->Initialize(tls_csprng);
}

bool ServerEasyHandshake::Initialize(const u8 *public_key, const u8 *private_key)
{
	// Initialize the tunnel server object using the provided key
    return tun_server.Initialize(tls_math, tls_csprng,
								 public_key, PUBLIC_KEY_BYTES,
								 private_key, PRIVATE_KEY_BYTES);
}

AuthenticatedEncryption *ServerEasyHandshake::ProcessChallenge(const u8 *challenge, u8 *answer)
{
	// Create a key hash object on the stack
	Skein key_hash;

	// Process and validate the client challenge.  This is an expensive operation
	// where most of the magic of the handshake occurs
	if (!tun_server.ProcessChallenge(tls_math, tls_csprng,
									 challenge, CHALLENGE_BYTES,
									 answer, ANSWER_BYTES, &key_hash))
	{
		return 0;
	}

	// Create an authenticated encryption object.  This could also be a member of the
	// client context structure created for each connected client
	AuthenticatedEncryption *auth_enc = new AuthenticatedEncryption;

	// Normally you would have the ability to key several authenticated encryption
	// objects from the same handshake, and give each one a different name.  For
	// simplicity I only allow one authenticated encryption object to be created per
	// handshake.  This would be useful for encrypting several different channels,
	// such as one handshake being used to key and encrypt a TCP stream and UDP
	// packets, or multiple TCP streams keyed from the same handshake, etc
	if (!tun_server.KeyEncryption(&key_hash, auth_enc, "EasyServerHandshake"))
	{
		delete auth_enc;
		return 0;
	}

	// Generate a proof that is the last quarter of the answer to the challenge,
	// which assures the client that the server is aware of the shared key
	if (!auth_enc->GenerateProof(answer + ANSWER_BYTES - PROOF_BYTES, PROOF_BYTES))
	{
		delete auth_enc;
		return 0;
	}

	// Return success indicated by a valid authenticated encryption object
	return auth_enc;
}


//// ClientEasyHandshake

ClientEasyHandshake::ClientEasyHandshake()
{
}

ClientEasyHandshake::~ClientEasyHandshake()
{
}

bool ClientEasyHandshake::Initialize(const u8 *public_key)
{
	// Initialize the tunnel client with the given public key
	return tun_client.Initialize(tls_math, public_key, PUBLIC_KEY_BYTES);
}

bool ClientEasyHandshake::GenerateChallenge(u8 *challenge)
{
	// Generate a challenge
    return tun_client.GenerateChallenge(tls_math, tls_csprng, challenge, CHALLENGE_BYTES);
}

AuthenticatedEncryption *ClientEasyHandshake::ProcessAnswer(const u8 *answer)
{
	// Create a key hash object on the stack
	Skein key_hash;

	// Process and validate the server's answer to our challenge.
	// This is an expensive operation
	if (!tun_client.ProcessAnswer(tls_math, answer, ANSWER_BYTES, &key_hash))
	{
		return 0;
	}

	// Create an authenticated encryption object.  This could also be a member of the
	// client context structure
	AuthenticatedEncryption *auth_enc = new AuthenticatedEncryption;

	// Normally you would have the ability to key several authenticated encryption
	// objects from the same handshake, and give each one a different name.  For
	// simplicity I only allow one authenticated encryption object to be created per
	// handshake.  This would be useful for encrypting several different channels,
	// such as one handshake being used to key and encrypt a TCP stream and UDP
	// packets, or multiple TCP streams keyed from the same handshake, etc
	if (!tun_client.KeyEncryption(&key_hash, auth_enc, "EasyClientHandshake"))
	{
		delete auth_enc;
		return 0;
	}

	// Validate the proof of key from the server, which is the last quarter of the
	// answer buffer
	if (!auth_enc->ValidateProof(answer + ANSWER_BYTES - PROOF_BYTES, PROOF_BYTES))
	{
		delete auth_enc;
		return 0;
	}

	// Erase the ephemeral private key we used for the handshake now that it is done
	tun_client.SecureErasePrivateKey();

	// Return success indicated by a valid authenticated encryption object
	return auth_enc;
}
