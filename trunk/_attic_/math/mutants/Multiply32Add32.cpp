#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
	// Return the carry out from X = X * M + A
    u32 Multiply32Add32(
    	int limbs,	// Number of limbs in parameter A and B
    	u32 *X,		// Large number
    	u32 M,		// 32-bit number
    	u32 A)		// 32-bit number
	{
		u64 p = (u64)X[0] * M + A;
		X[0] = (u32)p;

		while (--limbs)
		{
			p = (u64)*(++X) * M + (u32)(p >> 32);
			*X = (u32)p;
		}

		return (u32)(p >> 32);
	}
}
