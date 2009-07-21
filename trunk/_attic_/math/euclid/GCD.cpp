#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
	// Computes: result = GCD(a, b)  (greatest common divisor)
	// Length of result is the length of the smallest argument
	void GCD(
		const u32 *a,	//	Large number, buffer size = a_limbs
		int a_limbs,	//	Size of a
		const u32 *b,	//	Large number, buffer size = b_limbs
		int b_limbs,	//	Size of b
		u32 *result)	//	Large number, buffer size = min(a, b)
	{
		int limbs = (a_limbs <= b_limbs) ? a_limbs : b_limbs;

		u32 *g = (u32*)alloca(limbs*4);
		u32 *g1 = (u32*)alloca(limbs*4);

		if (a_limbs <= b_limbs)
		{
			// g = a, g1 = b (mod a)
			Set(g, limbs, a, a_limbs);
			Modulus(b, b_limbs, a, a_limbs, g1);
		}
		else
		{
			// g = b, g1 = a (mod b)
			Set(g, limbs, b, b_limbs);
			Modulus(a, a_limbs, b, b_limbs, g1);
		}

		for (;;) {
			// g = (g mod g1)
			Modulus(g, limbs, g1, limbs, g);

			if (!LimbDegree(g, limbs)) {
				Set(result, limbs, g1, limbs);
				return;
			}

			// g1 = (g1 mod g)
			Modulus(g1, limbs, g, limbs, g1);

			if (!LimbDegree(g1, limbs)) {
				Set(result, limbs, g, limbs);
				return;
			}
		}
	}
}
