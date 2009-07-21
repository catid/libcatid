#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
	void Set(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int min = lhs_limbs < rhs_limbs ? lhs_limbs : rhs_limbs;

		memcpy(lhs, rhs, min*4);
		memset(&lhs[min], 0, (lhs_limbs - min)*4);
	}

	void Set(u32 *lhs, int limbs, const u32 *rhs)
	{
		memcpy(lhs, rhs, limbs*4);
	}

	void Set32(u32 *lhs, int lhs_limbs, const u32 rhs)
	{
		*lhs = rhs;
		memset(&lhs[1], 0, (lhs_limbs - 1)*4);
	}
}
