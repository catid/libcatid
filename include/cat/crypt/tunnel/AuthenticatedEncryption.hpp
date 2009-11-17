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

// 07/14/2009 works again
// 07/01/2009 uses refactored math library
// 06/13/2009 first version

#ifndef CAT_AUTHENTICATED_ENCRYPTION_HPP
#define CAT_AUTHENTICATED_ENCRYPTION_HPP

#include <cat/crypt/symmetric/ChaCha.hpp>
#include <cat/crypt/hash/Skein.hpp>
#include <cat/crypt/hash/HMAC_MD5.hpp>

namespace cat {


/*
    Tunnel Authenticated Encryption "Calico" protocol:

    Run after the Key Agreement protocol completes.
    Uses a 1024-bit anti-replay sliding window, suitable for Internet file transfer over UDP.

	Cipher: 12-round ChaCha with 256-bit or 384-bit keys
	KDF: Key derivation function (Skein)
	MAC: 64-bit truncated HMAC-MD5
	IV: Initialization vector always starting at 0 and incrementing by 1 each time

    c2sMKey = KDF(k) { "upstream-MAC" }
    s2cMKey = KDF(k) { "downstream-MAC" }
    c2sEKey = KDF(k) { "upstream-ENC" }
    s2cEKey = KDF(k) { "downstream-ENC" }

	To transmit a message, the client calculates a MAC with the c2sMKey of the IV concatenated with
	the plaintext message and then appends the 8-byte MAC and low 3 bytes of the IV to the message,
	which is encrypted using the c2sEKey and the IV.

    c2s Encrypt(c2sEKey) { message || MAC(c2sMKey) { full-iv-us||message } } || Obfuscated { trunc-iv-us }

        encrypted { MESSAGE(X) MAC(8by) } IV(3by) = 11 bytes overhead at end of packet

	To transmit a message, the server calculates a MAC with the s2cMKey of the IV concatenated with
	the plaintext message and then appends the 8-byte MAC and low 3 bytes of the IV to the message,
	which is encrypted using the s2cEKey and the IV.

    s2c Encrypt(s2cEKey) { message || MAC(s2cMKey) { full-iv-ds||message } } || Obfuscated { trunc-iv-ds }

        encrypted { MESSAGE(X) MAC(8by) } IV(3by) = 11 bytes overhead at end of packet
*/


class KeyAgreementResponder;
class KeyAgreementInitiator;


// This class is NOT THREAD-SAFE.
class AuthenticatedEncryption
{
    friend class KeyAgreementResponder;
    friend class KeyAgreementInitiator;

    bool is_initiator, accept_out_of_order;
    Skein key_hash;

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
    bool SetKey(int KeyBytes, Skein *key, bool is_initiator, const char *key_name);

    bool IsValidIV(u64 iv);
    void AcceptIV(u64 iv);

public:
    // Generate a proof that the local host has the key
    bool GenerateProof(u8 *local_proof, int proof_bytes);

    // Validate a proof that the remote host has the key
    bool ValidateProof(const u8 *remote_proof, int proof_bytes);

public:
	void AllowOutOfOrder(bool allowed = true) { accept_out_of_order = allowed; }

public:
    // Overhead is OVERHEAD_BYTES bytes at the end of the packet
    // Returns false if the message is invalid.  Invalid messages should just be ignored as if they were never received
    // buf_bytes: Number of bytes in the buffer, including the overhead
    bool Decrypt(u8 *buffer, int buf_bytes);

    // Overhead is OVERHEAD_BYTES bytes at the end of the packet
    // msg_bytes: Number of bytes in the message, excluding the overhead
    void Encrypt(u8 *buffer, int msg_bytes);
};



} // namespace cat

#endif // CAT_AUTHENTICATED_ENCRYPTION_HPP
