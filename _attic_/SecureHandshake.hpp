// 10/08/08 misc cleanups; add note about Intel compiler

#ifndef SECURE_HANDSHAKE_HPP
#define SECURE_HANDSHAKE_HPP

/*
		--Secure handshake details--

	The client must know the server's public key ahead of time.

	The public key may be stored in the client's executable, or
	sent from a trusted server over a secure connection.  It is
	not permitted to send the server's public key over an
	unsecured connection during runtime, as this will invalidate
	any security and tamperless guarantees made by the handshake.

	-- Example usage:

	c2s 00 "hello"

		Client says hello.

		A:
	s2c 01 (256-bit random number "A")

		Server creates a context for the client, to
		remember the number "A" that it sent back.

		EncryptedSessionKey:
	c2s 02 (32 bits of "A"),
		   RSA-1024 {
				(padding + 256-bit random number "B"),
				(256-bit hash using SHA-256{A,padding+B} throwing away the low half)
			}

		Client sends a proof that it received the
		value of "A" from the server just now, and
		then sends its own random number "B", and a
		hash of A and B, encrypted with the server's
		public key.  Random padding is used so that
		the entire message body that is encrypted
		with RSA has no structure.

	s2c 03 "connected!" or "failure!"

		Server checks to make sure that the 32 bits
		of "A" match what it sent out, and then it
		decrypts the RSA-encrypted message with its
		private key.  The hash is performed again by
		the server and the hash from the client is
		verified, to insure that the RSA message is
		not corrupted.

		The client uses the 256-bit key: (256-bit hash)
		The server uses the 256-bit key: B xor (256-bit hash)

	-- Note:

	It is not necessary to understand all the details of the handshake
	in order to use it, but they are presented here for the curious.

	Read the documentation below for instructions and examples.
*/

#include "Platform.hpp"

/*
	Session key decryption and validation timing results on an overclocked
	Core2 Quad at 3.2 GHz with DDR3 RAM at 1600 MHz running Windows XP:

	512 bits: 0.251 ms (average)
	1024 bits: 1.483 ms (average)
	1536 bits: 4.42 ms (average)
	2048 bits: 9.77 ms (average)

	Compiling with the free Intel compiler that integrates with VS.NET
	with full optimization and removing the assembly code I wrote improves
	performance by at least 20%.  I highly recommend using ICC.
*/

static const int SECURE_HANDSHAKE_MIN_BITS = 512;	// restricted by KEY_BITS and security guarantee
static const int SECURE_HANDSHAKE_MAX_BITS = 2048;	// restricted by high CPU time required to decrypt
static const int SECURE_HANDSHAKE_KEY_BITS = 1024;	// recommended

static const int SECURE_HANDSHAKE_A_BYTES = SECURE_HANDSHAKE_KEY_BITS / 8;
static const int SECURE_HANDSHAKE_KEY_BYTES = SECURE_HANDSHAKE_KEY_BITS / 8;


/*
	class SecureHandshakeServer

		Provides the cryptographic routines to
		implement and abstract the server side
		of the secure handshake.

	The server should just have one instance of this object for all its connections to use.

	The server will load this object with its private key before
	using this object to decrypt session keys from connecting clients.
*/
class SecureHandshakeServer
{
	u32 *p, p_inv, *q, q_inv, *qInv, *dP, *dQ, factor_limbs;
	u32 e, *modulus, mod_inv, mod_limbs;

private:
	void Clear();

	bool GenerateExponents(const u32 *p, const u32 *q, int limbs, u32 &e, u32 *d);

	bool SetPrivateKey(const u32 *pi, const u32 *qi, int factor_limbsi);

	bool Decrypt(u32 *pt, const u32 *ct);

public:
	SecureHandshakeServer();
	~SecureHandshakeServer();

public:
	/*
		GenerateKey(int bits)

			Generate a key for the server to use.

			Returns false if the number of bits is not supported.

			This operation takes a LONG TIME.  Only do this once and save the result.

			Accepts values between 512 and 2048 bits, multiples of 32 bits.

			The clients must know the public part of this key before attempting
			to connect, so it either must be part of the client's executable
			(e.g. a companion file, embedded into the data section), or the public
			part of the key must be sent over a secure channel
			(e.g. sent as part of a server directory list for each one in the list).

		Specifies the number of bits in the RSA modulus used to secure the handshake.

		It is a good idea to change this key every 6 months or so.

		Key bits are limited on the low end by the size of the message that needs
		to be transmitted and minimum security guarantee, and on the high end by
		how slow decryption can get after 2048 bits.
	*/
	bool GenerateKey(int bits);

