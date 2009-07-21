#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// returns b ^ e (Mod m)
	u32 ExpMod32(u32 b, u32 e, u32 m)
	{
		// validate arguments
		if (b == 0 || m <= 1) return 0;
		if (e == 0) return 1;

		// find high bit of exponent
		u32 mask = 0x80000000;
		while ((e & mask) == 0) mask >>= 1;

		// seen 1 set bit, so result = base so far
		u32 r = b;

		while (mask >>= 1)
		{
			// VS.NET does a poor job recognizing that the division
			// is just an IDIV with a 32-bit dividend (not 64-bit) :-(

			// r = r^2 (mod m)
			r = (u32)(((u64)r * r) % m);

			// if exponent bit is set, r = r*b (mod m)
			if (e & mask) r = (u32)(((u64)r * b) % m);
		}

		return r;
	}
}
