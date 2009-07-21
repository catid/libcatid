#include <cat/math/BigInt.hpp>

namespace cat
{
	// Calculates mod_inv from low limb of modulus for Mon*()
	u32 MonReducePrecomp(u32 modulus0)
	{
		// mod_inv = -M ^ -1 (Mod 2^32)
		return MulInverse32(-(s32)modulus0);
	}
}
