#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Precompute a window for ExpMod() and MonExpMod()
	// Requires 2^window_bits multiplies
	u32 *ExpPrecomputeWindow(
		const u32 *base,
		const u32 *modulus,
		int limbs,
		u32 mod_inv,
		int window_bits)
	{
		u32 *temp = (u32*)alloca(limbs*2*4);

		u32 *base_squared = (u32*)alloca(limbs*4);
		Square(limbs, temp, base);
		MonReduce(limbs, temp, modulus, mod_inv, base_squared);

		// precomputed window starts with 000001, 000011, 000101, 000111, ...
		u32 k = (1 << (window_bits - 1));

		u32 *window = new u32[limbs * k];

		u32 *cw = window;
		Set(window, limbs, base);

		while (--k)
		{
			// cw+1 = cw * base^2
			Multiply(limbs, temp, cw, base_squared);
			MonReduce(limbs, temp, modulus, mod_inv, cw + limbs);
			cw += limbs;
		}

		return window;
	}
}
