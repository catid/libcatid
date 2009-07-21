#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// result = a * r^-1 (Mod modulus) in Montgomery domain
	void MonFinish(
		int limbs,			// Number of limbs in each parameter
		u32 *n,				// Large number, buffer size = limbs
		const u32 *modulus,	// Large number, buffer size = limbs
		u32 mod_inv)		// MonReducePrecomp() return
	{
		u32 *t = (u32*)alloca(limbs*2*4);
		memcpy(t, n, limbs*4);
		memset(t + limbs, 0, limbs*4);

		// Reduce the number
		MonReduce(limbs, t, modulus, mod_inv, n);

		// Fix MonReduce() results greater than the modulus
		if (!Less(limbs, n, modulus))
			Subtract(n, limbs, modulus, limbs);
	}
}
