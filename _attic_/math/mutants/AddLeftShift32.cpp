#include <cat/math/BigInt.hpp>

namespace cat
{
	// Return the carry out from A += B << S
    u32 AddLeftShift32(
    	int limbs,		// Number of limbs in parameter A and B
    	u32 *A,			// Large number
    	const u32 *B,	// Large number
    	u32 S)			// 32-bit number
	{
		u64 sum = 0;
		u32 last = 0;

		while (limbs--)
		{
			u32 b = *B++;

			sum = (u64)((b << S) | (last >> (32 - S))) + *A + (u32)(sum >> 32);

			last = b;
			*A++ = (u32)sum;
		}

		return (u32)(sum >> 32) + (last >> (32 - S));
	}
}
