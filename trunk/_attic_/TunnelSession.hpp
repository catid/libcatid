// 07/14/2009 works again
// 07/01/2009 uses refactored math library
// 06/13/2009 first version

#ifndef CAT_TUNNEL_SESSION_HPP
#define CAT_TUNNEL_SESSION_HPP

#include <cat/crypt/symmetric/ChaCha.hpp>
#include <cat/crypt/publickey/TwistedEdward.hpp>
#include <cat/crypt/hash/Skein.hpp>
#include <cat/crypt/hash/HMAC_MD5.hpp>

namespace cat {


/*
	Tunnel supports two Key Exchange protocols followed by Authenticated Encryption.
*/

/*
	Tunnel Key Exchange (TKE) protocol:

	Fast.  Provides security so long as no secrets are ever revealed.
	Useful for encrypting data that does not need a long-term security guarantee (ie. games).

	Using Elliptic Curve Cryptography over finite field Fq, cofactor h

		s: generator point G, private key a, public key A=a*G
		c: private key b, public key B=b*G
		c: SECRET=b*h*A

	c2s cseed || B || MAC(SECRET,cseed,B) { "client-challenge" || OOB-data } || OOB-data

		256-bit security: CSEED(32by) B(64by) MAC(32by) = 128 bytes
		384-bit security: CSEED(48by) B(96by) MAC(48by) = 192 bytes
		512-bit security: CSEED(64by) B(128by) MAC(64by) = 256 bytes

		s: SECRET=a*h*B
		s: validate MAC, ignore invalid
		s: k=KDF(SECRET,cseed,sseed)

	s2c sseed || MAC(SECRET,cseed,B) { "server-response" || sseed || OOB-data } || OOB-data

		256-bit security: SSEED(32by) MAC(32by) = 64 bytes
		384-bit security: SSEED(48by) MAC(48by) = 96 bytes
		512-bit security: SSEED(64by) MAC(64by) = 128 bytes

		c: validate MAC, ignore invalid
		c: k=KDF(SECRET,cseed,sseed)
*/

/*
	Tunnel Authenticated Encryption protocol:

	Run after either of the above Key Exchange protocols complete.

	Uses a 1024-bit anti-replay sliding window, efficient for Internet file transfer over UDP.

	The cipher used is limited to either 256-bit or 384-bit security, but this is sufficient.

	c2sMKey = KDF(k) { "upstream-MAC" }
	s2cMKey = KDF(k) { "downstream-MAC" }
	c2sEKey = KDF(k) { "upstream-ENC" }
	s2cEKey = KDF(k) { "downstream-ENC" }

	c2s Encrypt(c2sEKey) { message || MAC(c2sMKey) { full-iv-us||message } } || Obfuscated { trunc-iv-us }

		encrypted { MESSAGE(X) MAC(8by) } IV(3by) = 11 bytes overhead at end of packet

	s2c Encrypt(s2cEKey) { message || MAC(s2cMKey) { full-iv-ds||message } } || Obfuscated { trunc-iv-ds }

		encrypted { MESSAGE(X) MAC(8by) } IV(3by) = 11 bytes overhead at end of packet
*/


class TunnelServer;
class TunnelClient;
class TunnelSession;


//// TunnelSession

class TunnelSession
{
	friend class TunnelClient;
	friend class TunnelServer;

	HMAC_MD5 local_mac, remote_mac;
	ChaCha local_cipher, remote_cipher;
	u64 local_iv, remote_iv;

	// 1024-bit anti-replay sliding window
	static const int BITMAP_BITS = 1024;
	static const int BITMAP_WORDS = BITMAP_BITS / 64;
	u64 iv_bitmap[BITMAP_WORDS];

public:
	// Tunnel overhead bytes
	static const int MAC_BYTES = 8;
	static const int IV_BYTES = 3;
	static const int OVERHEAD_BYTES = IV_BYTES + MAC_BYTES;

	// IV constants
	static const int IV_BITS = IV_BYTES * 8;
	static const u32 IV_MSB = (1 << IV_BITS);
	static const u32 IV_MASK = (IV_MSB - 1);
	static const u32 IV_FUZZ = 0xCA7DCA7D;

	// Reconstruct a whole IV given the last accepted IV
	// Assumes the IV increments by 1 each time
	static u64 ReconstructIV(u64 last_accepted_iv, u32 new_iv_low_bits);

protected:
	void SetKey(int KeyBytes, const u8 *shared_secret, const u8 *client_seed, const u8 *server_seed, bool is_client);

	bool IsValidIV(u64 iv);
	void AcceptIV(u64 iv);

public:
	// Decrypt a packet from the remote host using the default IV counter
	// Overhead is OVERHEAD_BYTES bytes at the end of the packet
	// Returns false if the message is invalid.  Invalid messages should just be ignored as if they were never received
	bool Decrypt(u8 *buffer, int buf_bytes);

	// Encrypt a packet to send to the remote host using the default IV counter
	// Overhead is OVERHEAD_BYTES bytes at the end of the packet
	void Encrypt(u8 *buffer, int msg_bytes);
};



} // namespace cat

#endif // CAT_TUNNEL_SESSION_HPP
