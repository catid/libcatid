#include <cat/math/BigInt.hpp>

namespace cat
{
	// lhs -= rhs, return borrow out
	// precondition: lhs_limbs > 0, result limbs = lhs_limbs
	s32 Subtract32(u32 *lhs, int lhs_limbs, u32 rhs)
	{
		u32 n = lhs[0];
		u32 r = n - rhs;
		lhs[0] = r;

		if (r <= n)
			return 0;

		for (int ii = 1; ii < lhs_limbs; ++ii)
			if (lhs[ii]--)
				return 0;

		return -1;
	}
}
