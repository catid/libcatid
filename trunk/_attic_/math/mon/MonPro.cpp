#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// result = a * b * r^-1 (Mod modulus) in Montgomery domain
	void MonPro(
		int limbs,				// Number of limbs in each parameter
		const u32 *a_residue,	// Large number, buffer size = limbs
		const u32 *b_residue,	// Large number, buffer size = limbs
		const u32 *modulus,		// Large number, buffer size = limbs
		u32 mod_inv,			// MonReducePrecomp() return
		u32 *result)			// Large number, buffer size = limbs
	{
		u32 *t = (u32*)alloca(limbs*2*4);

		Multiply(limbs, t, a_residue, b_residue);
		MonReduce(limbs, t, modulus, mod_inv, result);
	}
}
