#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// product = xy
	// memory space for product may not overlap with x,y
    void Multiply(
    	int limbs,		// Number of limbs in x,y
    	u32 *product,	// Product; buffer size = limbs*2
    	const u32 *x,	// Large number; buffer size = limbs
    	const u32 *y)	// Large number; buffer size = limbs
	{
		// Stop recursing under 640 bits or odd limb count
		if (limbs < 30 || (limbs & 1))
		{
			SimpleMultiply(limbs, product, x, y);
			return;
		}

		// Compute high and low products
		Multiply(limbs/2, product, x, y);
		Multiply(limbs/2, product + limbs, x + limbs/2, y + limbs/2);

		// Compute (x1 + x2), xc = carry out
		u32 *xsum = (u32*)alloca((limbs/2)*4);
		u32 xcarry = Add(xsum, x, limbs/2, x + limbs/2, limbs/2);

		// Compute (y1 + y2), yc = carry out
		u32 *ysum = (u32*)alloca((limbs/2)*4);
		u32 ycarry = Add(ysum, y, limbs/2, y + limbs/2, limbs/2);

		// Compute (x1 + x2) * (y1 + y2)
		u32 *cross_product = (u32*)alloca(limbs*4);
		Multiply(limbs/2, cross_product, xsum, ysum);

		// Subtract out the high and low products
		s32 cross_carry = Subtract(cross_product, limbs, product, limbs);
		cross_carry += Subtract(cross_product, limbs, product + limbs, limbs);

		// Fix the extra high carry bits of the result
		if (ycarry) cross_carry += Add(cross_product + limbs/2, limbs/2, xsum, limbs/2);
		if (xcarry) cross_carry += Add(cross_product + limbs/2, limbs/2, ysum, limbs/2);
		cross_carry += (xcarry & ycarry);

		// Add the cross product into the result
		cross_carry += Add(product + limbs/2, limbs*3/2, cross_product, limbs);

		// Add in the fixed high carry bits
		if (cross_carry) Add32(product + limbs*3/2, limbs/2, cross_carry);
	}
}
