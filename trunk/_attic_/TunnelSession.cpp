#include <cat/crypt/tunnel/TunnelSession.hpp>
#include <cat/port/EndianNeutral.hpp>
#include <cat/crypt/SecureCompare.hpp>
using namespace cat;


//// TunnelSession

void TunnelSession::SetKey(int KeyBytes, const u8 *shared_secret, const u8 *client_seed, const u8 *server_seed, bool is_client)
{
	Skein keyed_hash, kdf;

	keyed_hash.BeginKey(KeyBytes * 8);
	keyed_hash.Crunch(shared_secret, KeyBytes);
	keyed_hash.Crunch(client_seed, KeyBytes);
	keyed_hash.Crunch(server_seed, KeyBytes);
	keyed_hash.End();

	kdf.SetKey(&keyed_hash);
	kdf.BeginKDF();
	kdf.CrunchString(is_client ? "upstream-MAC" : "downstream-MAC");
	kdf.End();
	local_mac.SetKey(&kdf);

	kdf.SetKey(&keyed_hash);
	kdf.BeginKDF();
	kdf.CrunchString(is_client ? "downstream-MAC" : "upstream-MAC");
	kdf.End();
	remote_mac.SetKey(&kdf);

	u8 local_key[TwistedEdwardCommon::MAX_BYTES];
	kdf.SetKey(&keyed_hash);
	kdf.BeginKDF();
	kdf.CrunchString(is_client ? "upstream-ENC" : "downstream-ENC");
	kdf.End();
	kdf.Generate(local_key, KeyBytes);
	local_cipher.Key(local_key, KeyBytes);
	local_iv = 1;

	u8 remote_key[TwistedEdwardCommon::MAX_BYTES];
	kdf.SetKey(&keyed_hash);
	kdf.BeginKDF();
	kdf.CrunchString(is_client ? "downstream-ENC" : "upstream-ENC");
	kdf.End();
	kdf.Generate(remote_key, KeyBytes);
	remote_cipher.Key(remote_key, KeyBytes);
	remote_iv = 0;

	CAT_OBJCLR(iv_bitmap);
}






bool TunnelSession::IsValidIV(u64 iv)
{
	// Check how far in the past this IV is
	int delta = (int)(remote_iv - iv);

	// If it is in the past,
	if (delta >= 0)
	{
		// Check if we have kept a record for this IV
		if (delta >= BITMAP_BITS) return false;

		u64 *map = &iv_bitmap[delta >> 6];
		u64 mask = (u64)1 << (delta & 63);

		// If it was seen, abort
		if (*map & mask) return false;
	}

	return true;
}

void TunnelSession::AcceptIV(u64 iv)
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






// Reconstruct a whole IV given the last accepted IV
// Assumes the IV increments by 1 each time
u64 TunnelSession::ReconstructIV(u64 last_accepted_iv, u32 new_iv_low_bits)
{
	s32 diff = new_iv_low_bits - (u32)(last_accepted_iv & IV_MASK);

	return ((last_accepted_iv & ~(u64)IV_MASK) | new_iv_low_bits)
		 - (((IV_MSB >> 1) - (diff & IV_MASK)) & IV_MSB)
		 + (diff & IV_MSB);
}

// Decrypt a packet from the remote host
bool TunnelSession::Decrypt(u8 *buffer, int buf_bytes)
{
	if (buf_bytes < OVERHEAD_BYTES) return false;

	u8 *overhead = buffer + buf_bytes - OVERHEAD_BYTES;
	// overhead: encrypted { ... MAC(8 bytes) } || truncated IV(3 bytes)

	// De-obfuscate the truncated IV
	u32 trunc_iv = IV_MASK & (getLE(*(u32*)(overhead + MAC_BYTES) ^ *(u32*)overhead) ^ IV_FUZZ);

	// Recover the original IV
	u64 iv = ReconstructIV(remote_iv, trunc_iv);

	if (!IsValidIV(iv)) return false;

	// Decrypt the message and the MAC
	remote_cipher.Begin(iv);
	remote_cipher.Crypt(buffer, buffer, buf_bytes - IV_BYTES);

	// Generate the expected MAC given the decrypted message and full IV
	remote_mac.BeginMAC();
	remote_mac.Crunch(&iv, sizeof(iv));
	remote_mac.Crunch(buffer, buf_bytes - OVERHEAD_BYTES);
	remote_mac.End();

	u8 expected[MAC_BYTES];
	remote_mac.Generate(expected, sizeof(expected));

	// Validate the MAC
	if (!SecureEqual(expected, overhead, sizeof(expected)))
		return false;

	AcceptIV(iv);

	return true;
}

// Encrypt a packet to send to the remote host
void TunnelSession::Encrypt(u8 *buffer, int msg_bytes)
{
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
}
