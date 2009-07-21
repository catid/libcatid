#include <cat/math/BigInt.hpp>

namespace cat
{
	// n = ~n, only invert bits up to the MSB, but none above that
	void BitNot(u32 *n, int limbs)
	{
		limbs = LimbDegree(n, limbs);
		if (limbs)
		{
			u32 high = n[--limbs];
			u32 high_degree = 32 - Degree32(high);

			n[limbs] = ((u32)(~high << high_degree) >> high_degree);
			while (limbs--) n[limbs] = ~n[limbs];
		}
	}

	// n = ~n, invert all bits, even ones above MSB
	void LimbNot(u32 *n, int limbs)
	{
		while (limbs--) *n++ = ~(*n);
	}
}
