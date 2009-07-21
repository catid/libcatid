#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>

namespace cat
{
	// returns the degree of the base 2 monic polynomial
	// (the number of bits used to represent the number)
	// eg, 0 0 0 0 1 0 1 1 ... => 28 out of 32 used
	u32 Degree32(u32 v)
	{
		return v ? (BitMath::BSR32(v) + 1) : 0;
	}

	// returns the number of limbs that are actually used
	int LimbDegree(const u32 *n, int limbs)
	{
		while (limbs--)
			if (n[limbs])
				return limbs + 1;

		return 0;
	}

	// return bits used
	u32 Degree(const u32 *n, int limbs)
	{
		u32 limb_degree = LimbDegree(n, limbs);
		if (!limb_degree) return 0;
		--limb_degree;

		u32 msl_degree = Degree32(n[limb_degree]);

		return msl_degree + limb_degree*32;
	}
}
