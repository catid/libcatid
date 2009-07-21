#include <cat/math/BigInt.hpp>

namespace cat
{
	// lhs += rhs, return carry out
	// precondition: lhs_limbs > 0
	u32 Add32(u32 *lhs, int lhs_limbs, u32 rhs)
	{
		if ((lhs[0] = lhs[0] + rhs) >= rhs)
			return 0;

		for (int ii = 1; ii < lhs_limbs; ++ii)
			if (++lhs[ii])
				return 0;

		return 1;
	}
}
