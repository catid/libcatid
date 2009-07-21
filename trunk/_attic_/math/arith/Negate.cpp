#include <cat/math/BigInt.hpp>

namespace cat
{
	// lhs = -rhs
	void Negate(int limbs, u32 *lhs, const u32 *rhs)
	{
		// Propagate negations until carries stop
		while (limbs-- > 0 && !(*lhs++ = -(s32)(*rhs++)));

		// Then just invert the remaining words
		while (limbs-- > 0) *lhs++ = ~(*rhs++);
	}
}
