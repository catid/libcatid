#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Rabin-Miller method for finding a strong pseudo-prime
	// Preconditions: High bit and low bit of n = 1
	bool RabinMillerPrimeTest(
		IRandom *prng,
		const u32 *n,	// Number to check for primality
		int limbs,		// Number of limbs in n
		u32 k)			// Confidence level (40 is pretty good)
	{
		// n1 = n - 1
		u32 *n1 = (u32 *)alloca(limbs*4);
		Set(n1, limbs, n);
		Subtract32(n1, limbs, 1);

		// d = n1
		u32 *d = (u32 *)alloca(limbs*4);
		Set(d, limbs, n1);

		// remove factors of two from d
		while (!(d[0] & 1))
			ShiftRight(limbs, d, d, 1);

		u32 *a = (u32 *)alloca(limbs*4);
		u32 *t = (u32 *)alloca(limbs*4);
		u32 *p = (u32 *)alloca((limbs*2)*4);
		u32 n_inv = MonReducePrecomp(n[0]);

		// iterate k times
		while (k--)
		{
			do prng->Generate(a, limbs*4);
			while (GreaterOrEqual(a, limbs, n, limbs));

			// a = a ^ d (Mod n)
			ExpMod(a, limbs, d, limbs, n, limbs, n_inv, a);

			Set(t, limbs, d);
			while (!Equal(limbs, t, n1) &&
				   !Equal32(a, limbs, 1) &&
				   !Equal(limbs, a, n1))
			{
				// TODO: verify this is actually working

				// a = a^2 (Mod n), non-critical path
				Square(limbs, p, a);
				Modulus(p, limbs*2, n, limbs, a);

				// t <<= 1
				ShiftLeft(limbs, t, t, 1);
			}

			if (!Equal(limbs, a, n1) && !(t[0] & 1)) return false;
		}

		return true;
	}
}
