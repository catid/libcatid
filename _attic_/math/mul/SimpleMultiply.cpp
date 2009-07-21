#include <cat/math/BigInt.hpp>

namespace cat
{
	// product = x * y
	void SimpleMultiply(
		int limbs,		// Number of limbs in parameters x, y
		u32 *product,	// Large number; buffer size = limbs*2
		const u32 *x,	// Large number
		const u32 *y)	// Large number
	{
		// Roughly 25% of the cost of exponentiation
		product[limbs] = Multiply32(limbs, product, x, *y);
		u32 ctr = limbs;
		while (--ctr) product[limbs] = AddMultiply32(limbs, ++product, x, *++y);
	}
}
