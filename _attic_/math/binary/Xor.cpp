#include <cat/math/BigInt.hpp>

namespace cat
{
	// lhs ^= rhs
	void Xor(int limbs, u32 *lhs, const u32 *rhs)
	{
		while (limbs--) *lhs++ ^= *rhs++;
	}
}
