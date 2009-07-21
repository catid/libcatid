#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Convert bigint to string
	std::string ToStr(const u32 *n, int limbs, int base)
	{
		limbs = LimbDegree(n, limbs);
		if (!limbs) return "0";

		std::string out;
		char ch;

		u32 *m = (u32*)alloca(limbs*4);
		Set(m, limbs, n, limbs);

		while (limbs)
		{
			u32 mod = Divide32(limbs, m, base);
			if (mod <= 9) ch = '0' + mod;
			else ch = 'A' + mod - 10;
			out = ch + out;
			limbs = LimbDegree(m, limbs);
		}

		return out;
	}
}
