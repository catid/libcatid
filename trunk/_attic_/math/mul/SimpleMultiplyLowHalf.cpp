#include <cat/math/BigInt.hpp>

namespace cat
{
	// product = low half of x * y product
	void SimpleMultiplyLowHalf(
		int limbs,		// Number of limbs in parameters x, y
		u32 *product,	// Large number; buffer size = limbs
		const u32 *x,	// Large number
		const u32 *y)	// Large number
	{
		Multiply32(limbs, product, x, *y);
		while (--limbs) AddMultiply32(limbs, ++product, x, *++y);
	}
}
