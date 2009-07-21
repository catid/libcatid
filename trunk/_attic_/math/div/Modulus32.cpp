#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Returns the remainder of N / divisor for a 32-bit divisor
    u32 Modulus32(
    	int limbs,		// Number of limbs in parameter N
    	const u32 *N,	// Large number, buffer size = limbs
    	u32 divisor)	// 32-bit number
	{
		u32 remainder = N[limbs-1] < divisor ? N[limbs-1] : 0;
		u32 counter = N[limbs-1] < divisor ? limbs-1 : limbs;

		while (counter--) remainder = (u32)((((u64)remainder << 32) | N[counter]) % divisor);

		return remainder;
	}
}
