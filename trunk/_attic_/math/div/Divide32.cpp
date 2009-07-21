#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	/*
	 * 'A' is overwritten with the quotient of the operation
	 * Returns the remainder of 'A' / divisor for a 32-bit divisor
	 *
	 * Does not check for divide-by-zero
	 */
    u32 Divide32(
    	int limbs,		// Number of limbs in parameter A
    	u32 *A,			// Large number, buffer size = limbs
    	u32 divisor)	// 32-bit number
	{
		u64 r = 0;
		for (int ii = limbs-1; ii >= 0; --ii)
		{
			u64 n = (r << 32) | A[ii];
			A[ii] = (u32)(n / divisor);
			r = n % divisor;
		}

		return (u32)r;
	}
}
