#include "SecureHandshake.hpp"
#include "Random.hpp"
#include "BigInt.hpp"
#include "Sha2.hpp"
using namespace big;

#include <iostream>
using namespace std;

/*
	class SecureHandshakeServer

		Provides the cryptographic routines to
		implement and abstract the server side
		of the secure handshake.
*/

SecureHandshakeServer::SecureHandshakeServer()
{
	p = 0;
	q = 0;
	qInv = 0;
	dP = 0;
	dQ = 0;
	modulus = 0;

	p_inv = 0;
	q_inv = 0;
	mod_inv = 0;
	mod_limbs = 0;
	factor_limbs = 0;
	e = 0;
}

SecureHandshakeServer::~SecureHandshakeServer()
{
	Clear();
}

void SecureHandshakeServer::Clear()
{
	if (p)
	{
		Set32(p, factor_limbs, 0);
		delete []p;
		p = 0;
	}

	if (q)
	{
		Set32(q, factor_limbs, 0);
		delete []q;
		q = 0;
	}

	if (qInv)
	{
		Set32(qInv, factor_limbs, 0);
		delete []qInv;
		qInv = 0;
	}

	if (dQ)
	{
		Set32(dQ, factor_limbs, 0);
		delete []dQ;
		dQ = 0;
	}

	if (dP)
	{
		Set32(dP, factor_limbs, 0);
		delete []dP;
		dP = 0;
	}

	if (modulus)
	{
		Set32(modulus, mod_limbs, 0);
		delete []modulus;
		modulus = 0;
	}

	p_inv = 0;
	q_inv = 0;
	mod_inv = 0;
	mod_limbs = 0;
	factor_limbs = 0;
	e = 0;
}

bool SecureHandshakeServer::SetPrivateKey(const u32 *pi, const u32 *qi, int factor_limbsi)
{
	Clear();

	factor_limbs = factor_limbsi;
	mod_limbs = factor_limbs * 2;

	p = new u32[factor_limbs];
	q = new u32[factor_limbs];
	modulus = new u32[mod_limbs];
	dP = new u32[factor_limbs];
	dQ = new u32[factor_limbs];
	qInv = new u32[factor_limbs];
	if (!p || !q || !qInv || !dP || !dQ || !modulus) return false;

	// Insure that p > q
	if (Greater(factor_limbs, pi, qi))
	{
		Set(p, factor_limbs, pi);
		Set(q, factor_limbs, qi);
	}
	else
	{
		Set(q, factor_limbs, pi);
		Set(p, factor_limbs, qi);
	}

	// p1 = p-1
	u32 *p1 = (u32 *)alloca(factor_limbs*4);
	Set(p1, factor_limbs, p);
	Subtract32(p1, factor_limbs, 1);

	// q1 = q-1
	u32 *q1 = (u32 *)alloca(factor_limbs*4);
	Set(q1, factor_limbs, q);
	Subtract32(q1, factor_limbs, 1);

	// e = first number relatively prime to phi, starting at 65537
	e = 65537-2;

	u32 r;
	do {
		e += 2;
		GCD(&e, 1, p1, factor_limbs, &r);
		if (r != 1) continue;
		GCD(&e, 1, q1, factor_limbs, &r);
	} while (r != 1 && e >= 65537);

	if (r != 1) return false;

	// modulus = p * q
	Multiply(factor_limbs, modulus, p, q);

	// dP = (1/e) mod (p-1)
	if (!InvMod(&e, 1, p1, factor_limbs, dP))
		return false;

	// dQ = (1/e) mod (q-1)
	if (!InvMod(&e, 1, q1, factor_limbs, dQ))
		return false;

	// qInv = (1/q) mod p
	if (!InvMod(q, factor_limbs, p, factor_limbs, qInv))
		return false;

	// Prepare for Montgomery multiplication
	p_inv = MonReducePrecomp(p[0]);
	q_inv = MonReducePrecomp(q[0]);
	mod_inv = MonReducePrecomp(modulus[0]);

	return true;
}

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
bool SecureHandshakeServer::GenerateKey(int bits)
{
	if (bits % (32 * 2) != 0 || bits < SECURE_HANDSHAKE_MIN_BITS || bits > SECURE_HANDSHAKE_MAX_BITS)
		return false;

	int factor_limbs = bits / 32 / 2;

	u32 *p = (u32 *)alloca(factor_limbs*4);
	u32 *q = (u32 *)alloca(factor_limbs*4);

	do {
		GenerateStrongPseudoPrime(p, factor_limbs);
		GenerateStrongPseudoPrime(q, factor_limbs);
	} while (!SetPrivateKey(p, q, factor_limbs));

	return true;
}

