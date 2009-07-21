// 07/12/09 now uses new math code
// 06/12/09 refactored to use SwapLittleEndian
// 06/11/09 part of libcat-1.0

/*
	Addition and doubling formulas from
	Hisil–Wong–Carter–Dawson paper "Twisted Edwards Curves Revisited" (Asiacrypt 2008)

	w-MOF scalar multiplication from
	http://www.sdl.hitachi.co.jp/crypto/mof/index-e.html
*/

#ifndef CAT_ECDH_HPP
#define CAT_ECDH_HPP

#include <cat/rand/IRandom.hpp>
#include <cat/math/BigTwistedEdward.hpp>

namespace cat {


// Elliptic Curve Cryptography for Diffie-Hellman key exchange (EC-DH)

#define CAT_SERVER_PRIVATE_KEY_BYTES(bits) ((bits) / CAT_LEG_BITS * sizeof(Leg))
#define CAT_SERVER_PUBLIC_KEY_BYTES(bits) (CAT_SERVER_PRIVATE_KEY_BYTES(bits) * 4)


class TwistedEdwardCommon
{
	// for TwistedEdwardServer::GenerateOfflineStuff()
	static const int ECC_OVERHEAD = 17;

	// C: field prime modulus (2^256 - C)
	// D: curve (yy-xx=1+Dxxyy)
	static const int EDWARD_C_256 = 189;
	static const int EDWARD_D_256 = 321;
	static const int EDWARD_C_384 = 317;
	static const int EDWARD_D_384 = 2857;
	static const int EDWARD_C_512 = 569;
	static const int EDWARD_D_512 = 3042;

protected:
	int KeyBits, KeyLegs, KeyBytes;

public:
	bool Initialize(int bits);

	CAT_INLINE int GetKeyBits() { return KeyBits; }
	CAT_INLINE int GetSharedSecretBytes() { return KeyBytes; }
	CAT_INLINE int GetPrivateKeyBytes() { return KeyBytes; }
	CAT_INLINE int GetClientPublicKeyBytes() { return KeyBytes * 2; }
	CAT_INLINE int GetServerPublicKeyBytes() { return KeyBytes * 4; }

	static BigTwistedEdward *InstantiateMath(int bits);
	static BigTwistedEdward *GetThreadLocalMath(int bits);
	static void DeleteThreadLocalMath();

	// Limits on security level
	static const int MAX_BITS = 512;
	static const int MAX_BYTES = MAX_BITS / 8;
	static const int MAX_LEGS = MAX_BYTES / sizeof(Leg);
};


class TwistedEdwardServer : public TwistedEdwardCommon
{
	Leg PrivateKey[MAX_LEGS];

public:
	// Zeroes the private key
	~TwistedEdwardServer();

	// Pregenerate the server's private and public keys.
	static void GenerateOfflineStuff(int bits, u8 *ServerPrivateKey, u8 *ServerPublicKey);

	// Called on start-up to initialize the object
	void SetPrivateKey(const u8 *ServerPrivateKey);

	// Compute a shared secret from a client's public key.  Thread-safe.  False on invalid input.
	bool ComputeSharedSecret(const u8 *ClientPublicKey, u8 *SharedSecret);
};


class TwistedEdwardClient : public TwistedEdwardCommon
{
public:
	// Compute a shared secret and client's public key
	bool ComputeSharedSecret(const u8 *ServerPublicKey,
							 u8 *ClientPublicKey,
							 u8 *SharedSecret);
};


} // namespace cat

#endif // CAT_ECDH_HPP
