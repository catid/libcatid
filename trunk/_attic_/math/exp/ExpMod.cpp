#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Computes: result = base ^ exponent (Mod modulus)
	// Using Montgomery multiplication with simple squaring method
	void ExpMod(
		const u32 *base,	//	Base for exponentiation, buffer size = base_limbs
		int base_limbs,		//	Number of limbs in base
		const u32 *exponent,//	Exponent, buffer size = exponent_limbs
		int exponent_limbs,	//	Number of limbs in exponent
		const u32 *modulus,	//	Modulus, buffer size = mod_limbs
		int mod_limbs,		//	Number of limbs in modulus
		u32 mod_inv,		//	MonReducePrecomp() return
		u32 *result)		//	Result, buffer size = mod_limbs
	{
		u32 *mon_base = (u32*)alloca(mod_limbs*4);
		MonInputResidue(base, base_limbs, modulus, mod_limbs, mon_base);

		MonExpMod(mon_base, exponent, exponent_limbs, modulus, mod_limbs, mod_inv, result);

		MonFinish(mod_limbs, result, modulus, mod_inv);
	}
}
