#include <cat/math/BigInt.hpp>

namespace cat
{
	// Generate a strong pseudo-prime using the Rabin-Miller primality test
	void GenerateStrongPseudoPrime(
		IRandom *prng,
		u32 *n,			// Output prime
		int limbs)		// Number of limbs in n
	{
		do {
			prng->Generate(n, limbs*4);
			n[limbs-1] |= 0x80000000;
			n[0] |= 1;
		} while (!RabinMillerPrimeTest(prng, n, limbs, 40)); // 40 iterations
	}
}