	/*
		SetPrivateKey()

			Set a server's private key from disk or some other storage location.

			Returns false if the private key is invalid.  Cannot check if the key has
			been corrupted somehow, so if you want that feature you will have to store
			a checksum with the key and verify it outside of this class.

		This must be protected from read access by other users, so that the private
		key for all the connected users is not visible to the world.
	*/
	bool SetPrivateKey(const void *private_key_buffer, int buffer_size_bytes);

	/*
		GetPublicKey()

			Return the server's public key.  This key is compatible with SetPublicKey().

		This key should be known ahead of time by the connecting client.
	*/
	bool GetPublicKey(void *public_key_buffer, int buffer_size_bytes);
	int GetPublicKeyBytes();

	/*
		GetPrivateKey()

			Return the server's private key.  This key is compatible with SetPrivateKey().

		This key must remain secret.  Read the rest of this header for more information.
	*/
	bool GetPrivateKey(void *private_key_buffer, int buffer_size_bytes);
	int GetPrivateKeyBytes();

	/*
		GenerateA()

			Generates the random number "A" that the client must
			know before he sends the session key to the server.

			This should be regenerated for each connection with the server.

			This key is compatible with EncryptSessionKeys() and DecryptSessionKeys().

		The server should generate "A" and send it to the client, so that the
		client can run EncryptSessionKeys().  The client will send the encrypted
		session key to the server which will run DecryptSessionKeys().

		This is just a 256-bit random number used as a salt in the handshake.
		See the handshake details at the top for more information.

		This would be thread-safe if the random number generator was thread-safe.
	*/
	bool GenerateA(void *A_buffer, int buffer_size_bytes);

	/*
		DecryptSessionKeys()

			Returns true if the client's key is valid.
			Please do handle a return value of false appropriately.

			Parameter (in) A: The return value of GenerateA().
			Parameter (in) EncryptedSessionKey: The return value of EncryptSessionKey().

			Parameter (out) ServerKey: Set to the decrypted server key.
			Parameter (out) ClientKey: Set to the decrypted client key.

		If the function returns true, ServerKey and ClientKey are set to the client and server keys for the session.

		A return value of false means that the client sent us bad data.  Either ignore the message or disconnect the client.

		This operation is somewhat slow (a few milliseconds for 1024-bit RSA on average hardware).

		This function is thread-safe.
	*/
	bool DecryptSessionKeys(const void *A, int A_bytes,							// 256-bit buffer
							const void *EncryptedSessionKey, int ESK_bytes,		// 1024-bit buffer
							void *ServerKey, int SK_bytes,						// 256-bit buffer
							void *ClientKey, int CK_bytes);						// 256-bit buffer
	int GetEncryptedSessionKeyBytes();
};


/*
	class SecureHandshakeClient

		Provides the cryptographic routines to
		implement and abstract the client side
		of the secure handshake.
*/
class SecureHandshakeClient
{
	u32 e, *modulus, mod_inv, mod_limbs;

private:
	void Clear();

	bool Encrypt(u32 *ct, const u32 *pt);

public:
	SecureHandshakeClient();
	~SecureHandshakeClient();

public:
	/*
		SetPublicKey()

			Set a server's public key from a secure remotely tamper-proof location,
			e.g. stored inside the executable or received over a secure connection.

			Returns false if the public key is invalid.  Cannot check if the key has
			been corrupted somehow, so if you want that feature you will have to store
			a checksum with the key and verify it outside of this class.

		As stated all over the place in this header, do not have the server send its
		public key to the client.  The client must know this public key ahead of time
		or else the security guarantees made by the handshake will disappear.
	*/
	bool SetPublicKey(const void *public_key_buffer, int buffer_size_bytes);

	/*
		EncryptSessionKeys()

			Generate the server and client key and the encrypted session key to be sent
			to the server, which will process it with DecryptSessionKeys().

			Returns true if "A" is valid.
			Please do handle a return value of false appropriately.

			Parameter (in) A: The return value of GenerateA() from the server.

			Parameter (out) ServerKey: Set to the generated server key.
			Parameter (out) ClientKey: Set to the generated client key.
			Parameter (out) EncryptedSessionKey: Set to the generated encrypted session key.

		Do not share the server key nor the client key with the server; only send
		the EncryptedSessionKey.

		Precondition: SetPublicKey() has been called.
	*/
	bool EncryptSessionKeys(const void *A, int A_bytes,					// 256-bit buffer
							void *EncryptedSessionKey, int ESK_bytes,	// 1024-bit buffer
							void *ServerKey, int SK_bytes,				// 256-bit buffer
							void *ClientKey, int CK_bytes);				// 256-bit buffer
	int GetEncryptedSessionKeyBytes();
};

#endif // include guard
