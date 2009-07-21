#include <cat/math/BigInt.hpp>

namespace cat
{
	// {out, carry} = in <<< shift
	// Precondition: 0 <= shift < 31
	u32 ShiftLeft(int limbs, u32 *out, const u32 *in, int shift)
	{
		if (!shift)
		{
			Set(out, limbs, in);
			return 0;
		}

		u32 carry = 0;

		for (int ii = 0; ii < limbs; ++ii)
		{
			u32 r = in[ii];

			out[ii] = (r << shift) | carry;

			carry = r >> (32 - shift);
		}

		return carry;
	}
}
