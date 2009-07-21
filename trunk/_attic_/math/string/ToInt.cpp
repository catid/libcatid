#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Convert string to bigint
	// Return 0 if string contains non-digit characters, else number of limbs used
	int ToInt(u32 *lhs, int max_limbs, const char *rhs, u32 base)
	{
		if (max_limbs < 2) return 0;

		lhs[0] = 0;
		int used = 1;

		char ch;
		while ((ch = *rhs++))
		{
			u32 mod;
			if (ch >= '0' && ch <= '9') mod = ch - '0';
			else mod = toupper(ch) - 'A' + 10;
			if (mod >= base) return 0;

			// lhs *= base
			u32 carry = Multiply32Add32(used, lhs, base, mod);

			// react to running out of room
			if (carry)
			{
				if (used >= max_limbs)
					return 0;

				lhs[used++] = carry;
			}
		}

		if (used < max_limbs)
			Set32(lhs+used, max_limbs-used, 0);

		return used;
	}
}
