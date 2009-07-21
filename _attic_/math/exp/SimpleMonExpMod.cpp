#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
	// Simple internal version without windowing for small exponents
	void SimpleMonExpMod(
		const u32 *base,	//	Base for exponentiation, buffer size = mod_limbs
		const u32 *exponent,//	Exponent, buffer size = exponent_limbs
		int exponent_limbs,	//	Number of limbs in exponent
		const u32 *modulus,	//	Modulus, buffer size = mod_limbs
		int mod_limbs,		//	Number of limbs in modulus
		u32 mod_inv,		//	MonReducePrecomp() return
		u32 *result)		//	Result, buffer size = mod_limbs
	{
		bool set = false;

		u32 *temp = (u32*)alloca((mod_limbs*2)*4);

		// Run down exponent bits and use the squaring method
		for (int ii = exponent_limbs - 1; ii >= 0; --ii)
		{
			u32 e_i = exponent[ii];

			for (u32 mask = 0x80000000; mask; mask >>= 1)
			{
				if (set)
				{
					// result = result^2
					Square(mod_limbs, temp, result);
					MonReduce(mod_limbs, temp, modulus, mod_inv, result);

					if (e_i & mask)
					{
						// result *= base
						Multiply(mod_limbs, temp, result, base);
						MonReduce(mod_limbs, temp, modulus, mod_inv, result);
					}
				}
				else
				{
					if (e_i & mask)
					{
						// result = base
						Set(result, mod_limbs, base, mod_limbs);
						set = true;
					}
				}
			}
		}
	}
}
