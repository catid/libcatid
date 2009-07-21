#include <cat/math/BigInt.hpp>

namespace cat
{
	// returns (n ^ -1) Mod 2^32
	u32 MulInverse32(u32 n)
	{
		// {u1, g1} = 2^32 / n
		u32 hb = (~(n - 1) >> 31);
		u32 u1 = -(s32)(0xFFFFFFFF / n + hb);
		u32 g1 = ((-(s32)hb) & (0xFFFFFFFF % n + 1)) - n;

		if (!g1) {
			if (n != 1) return 0;
			else return 1;
		}

		u32 q, u = 1, g = n;

		for (;;) {
			q = g / g1;
			g %= g1;

			if (!g) {
				if (g1 != 1) return 0;
				else return u1;
			}

			u -= q*u1;
			q = g1 / g;
			g1 %= g;

			if (!g1) {
				if (g != 1) return 0;
				else return u;
			}

			u1 -= q*u;
		}
	}
}
