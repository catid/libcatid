#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
	// product = x^2
	// memory space for product may not overlap with x
    void Square(
    	int limbs,		// Number of limbs in x
    	u32 *product,	// Product; buffer size = limbs*2
    	const u32 *x)	// Large number; buffer size = limbs
	{
		// Stop recursing under 1280 bits or odd limb count
		if (limbs < 40 || (limbs & 1))
		{
			SimpleSquare(limbs, product, x);
			return;
		}

		// Compute high and low squares
		Square(limbs/2, product, x);
		Square(limbs/2, product + limbs, x + limbs/2);

		// Generate the cross product
		u32 *cross_product = (u32*)alloca(limbs*4);
		Multiply(limbs/2, cross_product, x, x + limbs/2);

		// Multiply the cross product by 2 and add it to the result
		u32 cross_carry = AddLeftShift32(limbs, product + limbs/2, cross_product, 1);

		// Roll the carry out up to the highest limb
		if (cross_carry) Add32(product + limbs*3/2, limbs/2, cross_carry);
	}
}
