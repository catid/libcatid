#include <cat/math/BigInt.hpp>

namespace cat
{
	// result = a^-1 (Mod modulus) in Montgomery domain
	void MonInverse(
		int limbs,				// Number of limbs in each parameter
		const u32 *a_residue,	// Large number, buffer size = limbs
		const u32 *modulus,		// Large number, buffer size = limbs
		u32 mod_inv,			// MonReducePrecomp() return
		u32 *result)			// Large number, buffer size = limbs
	{
		Set(result, limbs, a_residue);
		MonFinish(limbs, result, modulus, mod_inv);
		InvMod(result, limbs, modulus, limbs, result);
		MonInputResidue(result, limbs, modulus, limbs, result);
	}
}
