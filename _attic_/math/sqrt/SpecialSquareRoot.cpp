#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Take the square root in a finite field modulus a prime m.
	// Special conditions: m = 2^N - c = 3 (mod 4) -> c = 1 (mod 4)
	void SpecialSquareRoot(
		int limbs,
		const u32 *x,
		u32 c,
		u32 *r)
	{
		// Compute x ^ (m + 1)/4 = x ^ ((2^N-c) >> 2)
		u32 *p = (u32*)alloca(limbs*2*4);
		u32 *m = (u32*)alloca(limbs*4);
		u32 *x_squared = (u32*)alloca(limbs*4);

		// m = modulus >> 2
		Set32(m, limbs, 0);
		Subtract32(m, limbs, c);
		Add32(m, limbs, 1);
		ShiftRight(limbs, m, m, 2);

		bool seen = false;

		for (int limb = limbs - 1; limb >= 0; --limb)
		{
			for (u32 bit = 1 << 31; bit; bit >>= 1)
			{
				if (!seen)
				{
					if (m[limb] & bit)
					{
						Set(x_squared, limbs, x);
						seen = true;
					}
				}
				else
				{
					Square(limbs, p, x_squared);
					SpecialModulus(p, limbs*2, c, limbs, x_squared);

					if (m[limb] & bit)
					{
						Multiply(limbs, p, x_squared, x);
						SpecialModulus(p, limbs*2, c, limbs, x_squared);
					}
				}
			}
		}

		Set(r, limbs, x_squared);
	}
}
