#include <cat/crypt/hash/Sha256.hpp>
#include <cat/math/BigInt.hpp>
#include <cstring>
using namespace cat;

// Algorithm from FIPS180-2

// 4.1.2	SHA-256 Functions

#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define Sigma0(x) (CAT_ROR32(x,  2) ^ CAT_ROR32(x, 13) ^ CAT_ROR32(x, 22))
#define Sigma1(x) (CAT_ROR32(x,  6) ^ CAT_ROR32(x, 11) ^ CAT_ROR32(x, 25))
#define Gamma0(x) (CAT_ROR32(x,  7) ^ CAT_ROR32(x, 18) ^ ((x) >>  3))
#define Gamma1(x) (CAT_ROR32(x, 17) ^ CAT_ROR32(x, 19) ^ ((x) >> 10))

// 4.2.2	SHA-256 Constants
// "These words represent the first thirty-two bits of the
// fractional parts of the cube roots of the first 24 primes."

static const u32 SBOX[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// 6.2.2	SHA-256 Hash Computation

#define STEP3(t, w) \
{ \
	u32 T1, T2; \
	T1 = h + Sigma1(e) + Ch(e, f, g) + SBOX[t] + (w); \
	T2 = Sigma0(a) + Maj(a, b, c); \
	h = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2; \
}

void SHA256::HashComputation()
{
	register u32 a, b, c, d, e, f, g, h;
	u32 ii;

	a = H[0];
	b = H[1];
	c = H[2];
	d = H[3];
	e = H[4];
	f = H[5];
	g = H[6];
	h = H[7];

	for (ii = 0; ii < 16; ++ii)
	{
		u32 Wt = swapBE(work[ii]);

		STEP3(ii, Wt);
	}

	for (; ii < 64; ++ii)
	{
		u32 Wt = Gamma1(work[ii - 2]) + work[ii - 7] + Gamma0(work[ii - 15]) + work[ii - 16];

		work[ii] = Wt;

		STEP3(ii, Wt);
	}

	H[0] += a;
	H[1] += b;
	H[2] += c;
	H[3] += d;
	H[4] += e;
	H[5] += f;
	H[6] += g;
	H[7] += h;
}


//// SHA256

SHA256::~SHA256()
{
	CAT_OBJCLR(State);
	CAT_OBJCLR(Tweak);
	CAT_OBJCLR(Work);
}

// 5.3.2	SHA-256 Initial hash value
// "These words were obtained by taking the first 32 bits
// of the factional parts of the square roots of the first
// 8 prime numbers."

static const u32 State0_256[8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

bool SHA256::Begin(int bits)
{
	memcpy(State, State0_256, sizeof(State));
	CAT_OBJCLR(Work);
	CAT_OBJCLR(Tweak);
	free_bytes = BYTES;
	digest_bytes = bits / 8;
	return true;
}

bool SHA256::BeginMAC(const ICryptHash *key_hash)
{
	const SHA256 *parent = dynamic_cast<const SHA256 *>(key_hash);
	if (!parent) return false;

	memcpy(State, parent->State, sizeof(State));
	CAT_OBJCLR(Tweak);
	free_bytes = BYTES;
	digest_bytes = parent->digest_bytes;
	return true;
}

bool SHA256::BeginKDF(const ICryptHash *key_hash)
{
	const SHA256 *parent = dynamic_cast<const SHA256 *>(key_hash);
	if (!parent) return false;

	memcpy(State, parent->State, sizeof(State));
	CAT_OBJCLR(Tweak);
	free_bytes = BYTES;
	digest_bytes = parent->digest_bytes;
	return true;
}

void SHA256::Crunch(const void *_buffer, u32 bytes)
{
	const u8 *buffer = (const u8*)_buffer;
	u8 *work8 = (u8*)Work;

	// Update the byte counter
	if ((Tweak[0] += bytes) < bytes)
		++Tweak[1];

	// If there is still room in the workspace for this data,
	if (bytes < free_bytes)
	{
		// Copy the new data in
		memcpy(work8 + MESSAGE_BYTES - free_bytes, buffer, bytes);
		free_bytes -= bytes;
	}
	else // Otherwise, we will fill up the workspace
	{
		// Fill up the workspace
		memcpy(work8 + MESSAGE_BYTES - free_bytes, buffer, free_bytes);

		HashComputation();

		// Remove this data from the buffer
		buffer += free_bytes;
		bytes -= free_bytes;

		while (bytes >= MESSAGE_BYTES)
		{
			// Copy over the work buffer
			memcpy(work, buffer, MESSAGE_BYTES);

			HashComputation();

			// Remove this data from the buffer
			buffer += MESSAGE_BYTES;
			bytes -= MESSAGE_BYTES;
		}

		// Copy whatever is left over
		memcpy(work, buffer, bytes);
		free_bytes = MESSAGE_BYTES - bytes;
	}
}

void SHA256::End()
{
	// NOTE: We should always have at least one message hash to perform here

	// Pad with zeroes
	memset(Work + used_bytes, 0, BYTES - used_bytes);

	// Final message hash
	Tweak[1] |= T1_MASK_FINAL;
	HashComputation();
}

void SHA256::Generate(void *out, int bytes)
{
	// Put the Skein generator in counter mode and generate WORDS at a time
	u64 FinalMessage[WORDS] = {0};
	u64 *out64 = (u64 *)out;

	while (bytes >= BYTES)
	{
		// T1 = FIRST | FINAL | OUT
		Tweak[0] = 0;
		Tweak[1] = T1_MASK_FIRST | T1_MASK_FINAL | ((u64)BLK_TYPE_OUT << T1_POS_BLK_TYPE);

		// Produce next output
		HashComputation();

#if defined(BIG_ENDIAN)
		for (int ii = 0; ii < WORDS; ++ii)
			swapLE(out64[ii]);
#endif

		// Next counter
		out64 += WORDS;
		bytes -= BYTES;
		FinalMessage[0]++;
	}

	if (bytes > 0)
	{
		// T1 = FIRST | FINAL | OUT
		Tweak[0] = 0;
		Tweak[1] = T1_MASK_FIRST | T1_MASK_FINAL | ((u64)BLK_TYPE_OUT << T1_POS_BLK_TYPE);

		// Produce final output
		HashComputation();

#if defined(BIG_ENDIAN)
		for (int ii = CAT_CEIL_UNIT(bytes, sizeof(u64)); ii >= 0; --ii)
			swapLE(FinalMessage[ii]);
#endif

		// Copy however many bytes they wanted
		memcpy(out64, FinalMessage, bytes);
	}
}














void SHA256::Crunch(const void *_buffer, u32 bytes)
{
	const u8 *buffer = (const u8*)_buffer;
	u8 *work8 = (u8*)work;

	// Update the byte counter
	if ((lowCounter += bytes) < bytes)
		++highCounter;

	// If there is still room in the workspace for this data,
	if (bytes < free_bytes)
	{
		// Copy the new data in
		memcpy(work8 + MESSAGE_BYTES - free_bytes, buffer, bytes);
		free_bytes -= bytes;
	}
	else // Otherwise, we will fill up the workspace
	{
		// Fill up the workspace
		memcpy(work8 + MESSAGE_BYTES - free_bytes, buffer, free_bytes);

		HashComputation();

		// Remove this data from the buffer
		buffer += free_bytes;
		bytes -= free_bytes;

		while (bytes >= MESSAGE_BYTES)
		{
			// Copy over the work buffer
			memcpy(work, buffer, MESSAGE_BYTES);

			HashComputation();

			// Remove this data from the buffer
			buffer += MESSAGE_BYTES;
			bytes -= MESSAGE_BYTES;
		}

		// Copy whatever is left over
		memcpy(work, buffer, bytes);
		free_bytes = MESSAGE_BYTES - bytes;
	}
}

const u8 *SHA256::Finish()
{
	u8 *work8 = (u8*)work;
	int last_free = MESSAGE_BYTES - free_bytes;

	// We will always have at least some free bytes
	work8[last_free] = 0x80;

	++last_free;
	--free_bytes;

	memset(work8 + last_free, 0, free_bytes);

	// If there is not enough space for the counters, do hash computation
	if (free_bytes < COUNTER_BYTES)
	{
		HashComputation();

		// And start over with an empty workspace
		memset(work, 0, MESSAGE_BYTES);
	}

	// Convert the counters into bit-counts
	highCounter <<= 3;
	highCounter |= lowCounter >> (32 - 3);
	lowCounter <<= 3;

	// Copy them to the high words of message buffer, big-endian
	work[15] = getBE(lowCounter);
	work[14] = getBE(highCounter);

	// Perform the final has computation with the counters
	HashComputation();

	for (int ii = 0; ii < 8; ++ii)
		swapBE(H[ii]);

	return (u8*)H;
}
