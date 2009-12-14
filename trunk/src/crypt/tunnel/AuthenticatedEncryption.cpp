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

#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>
#include <cat/port/EndianNeutral.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/tunnel/KeyAgreement.hpp>
#include <cat/math/BitMath.hpp>
using namespace cat;


bool AuthenticatedEncryption::SetKey(int KeyBytes, Skein *key, bool is_initiator, const char *key_name)
{
	_accept_out_of_order = true;
    _is_initiator = is_initiator;

	if (!key_hash.SetKey(key)) return false;
	if (!key_hash.BeginKDF()) return false;
	key_hash.CrunchString(key_name);
    key_hash.End();

    Skein kdf;

    if (!kdf.SetKey(&key_hash)) return false;
    if (!kdf.BeginKDF()) return false;
    kdf.CrunchString(is_initiator ? "upstream-MAC" : "downstream-MAC");
    kdf.End();
    if (!local_mac.SetKey(&kdf)) return false;

    if (!kdf.SetKey(&key_hash)) return false;
    if (!kdf.BeginKDF()) return false;
    kdf.CrunchString(is_initiator ? "downstream-MAC" : "upstream-MAC");
    kdf.End();
    if (!remote_mac.SetKey(&kdf)) return false;

    u8 local_key[KeyAgreementCommon::MAX_BYTES];
    if (!kdf.SetKey(&key_hash)) return false;
    if (!kdf.BeginKDF()) return false;
    kdf.CrunchString(is_initiator ? "upstream-ENC" : "downstream-ENC");
    kdf.End();
    kdf.Generate(local_key, KeyBytes);
    local_cipher.Key(local_key, KeyBytes);
    local_iv = 1;

    u8 remote_key[KeyAgreementCommon::MAX_BYTES];
    if (!kdf.SetKey(&key_hash)) return false;
    if (!kdf.BeginKDF()) return false;
    kdf.CrunchString(is_initiator ? "downstream-ENC" : "upstream-ENC");
    kdf.End();
    kdf.Generate(remote_key, KeyBytes);
    remote_cipher.Key(remote_key, KeyBytes);
    remote_iv = 0;

    CAT_OBJCLR(iv_bitmap);

	return true;
}

bool AuthenticatedEncryption::GenerateProof(u8 *local_proof, int proof_bytes)
{
    Skein mac;

    if (!mac.SetKey(&key_hash) || !mac.BeginMAC()) return false;
    mac.CrunchString(_is_initiator ? "initiator proof" : "responder proof");
    mac.End();

    mac.Generate(local_proof, proof_bytes);

    return true;
}

bool AuthenticatedEncryption::ValidateProof(const u8 *remote_proof, int proof_bytes)
{
    if (proof_bytes > KeyAgreementCommon::MAX_BYTES) return false;

    Skein mac;

    if (!mac.SetKey(&key_hash) || !mac.BeginMAC()) return false;
    mac.CrunchString(_is_initiator ? "responder proof" : "initiator proof");
    mac.End();

    u8 expected[KeyAgreementCommon::MAX_BYTES];
    mac.Generate(expected, proof_bytes);

    return SecureEqual(expected, remote_proof, proof_bytes);
}





bool AuthenticatedEncryption::IsValidIV(u64 iv)
{
    // Check how far in the past this IV is
    int delta = (int)(remote_iv - iv);

    // If it is in the past,
    if (delta >= 0)
    {
		// Check if we do not accept out of order messages
		if (!_accept_out_of_order) return false;

        // Check if we have kept a record for this IV
        if (delta >= BITMAP_BITS) return false;

        u64 *map = &iv_bitmap[delta >> 6];
        u64 mask = (u64)1 << (delta & 63);

        // If it was seen, abort
        if (*map & mask) return false;
    }

    return true;
}

