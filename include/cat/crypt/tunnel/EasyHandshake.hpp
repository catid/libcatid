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

#ifndef CAT_EASY_HANDSHAKE_HPP
#define CAT_EASY_HANDSHAKE_HPP

#include <cat/crypt/tunnel/KeyAgreementInitiator.hpp>
#include <cat/crypt/tunnel/KeyAgreementResponder.hpp>
#include <cat/crypt/cookie/CookieJar.hpp>

namespace cat {


/*
	The EasyHandshake classes implement a simplified version of the Tabby handshake protocol.

	It only works for single-threaded servers and only produces a single authenticated encryption
	object for each handshake.  Explanations on how to use the library with a multi-threaded
	server, and how to use one handshake to secure several TCP streams, are documented in the
	comments of these classes.


	Over the network, the handshake will look like this:

	client --> server : CHALLENGE (64 random-looking bytes)

	server --> client : ANSWER (128 random-looking bytes)

	client --> server : PROOF (32 random-looking bytes) + first encrypted packet can be appended here


	As far as coding goes, the function calls fit into the protocol like this:

	server is offline:

		----------------------------------------------------------------
		u8 public_key[EasyHandshake::PUBLIC_KEY_BYTES];
		u8 private_key[EasyHandshake::PRIVATE_KEY_BYTES];

		EasyHandshake temp;
		temp.GenerateServerKey(public_key, private_key);
		----------------------------------------------------------------

		(keys are stored to disk for reading on start-up)

		(public key is given to the client somehow)
			+ built into the client code
			+ provided by a trusted server

	server comes online:

		----------------------------------------------------------------
		ServerEasyHandshake server_handshake;

		server_handshake.Initialize(public_key, private_key);
		----------------------------------------------------------------

	client comes online:

		----------------------------------------------------------------
		ClientEasyHandshake client_handshake;

		client_handshake.Initialize(public_key);

		u8 challenge[EasyHandshake::CHALLENGE_BYTES];

		client_handshake.GenerateChallenge(challenge);
		----------------------------------------------------------------

	client --> server : CHALLENGE (64 random-looking bytes)

		----------------------------------------------------------------
		AuthenticatedEncryption *server_e;

		u8 answer[EasyHandshake::ANSWER_BYTES];

		server_e = server_handshake.ProcessChallenge(challenge, answer);
		----------------------------------------------------------------

	server --> client : ANSWER (128 random-looking bytes)

		----------------------------------------------------------------
		AuthenticatedEncryption *client_e;

		client_e = client_handshake.ProcessAnswer(answer);

		u8 proof[EasyHandshake::PROOF_BYTES];

		client_e->GenerateProof(proof, EasyHandshake::PROOF_BYTES);
		----------------------------------------------------------------

		Encryption example:
		----------------------------------------------------------------
		// Example message encryption of "Hello".  Note that encryption
		// inflates the size of the message by OVERHEAD_BYTES.
		const int PLAINTEXT_BYTES = 5;
		const int CIPHERTEXT_BYTES =
			PLAINTEXT_BYTES + AuthenticatedEncryption::OVERHEAD_BYTES;

		// Note that it makes room for message inflation
		u8 message[CIPHERTEXT_BYTES] = {
			'H', 'e', 'l', 'l', 'o'
		};

		// Note the second parameter is the number of plaintext bytes
		client_e->Encrypt(message, PLAINTEXT_BYTES);
		----------------------------------------------------------------

	client --> server : PROOF (32 random-looking bytes) + first encrypted packet can be appended here

		----------------------------------------------------------------
		server_e->ValidateProof(proof, EasyHandshake::PROOF_BYTES);
		----------------------------------------------------------------

		Decryption example:
		----------------------------------------------------------------
		// Note the second parameter is the number of ciphertext bytes
		server_e->Decrypt(message, CIPHERTEXT_BYTES);

		const int RECOVERED_BYTES = 
			CIPHERTEXT_BYTES - AuthenticatedEncryption::OVERHEAD_BYTES;

		// message is now decrypted and is RECOVERED_BYTES in length
		----------------------------------------------------------------

	NOTES:

		Once the authenticated encryption objects are created, if the messages received are always
		guaranteed to be in order, then the following flag can be set to make the object reject
		packets received out of order, which would indicate tampering:
			auth_enc->AllowOutOfOrder(false);
		By default the messages are assumed to arrive in any order up to 1024 messages out of order.

		The server similarly can encrypt messages the same way the client does in the examples.

		Encrypted messages are inflated by 11 random-looking bytes for a MAC and an IV.
		Modifications to the code can allow lower overhead if needed.

		The EasyHandshake classes are *NOT* THREAD-SAFE.

		The AuthenticatedEncryption class is *NOT* THREAD-SAFE.  Simultaneously, only ONE thread
		can be encrypting messages.  And only ONE thread can be decrypting messages.  Encryption
		and decryption are separate and safe to perform simultaneously.
*/


/*
	Common data needed for handshaking
*/
class EasyHandshake
{
protected:
	// Normally these would be created per-thread.
	// To free memory associated with these objects just delete them.
	BigTwistedEdwards *tls_math;
	FortunaOutput *tls_csprng;

public:
	static const int BITS = 256;
	static const int BYTES = BITS / 8;
	static const int PUBLIC_KEY_BYTES = BYTES * 2;
	static const int PRIVATE_KEY_BYTES = BYTES;
	static const int CHALLENGE_BYTES = BYTES * 2; // Packet # 1 in handshake, sent to server
	static const int ANSWER_BYTES = BYTES * 4; // Packet # 2 in handshake, sent to client
	static const int PROOF_BYTES = BYTES; // Packet # 3 in handshake, sent to server

public:
	// Demonstrates how to allocate and free the math and prng objects
	EasyHandshake();
	~EasyHandshake();

public:
	// Generate a server (public, private) key pair
	// Connecting clients will need to know the public key in order to connect
	bool GenerateServerKey(u8 *out_public_key /* EasyHandshake::PUBLIC_KEY_BYTES */,
						   u8 *out_private_key /* EasyHandshake::PRIVATE_KEY_BYTES */);
};

/*
	Implements the simple case of a server that performs handshakes with clients
	from a single thread.  Note that this implementation is not thread-safe.
*/
class ServerEasyHandshake : public EasyHandshake
{
	KeyAgreementResponder tun_server;

public:
	ServerEasyHandshake();
	~ServerEasyHandshake();

