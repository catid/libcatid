#include <cat/math/BigInt.hpp>

namespace cat
{
	// lhs += rhs, return carry out
	// precondition: lhs_limbs >= rhs_limbs
	u32 Add(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		u64 r = (u64)lhs[0] + rhs[0];
		lhs[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((u64)lhs[ii] + rhs[ii]) + (u32)(r >> 32);
			lhs[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (u32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			lhs[ii] = (u32)r;
		}

		return (u32)(r >> 32);
	}

	// out = lhs + rhs, return carry out
	// precondition: lhs_limbs >= rhs_limbs
	u32 Add(u32 *out, const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		u64 r = (u64)lhs[0] + rhs[0];
		out[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((u64)lhs[ii] + rhs[ii]) + (u32)(r >> 32);
			out[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (u32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			out[ii] = (u32)r;
		}

		return (u32)(r >> 32);
	}
}
