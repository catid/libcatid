#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Compute n_residue for Montgomery reduction
	void MonInputResidue(
		const u32 *n,		//	Large number, buffer size = n_limbs
		int n_limbs,		//	Number of limbs in n
		const u32 *modulus,	//	Large number, buffer size = m_limbs
		int m_limbs,		//	Number of limbs in modulus
		u32 *n_residue)		//	Result, buffer size = m_limbs
	{
		// p = n * 2^(k*m)
		u32 *p = (u32*)alloca((n_limbs+m_limbs)*4);
		Set(p+m_limbs, n_limbs, n, n_limbs);
		Set32(p, m_limbs, 0);

		// n_residue = p (Mod modulus)
		Modulus(p, n_limbs+m_limbs, modulus, m_limbs, n_residue);
	}
}