	// Prepare a cookie jar for hungry consumers
	void FillCookieJar(CookieJar *jar);

	// Provide the public and private key for the server, previously generated offline
	bool Initialize(const u8 *in_public_key /* EasyHandshake::PUBLIC_KEY_BYTES */,
					const u8 *in_private_key /* EasyHandshake::PRIVATE_KEY_BYTES */);

	// Process a client challenge and generate a server answer
	// Returns an encryptor if a session has been formed, or 0 if the challenge was invalid
	AuthenticatedEncryption *ProcessChallenge(const u8 *in_challenge /* EasyHandshake::CHALLENGE_BYTES */,
											  u8 *out_answer /* EasyHandshake::ANSWER_BYTES */);
};

/*
	Implements the simple case of a client that performs handshakes with servers
	from a single thread.  Note that this implementation is not thread-safe.
*/
class ClientEasyHandshake : public EasyHandshake
{
	KeyAgreementInitiator tun_client;

public:
	ClientEasyHandshake();
	~ClientEasyHandshake();

	// Provide the public key for the server, acquired through some secure means
	bool Initialize(const u8 *in_public_key /* EasyHandshake::PUBLIC_KEY_BYTES */);

	// Generate a challenge for the server to answer
	bool GenerateChallenge(u8 *out_challenge /* EasyHandshake::CHALLENGE_BYTES */);

	// Process a server answer to our challenge
	// Returns an encryptor if a session has been formed, or 0 if the answer was invalid
	AuthenticatedEncryption *ProcessAnswer(const u8 *in_answer /* EasyHandshake::ANSWER_BYTES */);
};


} // namespace cat

#endif // CAT_EASY_HANDSHAKE_HPP