/*
	SetPrivateKey()

		Set a server's private key from disk or some other storage location.

		Returns false if the private key is invalid.  Cannot check if the key has
		been corrupted somehow, so if you want that feature you will have to store
		a checksum with the key and verify it outside of this class.

	This must be protected from read access by other users, so that the private
	key for all the connected users is not visible to the world.
*/
bool SecureHandshakeServer::SetPrivateKey(const void *private_key_buffer, int buffer_size_bytes)
{
	int factor_limbs = buffer_size_bytes / 8;

	if (buffer_size_bytes % 8 != 0 || factor_limbs < SECURE_HANDSHAKE_MIN_BITS/32/2 || factor_limbs > SECURE_HANDSHAKE_MAX_BITS/32/2)
		return false;

	u32 *pf = (u32 *)alloca(factor_limbs*4);
	u32 *qf = (u32 *)alloca(factor_limbs*4);

	memcpy(pf, private_key_buffer, factor_limbs*4);
	memcpy(qf, (const u32*)private_key_buffer + factor_limbs, factor_limbs*4);

	FromLittleEndian(pf, factor_limbs);
	FromLittleEndian(qf, factor_limbs);

	return SetPrivateKey(pf, qf, factor_limbs);
}

/*
	GetPublicKey()

		Return the server's public key.  This key is compatible with SetPublicKey().

	This key should be known ahead of time by the connecting client.
*/
bool SecureHandshakeServer::GetPublicKey(void *public_key_buffer, int buffer_size_bytes)
{
	if (buffer_size_bytes != GetPublicKeyBytes())
		return false;

	u32 *out = (u32*)public_key_buffer;

	out[0] = e;
	memcpy(&out[1], modulus, mod_limbs * 4);
	ToLittleEndian(out, mod_limbs + 1);

	return true;
}

int SecureHandshakeServer::GetPublicKeyBytes()
{
	return 4 + mod_limbs * 4;
}

/*
	GetPrivateKey()

		Return the server's private key.  This key is compatible with SetPrivateKey().

	This key must remain secret.  Read the rest of this header for more information.
*/
bool SecureHandshakeServer::GetPrivateKey(void *private_key_buffer, int buffer_size_bytes)
{
	if (buffer_size_bytes != GetPrivateKeyBytes())
		return false;

	u32 *out = (u32*)private_key_buffer;

	memcpy(out, p, factor_limbs);
	memcpy(&out[factor_limbs], q, factor_limbs);
	ToLittleEndian(out, factor_limbs * 2);

	return true;
}

int SecureHandshakeServer::GetPrivateKeyBytes()
{
	return factor_limbs * 2 * 4;
}

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
*/
bool SecureHandshakeServer::GenerateA(void *A_buffer, int buffer_size_bytes)
{
	if (buffer_size_bytes != SECURE_HANDSHAKE_KEY_BITS/8)
		return false;

	Random::ref()->generate(A_buffer, buffer_size_bytes);

	// Byte order does not matter for A.. it is not used for any math
	//ToLittleEndian((u32*)a_buffer, SECURE_HANDSHAKE_KEY_BITS/32);

	return true;
}

