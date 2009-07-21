#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
	// product = x ^ 2
	void SimpleSquare(
		int limbs,		// Number of limbs in parameter x
		u32 *product,	// Large number; buffer size = limbs*2
		const u32 *x)	// Large number
	{
		// Seems about 15% faster than SimpleMultiply() in practice
		u32 *cross_product = (u32*)alloca(limbs*2*4);

		// Calculate square-less and repeat-less cross products
		cross_product[limbs] = Multiply32(limbs - 1, cross_product + 1, x + 1, x[0]);
		for (int ii = 1; ii < limbs - 1; ++ii)
		{
			cross_product[limbs + ii] = AddMultiply32(limbs - ii - 1,
													  cross_product + ii*2 + 1,
													  x + ii + 1,
													  x[ii]);
		}

		// Calculate square products
		for (int ii = 0; ii < limbs; ++ii)
		{
			u32 xi = x[ii];
			u64 si = (u64)xi * xi;
			product[ii*2] = (u32)si;
			product[ii*2+1] = (u32)(si >> 32);
		}

		// Multiply the cross product by 2 and add it to the square products
		product[limbs*2 - 1] += AddLeftShift32(limbs*2 - 2, product + 1, cross_product + 1, 1);
	}
}
