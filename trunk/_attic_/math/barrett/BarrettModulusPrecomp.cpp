#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// m_inv ~= 2^(2k)/m
	// Generates m_inv parameter of BarrettModulus()
	// It is limbs in size, chopping off the 2^k bit
	// Only works for m with the high bit set
	void BarrettModulusPrecomp(
		int limbs,		// Number of limbs in m and m_inv
		const u32 *m,	// Modulus, size = limbs
		u32 *m_inv)		// Large number result, size = limbs
	{
		u32 *q = (u32*)alloca((limbs*2+1)*4);

		// q = 2^(2k)
		Set32(q, limbs*2, 0);
		q[limbs*2] = 1;

		// q /= m
		Divide(q, limbs*2+1, m, limbs, q, m_inv);

		// m_inv = q
		Set(m_inv, limbs, q);
	}
}