bool SecureHandshakeServer::Decrypt(u32 *pt, const u32 *ct)
{
	if (!e) return false;

	// CRT method

	// s_p = c ^ dP mod p
	u32 *s_p = (u32*)alloca(factor_limbs*4);
	ExpMod(ct, mod_limbs, dP, factor_limbs, p, factor_limbs, p_inv, s_p);

	// s_q = c ^ dQ mod q
	u32 *s_q = (u32*)alloca(factor_limbs*4);
	ExpMod(ct, mod_limbs, dQ, factor_limbs, q, factor_limbs, q_inv, s_q);

	// Garner's CRT recombination

	// s_p = qInv(s_p - s_q) mod p
	if (Subtract(s_p, factor_limbs, s_q, factor_limbs))
		Add(s_p, factor_limbs, p, factor_limbs);
	MulMod(factor_limbs, qInv, s_p, p, s_p);

	// pt = s_q + s_p*q
	Multiply(factor_limbs, pt, s_p, q);
	Add(pt, mod_limbs, s_q, factor_limbs);

	return true;
}

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
*/
bool SecureHandshakeServer::DecryptSessionKeys(const void *A, int A_bytes,						// 256-bit buffer
											   const void *EncryptedSessionKey, int ESK_bytes,	// 1024-bit buffer
											   void *ServerKey, int SK_bytes,					// 256-bit buffer
											   void *ClientKey, int CK_bytes)					// 256-bit buffer
{
	const int hash_limbs = SECURE_HANDSHAKE_KEY_BITS/32;

	// Verify the ESK size
	if (ESK_bytes != mod_limbs * 4 + 4 || A_bytes != hash_limbs * 4 || SK_bytes != hash_limbs * 4 || CK_bytes != hash_limbs * 4)
		return false;

	// ESK = A[0] || RSA { B, H(A,B) }
	const u32 *ESK = (const u32*)EncryptedSessionKey;

	// If the client didn't send back the correct value for A, abort
	if (*(u32*)A != ESK[0]) // note: A[0] and in[0] are both little-endian
		return false;

	// Decrypt the encrypted message
	u32 *pt = (u32*)alloca(mod_limbs*4);

	memcpy(pt, &ESK[1], mod_limbs*4);
	FromLittleEndian(pt, mod_limbs);

	Decrypt(pt, pt);

	// Convert decrypted B to little endian for hashing
	ToLittleEndian(&pt[hash_limbs], mod_limbs - hash_limbs);

	// SHA-256 hash { A, B }, A and B are in little endian byte order
	DigestSHA256 digest;
	digest.performDigest(A, A_bytes);
	digest.performDigest(&pt[hash_limbs], (mod_limbs - hash_limbs) * 4);
	u32 *AB_hash = digest.endDigest();

	// Validate the hash
	if (0 != memcmp(AB_hash, pt, hash_limbs*4))
		return false;

	// ClientKey = (big-endian hash)
	memcpy(ClientKey, AB_hash, hash_limbs * 4);

	// ServerKey = (little-endian B) xor (big-endian hash)
	memcpy(ServerKey, AB_hash, hash_limbs * 4);
	Xor(hash_limbs, (u32*)ServerKey, &pt[hash_limbs]);

	return true;
}

int SecureHandshakeServer::GetEncryptedSessionKeyBytes()
{
	return 4 + mod_limbs * 4;
}


/*
	class SecureHandshakeClient

		Provides the cryptographic routines to
		implement and abstract the client side
		of the secure handshake.
*/

SecureHandshakeClient::SecureHandshakeClient()
{
	modulus = 0;

	mod_inv = 0;
	mod_limbs = 0;
	e = 0;
}

SecureHandshakeClient::~SecureHandshakeClient()
{
	Clear();
}

