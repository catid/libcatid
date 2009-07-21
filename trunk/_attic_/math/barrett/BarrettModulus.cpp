#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// r = x mod m
	// Using Barrett's method with precomputed m_inv
	void BarrettModulus(
		int limbs,			// Number of limbs in m and m_inv
		const u32 *x,		// Number to reduce, size = limbs*2
		const u32 *m,		// Modulus, size = limbs
		const u32 *m_inv,	// R/Modulus, precomputed, size = limbs
		u32 *result)		// Large number result
	{
		// q2 = x * m_inv
		// Skips the low limbs+1 words and some high limbs too
		// Needs to partially calculate the next 2 words below for carries
		u32 *q2 = (u32*)alloca((limbs+3)*4);
		int ii, jj = limbs - 1;

		// derived from the fact that m_inv[limbs] was always 1, so m_inv is the same length as modulus now
		*(u64*)q2 = (u64)m_inv[jj] * x[jj];
		*(u64*)(q2 + 1) = (u64)q2[1] + x[jj];

		for (ii = 1; ii < limbs; ++ii)
			*(u64*)(q2 + ii + 1) = ((u64)q2[ii + 1] + x[jj + ii]) + AddMultiply32(ii + 1, q2, m_inv + jj - ii, x[jj + ii]);

		*(u64*)(q2 + ii + 1) = ((u64)q2[ii + 1] + x[jj + ii]) + AddMultiply32(ii, q2 + 1, m_inv, x[jj + ii]);

		q2 += 2;

		// r2 = (q3 * m2) mod b^(k+1)
		u32 *r2 = (u32*)alloca((limbs+1)*4);

		// Skip high words in product, also input limbs are different by 1
		Multiply32(limbs + 1, r2, q2, m[0]);
		for (int ii = 1; ii < limbs; ++ii)
			AddMultiply32(limbs + 1 - ii, r2 + ii, q2, m[ii]);

		// Correct the error of up to two modulii
		u32 *r = (u32*)alloca((limbs+1)*4);
		if (Subtract(r, x, limbs+1, r2, limbs+1))
		{
			while (!Subtract(r, limbs+1, m, limbs));
		}
		else
		{
			while (GreaterOrEqual(r, limbs+1, m, limbs))
				Subtract(r, limbs+1, m, limbs);
		}

		Set(result, limbs, r);
	}
}
