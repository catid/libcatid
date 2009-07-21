#include <cat/crypt/hash/Sha512.hpp>
#include <cat/math/BigInt.hpp>
#include <cstring>
using namespace cat;

// Algorithm from FIPS180-2

// 4.1.3	SHA-512 Functions

#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define Sigma0(x) (CAT_ROR64(x, 28) ^ CAT_ROR64(x, 34) ^ CAT_ROR64(x, 39))
#define Sigma1(x) (CAT_ROR64(x, 14) ^ CAT_ROR64(x, 18) ^ CAT_ROR64(x, 41))
#define Gamma0(x) (CAT_ROR64(x,  1) ^ CAT_ROR64(x,  8) ^ ((x) >> 7))
#define Gamma1(x) (CAT_ROR64(x, 19) ^ CAT_ROR64(x, 61) ^ ((x) >> 6))

// 4.2.3	SHA-512 Constants
// "These words represent the first sixty-four bits of the fractional parts of 
// the cube roots of the first eighty prime numbers"

static const u64 SBOX[80] = {
	0x428a2f98d728ae22LL, 0x7137449123ef65cdLL, 0xb5c0fbcfec4d3b2fLL, 0xe9b5dba58189dbbcLL,
	0x3956c25bf348b538LL, 0x59f111f1b605d019LL, 0x923f82a4af194f9bLL, 0xab1c5ed5da6d8118LL,
	0xd807aa98a3030242LL, 0x12835b0145706fbeLL, 0x243185be4ee4b28cLL, 0x550c7dc3d5ffb4e2LL,
	0x72be5d74f27b896fLL, 0x80deb1fe3b1696b1LL, 0x9bdc06a725c71235LL, 0xc19bf174cf692694LL,
	0xe49b69c19ef14ad2LL, 0xefbe4786384f25e3LL, 0x0fc19dc68b8cd5b5LL, 0x240ca1cc77ac9c65LL,
	0x2de92c6f592b0275LL, 0x4a7484aa6ea6e483LL, 0x5cb0a9dcbd41fbd4LL, 0x76f988da831153b5LL,
	0x983e5152ee66dfabLL, 0xa831c66d2db43210LL, 0xb00327c898fb213fLL, 0xbf597fc7beef0ee4LL,
	0xc6e00bf33da88fc2LL, 0xd5a79147930aa725LL, 0x06ca6351e003826fLL, 0x142929670a0e6e70LL,
	0x27b70a8546d22ffcLL, 0x2e1b21385c26c926LL, 0x4d2c6dfc5ac42aedLL, 0x53380d139d95b3dfLL,
	0x650a73548baf63deLL, 0x766a0abb3c77b2a8LL, 0x81c2c92e47edaee6LL, 0x92722c851482353bLL,
	0xa2bfe8a14cf10364LL, 0xa81a664bbc423001LL, 0xc24b8b70d0f89791LL, 0xc76c51a30654be30LL,
	0xd192e819d6ef5218LL, 0xd69906245565a910LL, 0xf40e35855771202aLL, 0x106aa07032bbd1b8LL,
	0x19a4c116b8d2d0c8LL, 0x1e376c085141ab53LL, 0x2748774cdf8eeb99LL, 0x34b0bcb5e19b48a8LL,
	0x391c0cb3c5c95a63LL, 0x4ed8aa4ae3418acbLL, 0x5b9cca4f7763e373LL, 0x682e6ff3d6b2b8a3LL,
	0x748f82ee5defb2fcLL, 0x78a5636f43172f60LL, 0x84c87814a1f0ab72LL, 0x8cc702081a6439ecLL,
	0x90befffa23631e28LL, 0xa4506cebde82bde9LL, 0xbef9a3f7b2c67915LL, 0xc67178f2e372532bLL,
	0xca273eceea26619cLL, 0xd186b8c721c0c207LL, 0xeada7dd6cde0eb1eLL, 0xf57d4f7fee6ed178LL,
	0x06f067aa72176fbaLL, 0x0a637dc5a2c898a6LL, 0x113f9804bef90daeLL, 0x1b710b35131c471bLL,
	0x28db77f523047d84LL, 0x32caab7b40c72493LL, 0x3c9ebe0a15c9bebcLL, 0x431d67c49c100d4cLL,
	0x4cc5d4becb3e42b6LL, 0x597f299cfc657e2aLL, 0x5fcb6fab3ad6faecLL, 0x6c44198c4a475817LL
};

// 5.3.3	SHA-512 Initial hash value

// "These words were obtained by taking the first sixty-four bits of the fractional parts
// of the square roots of the first eight prime numbers."
static const u64 H0_512[8] = {
	0x6a09e667f3bcc908LL, 0xbb67ae8584caa73bLL, 0x3c6ef372fe94f82bLL, 0xa54ff53a5f1d36f1LL,
	0x510e527fade682d1LL, 0x9b05688c2b3e6c1fLL, 0x1f83d9abfb41bd6bLL, 0x5be0cd19137e2179LL
};

// "These words were obtained by taking the first sixty-four bits of the fractional parts
// of the square roots of the ninth through sixteenth prime numbers."
static const u64 H0_384[8] = {
	0xcbbb9d5dc1059ed8LL, 0x629a292a367cd507LL, 0x9159015a3070dd17LL, 0x152fecd8f70e5939LL,
	0x67332667ffc00b31LL, 0x8eb44a8768581511LL, 0xdb0c2e0d64f98fa7LL, 0x47b5481dbefa4fa4LL
};

// 6.3.2	SHA-512 Hash Computation

#define STEP3(t, w) \
{ \
	u64 T1, T2; \
	T1 = h + Sigma1(e) + Ch(e, f, g) + SBOX[t] + w; \
	T2 = Sigma0(a) + Maj(a, b, c); \
	h = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2; \
}

void SHA512::HashComputation()
{
	register u64 a, b, c, d, e, f, g, h;
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
		u64 Wt = swapBE(work[ii]);

		STEP3(ii, Wt);
	}

	for (; ii < 80; ++ii)
	{
		u64 Wt = Gamma1(work[ii - 2]) + work[ii - 7] + Gamma0(work[ii - 15]) + work[ii - 16];

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


//// SHA512

SHA512::SHA512(int bits)
{
	Begin(bits);
}

SHA512::~SHA512()
{
	highCounter = lowCounter = 0;

	CAT_OBJCLR(H);
	CAT_OBJCLR(work);
}

bool SHA512::Begin(int bits)
{
	if (bits > 512) return false;

	highCounter = lowCounter = 0;

	CAT_OBJCLR(work);
	free_bytes = MESSAGE_BYTES;

	digest_bytes = bits / 8;

	if (bits > 384)
		memcpy(H, H0_512, sizeof(H));
	else
		memcpy(H, H0_384, sizeof(H));

	return true;
}

void SHA512::Crunch(const void *_buffer, u32 bytes)
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

const u8 *SHA512::Finish()
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
	highCounter |= lowCounter >> (64 - 3);
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
