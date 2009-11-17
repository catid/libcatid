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

bool EasyHandshake::GenerateServerKey(void *out_public_key, void *out_private_key)
{
	u8 *public_key = reinterpret_cast<u8*>( out_public_key );
	u8 *private_key = reinterpret_cast<u8*>( out_private_key );

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

bool ServerEasyHandshake::Initialize(const void *in_public_key, const void *in_private_key)
{
	const u8 *public_key = reinterpret_cast<const u8*>( in_public_key );
	const u8 *private_key = reinterpret_cast<const u8*>( in_private_key );

	// Initialize the tunnel server object using the provided key
    return tun_server.Initialize(tls_math, tls_csprng,
								 public_key, PUBLIC_KEY_BYTES,
								 private_key, PRIVATE_KEY_BYTES);
}

bool ServerEasyHandshake::ProcessChallenge(const void *in_challenge, void *out_answer, AuthenticatedEncryption *auth_enc)
{
	const u8 *challenge = reinterpret_cast<const u8*>( in_challenge );
	u8 *answer = reinterpret_cast<u8*>( out_answer );

	// Create a key hash object on the stack
	Skein key_hash;

	// Process and validate the client challenge.  This is an expensive operation
	// where most of the magic of the handshake occurs
	if (!tun_server.ProcessChallenge(tls_math, tls_csprng,
									 challenge, CHALLENGE_BYTES,
									 answer, ANSWER_BYTES, &key_hash))
	{
		return false;
	}

	// Normally you would have the ability to key several authenticated encryption
	// objects from the same handshake, and give each one a different name.  For
	// simplicity I only allow one authenticated encryption object to be created per
	// handshake.  This would be useful for encrypting several different channels,
	// such as one handshake being used to key and encrypt a TCP stream and UDP
	// packets, or multiple TCP streams keyed from the same handshake, etc
	if (!tun_server.KeyEncryption(&key_hash, auth_enc, "EasyServerHandshake"))
		return false;

	// Generate a proof that is the last quarter of the answer to the challenge,
	// which assures the client that the server is aware of the shared key
	return auth_enc->GenerateProof(answer + ANSWER_BYTES - PROOF_BYTES, PROOF_BYTES);
}


//// ClientEasyHandshake

ClientEasyHandshake::ClientEasyHandshake()
{
}

ClientEasyHandshake::~ClientEasyHandshake()
{
}

bool ClientEasyHandshake::Initialize(const void *in_public_key)
{
	const u8 *public_key = reinterpret_cast<const u8*>( in_public_key );

	// Initialize the tunnel client with the given public key
	return tun_client.Initialize(tls_math, public_key, PUBLIC_KEY_BYTES);
}

bool ClientEasyHandshake::GenerateChallenge(void *out_challenge)
{
	u8 *challenge = reinterpret_cast<u8*>( out_challenge );

	// Generate a challenge
    return tun_client.GenerateChallenge(tls_math, tls_csprng, challenge, CHALLENGE_BYTES);
}

bool ClientEasyHandshake::ProcessAnswer(const void *in_answer, AuthenticatedEncryption *auth_enc)
{
	const u8 *answer = reinterpret_cast<const u8*>( in_answer );

	// Create a key hash object on the stack
	Skein key_hash;

	// Process and validate the server's answer to our challenge.
	// This is an expensive operation
	if (!tun_client.ProcessAnswer(tls_math, answer, ANSWER_BYTES, &key_hash))
		return false;

	// Normally you would have the ability to key several authenticated encryption
	// objects from the same handshake, and give each one a different name.  For
	// simplicity I only allow one authenticated encryption object to be created per
	// handshake.  This would be useful for encrypting several different channels,
	// such as one handshake being used to key and encrypt a TCP stream and UDP
	// packets, or multiple TCP streams keyed from the same handshake, etc
	if (!tun_client.KeyEncryption(&key_hash, auth_enc, "EasyClientHandshake"))
		return false;

	// Validate the proof of key from the server, which is the last quarter of the
	// answer buffer
	if (!auth_enc->ValidateProof(answer + ANSWER_BYTES - PROOF_BYTES, PROOF_BYTES))
		return false;

	// Erase the ephemeral private key we used for the handshake now that it is done
	tun_client.SecureErasePrivateKey();

	return true;
}