void SecureHandshakeClient::Clear()
{
	if (modulus)
	{
		Set32(modulus, mod_limbs, 0);
		delete []modulus;
		modulus = 0;
	}

	mod_inv = 0;
	mod_limbs = 0;
	e = 0;
}

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
bool SecureHandshakeClient::SetPublicKey(const void *public_key_buffer, int buffer_size_bytes)
{
	// Validate buffer size bytes
	if (buffer_size_bytes % 4 != 0 || buffer_size_bytes < (SECURE_HANDSHAKE_MIN_BITS + 32)/8 || buffer_size_bytes > (SECURE_HANDSHAKE_MAX_BITS + 32)/8)
		return false;

	Clear();

	// Calculate modulus limbs from buffer size
	mod_limbs = buffer_size_bytes / 4 - 1;

	// Validate modulus limbs
	if (mod_limbs % 2 != 0)
		return false;

	modulus = new u32[mod_limbs];
	if (!modulus) return false;

	const u32 *in = (const u32*)public_key_buffer;

	// Recover public key exponent
	e = getLE(in[0]);

	// Copy the little endian modulus
	memcpy(modulus, &in[1], mod_limbs * 4);
	FromLittleEndian(modulus, mod_limbs);

	// Store the modulus low word inverse
	mod_inv = MonReducePrecomp(modulus[0]);

	return true;
}

bool SecureHandshakeClient::Encrypt(u32 *ct, const u32 *pt)
{
	if (!e) return false;

	ExpMod(pt, mod_limbs, &e, 1, modulus, mod_limbs, mod_inv, ct);

	return true;
}

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
bool SecureHandshakeClient::EncryptSessionKeys(const void *A, int A_bytes,					// 256-bit buffer
											   void *EncryptedSessionKey, int ESK_bytes,	// 1024-bit buffer
											   void *ServerKey, int SK_bytes,				// 256-bit buffer
											   void *ClientKey, int CK_bytes)				// 256-bit buffer
{
	const int hash_limbs = SECURE_HANDSHAKE_KEY_BITS/32;

	// Verify the ESK size
	if (ESK_bytes != mod_limbs * 4 + 4 || A_bytes != hash_limbs * 4 || SK_bytes != hash_limbs * 4 || CK_bytes != hash_limbs * 4)
		return false;

	// ESK = A[0] || RSA { B, H(A,B) }

	u32 *ESK = (u32*)EncryptedSessionKey;

	ESK[0] = *(u32*)A;

	u32 *pt = ESK + 1;

	// Generate B
	do {
		Random::ref()->generate(pt, mod_limbs * 4);

		/*
			The modulus is designed with two primes with the MSB set,
			this means in the worst case 1000 x 1000 = 0100 0000, so
			if we zero the high bit we will be able to generate a
			message that is at most one modulus too large.
		*/
		pt[mod_limbs-1] >>= 1; // zero high bit of message

		// Keep the random padding within the modulus (will be at most one modulus too large)
		if (GreaterOrEqual(pt, mod_limbs, modulus, mod_limbs))
			Subtract(pt, mod_limbs, modulus, mod_limbs);

		// Subtract 1 from the high padding to insure that it is always under the modulus regardless of message content
	} while (0 != Subtract32(&pt[hash_limbs], mod_limbs - hash_limbs, 1));

	ToLittleEndian(&pt[hash_limbs], mod_limbs - hash_limbs); // hash an endian-neutral version of B

	// Calculate H(A,B)
	DigestSHA256 digest;
	digest.performDigest(A, A_bytes);
	u32 test0 = pt[hash_limbs];
	digest.performDigest(&pt[hash_limbs], (mod_limbs - hash_limbs) * 4);
	u32 *AB_hash = digest.endDigest();

	// ClientKey = (big-endian hash)
	memcpy(ClientKey, AB_hash, hash_limbs * 4);

	// ServerKey = (big-endian hash) xor (little endian B)
	memcpy(ServerKey, AB_hash, hash_limbs * 4);
	Xor(hash_limbs, (u32*)ServerKey, &pt[hash_limbs]);

	FromLittleEndian(&pt[hash_limbs], mod_limbs - hash_limbs); // undo the conversion to little endian for RSA encryption

	// Write the hash into the plaintext
	memcpy(pt, AB_hash, hash_limbs * 4);

	// Encrypt the plaintext
	Encrypt(pt, pt);

	// Convert it to little endian 
	ToLittleEndian(pt, mod_limbs);

	return true;
}

int SecureHandshakeClient::GetEncryptedSessionKeyBytes()
{
	return 4 + mod_limbs * 4;
}
