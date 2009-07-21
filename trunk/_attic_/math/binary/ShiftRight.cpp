#include <cat/math/BigInt.hpp>

namespace cat
{
	// out = in >>> shift
	// Precondition: 0 <= shift < 31
	void ShiftRight(int limbs, u32 *out, const u32 *in, int shift)
	{
		if (!shift)
		{
			Set(out, limbs, in);
			return;
		}

		u32 carry = 0;

		for (int ii = limbs - 1; ii >= 0; --ii)
		{
			u32 r = in[ii];

			out[ii] = (r >> shift) | carry;

			carry = r << (32 - shift);
		}
	}
}
