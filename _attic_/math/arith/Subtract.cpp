#include <cat/math/BigInt.hpp>

namespace cat
{
	// lhs -= rhs, return borrow out
	// precondition: lhs_limbs >= rhs_limbs
	s32 Subtract(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		s64 r = (s64)lhs[0] - rhs[0];
		lhs[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((s64)lhs[ii] - rhs[ii]) + (s32)(r >> 32);
			lhs[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (s32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			lhs[ii] = (u32)r;
		}

		return (s32)(r >> 32);
	}

	// out = lhs - rhs, return borrow out
	// precondition: lhs_limbs >= rhs_limbs
	s32 Subtract(u32 *out, const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		s64 r = (s64)lhs[0] - rhs[0];
		out[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((s64)lhs[ii] - rhs[ii]) + (s32)(r >> 32);
			out[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (s32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			out[ii] = (u32)r;
		}

		return (s32)(r >> 32);
	}
}
