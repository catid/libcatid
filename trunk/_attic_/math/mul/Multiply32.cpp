#include <cat/math/BigInt.hpp>

namespace cat
{
	// Return the carry out from result = A * B
    u32 Multiply32(
    	int limbs,		// Number of limbs in parameter A, result
    	u32 *result,	// Large number
    	const u32 *A,	// Large number
    	u32 B)			// 32-bit number
	{
		u64 p = (u64)A[0] * B;
		result[0] = (u32)p;

		while (--limbs)
		{
			p = (u64)*(++A) * B + (u32)(p >> 32);
			*(++result) = (u32)p;
		}

		return (u32)(p >> 32);
	}
}