void AuthenticatedEncryption::AcceptIV(u64 iv)
{
    // Check how far in the past/future this IV is
    int delta = (int)(iv - remote_iv);

    // If it is in the future,
    if (delta > 0)
    {
        // If it would shift out everything we have seen,
        if (delta >= BITMAP_BITS)
        {
            // Set low bit to 1 and all other bits to 0
            iv_bitmap[0] = 1;
            memset(&iv_bitmap[1], 0, sizeof(iv_bitmap) - sizeof(u64));
        }
        else
        {
            int word_shift = delta >> 6;
            int bit_shift = delta & 63;

            // Shift replay window
            u64 last = iv_bitmap[BITMAP_WORDS - 1 - word_shift];
            for (int ii = BITMAP_WORDS - 1; ii >= word_shift + 1; --ii)
            {
                u64 x = iv_bitmap[ii - word_shift - 1];
                iv_bitmap[ii] = (last << bit_shift) | (x >> (64-bit_shift));
                last = x;
            }
            iv_bitmap[word_shift] = last << bit_shift;

            // Zero the words we skipped
            for (int ii = 0; ii < word_shift; ++ii)
                iv_bitmap[ii] = 0;

            // Set low bit for this IV
            iv_bitmap[0] |= 1;
        }

        // Only update the IV if the MAC was valid and the new IV is in the future
        remote_iv = iv;
    }
    else // Process an out-of-order packet
    {
        delta = -delta;

        // Set the bit in the bitmap for this IV
        iv_bitmap[delta >> 6] |= (u64)1 << (delta & 63);
    }
}




// Decrypt a packet from the remote host
bool AuthenticatedEncryption::Decrypt(u8 *buffer, int &buf_bytes)
{
    if (buf_bytes < OVERHEAD_BYTES) return false;

	int msg_bytes = buf_bytes - OVERHEAD_BYTES;

    u8 *overhead = buffer + msg_bytes;
    // overhead: encrypted { ... MAC(8 bytes) } || truncated IV(3 bytes)

    // De-obfuscate the truncated IV
    u32 trunc_iv = IV_MASK & (getLE(*(u32*)(overhead + MAC_BYTES) ^ *(u32*)overhead) ^ IV_FUZZ);

    // Reconstruct the original, full IV
    u64 iv = ReconstructCounter<IV_BITS>(remote_iv, trunc_iv);

    if (!IsValidIV(iv)) return false;

    // Decrypt the message and the MAC
    remote_cipher.Begin(iv);
    remote_cipher.Crypt(buffer, buffer, buf_bytes - IV_BYTES);

    // Generate the expected MAC given the decrypted message and full IV
    remote_mac.BeginMAC();
    remote_mac.Crunch(&iv, sizeof(iv));
    remote_mac.Crunch(buffer, msg_bytes);
    remote_mac.End();

    u8 expected[MAC_BYTES];
    remote_mac.Generate(expected, MAC_BYTES);

    // Validate the MAC
    if (!SecureEqual(expected, overhead, MAC_BYTES))
        return false;

    AcceptIV(iv);

	// Return the number of message bytes in buf_bytes
	buf_bytes = msg_bytes;
    return true;
}

// Encrypt a packet to send to the remote host
bool AuthenticatedEncryption::Encrypt(u8 *buffer, int buffer_bytes, int &msg_bytes)
{
	int out_bytes = msg_bytes + OVERHEAD_BYTES;
	if (out_bytes > buffer_bytes) return false;

    u8 *overhead = buffer + msg_bytes;

    // Generate a MAC for the message and full IV
    local_mac.BeginMAC();
    local_mac.Crunch(&local_iv, sizeof(local_iv));
    local_mac.Crunch(buffer, msg_bytes);
    local_mac.End();
    local_mac.Generate(overhead, MAC_BYTES);

    // Encrypt the message and MAC
    local_cipher.Begin(local_iv);
    local_cipher.Crypt(buffer, buffer, msg_bytes + MAC_BYTES);

    // Obfuscate the truncated IV
    u32 trunc_iv = IV_MASK & (getLE((u32)local_iv ^ *(u32*)overhead) ^ IV_FUZZ);

    overhead[MAC_BYTES] = (u8)trunc_iv;
    overhead[MAC_BYTES+1] = (u8)(trunc_iv >> 8);
    overhead[MAC_BYTES+2] = (u8)(trunc_iv >> 16);

    ++local_iv;

	// Return the number of ciphertext bytes in msg_bytes
	msg_bytes = out_bytes;
	return true;
}
