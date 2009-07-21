#include <cat/math/BigInt.hpp>

namespace cat
{
	// result = a * r^-1 (Mod modulus) in Montgomery domain
	// The result may be greater than the modulus, but this is okay since
	// the result is still in the RNS.  MonFinish() corrects this at the end.
	void MonReduce(
		int limbs,			// Number of limbs in modulus
		u32 *s,				// Large number, buffer size = limbs*2, gets clobbered
		const u32 *modulus,	// Large number, buffer size = limbs
		u32 mod_inv,		// MonReducePrecomp() return
		u32 *result)		// Large number, buffer size = limbs
	{
		// This function is roughly 60% of the cost of exponentiation
		for (int ii = 0; ii < limbs; ++ii)
		{
			u32 q = s[0] * mod_inv;
			s[0] = AddMultiply32(limbs, s, modulus, q);
			++s;
		}

		// Add the saved carries
		if (Add(result, s, limbs, s - limbs, limbs))
		{
			// Reduce the result only when needed
			Subtract(result, limbs, modulus, limbs);
		}
	}
}
