#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Computes multiplicative inverse of given number
	// Such that: result * u = 1
	// Using Extended Euclid's Algorithm (GCDe)
	// This is not always possible, so it will return false iff not possible.
	bool MulInverse(
		int limbs,		// Limbs in u and result
		const u32 *u,	// Large number, buffer size = limbs
		u32 *result)	// Large number, buffer size = limbs
	{
		u32 *u1 = (u32*)alloca(limbs*4);
		u32 *u3 = (u32*)alloca(limbs*4);
		u32 *v1 = (u32*)alloca(limbs*4);
		u32 *v3 = (u32*)alloca(limbs*4);
		u32 *t1 = (u32*)alloca(limbs*4);
		u32 *t3 = (u32*)alloca(limbs*4);
		u32 *q = (u32*)alloca((limbs+1)*4);
		u32 *w = (u32*)alloca((limbs+1)*4);

		// Unrolled first iteration
		{
			Set32(u1, limbs, 0);
			Set32(v1, limbs, 1);
			Set(v3, limbs, u);
		}

		// Unrolled second iteration
		if (!LimbDegree(v3, limbs))
			return false;

		// {q, t3} <- R / v3
		Set32(w, limbs, 0);
		w[limbs] = 1;
		Divide(w, limbs+1, v3, limbs, q, t3);

		SimpleMultiplyLowHalf(limbs, t1, q, v1);
		Add(t1, limbs, u1, limbs);

		for (;;)
		{
			if (!LimbDegree(t3, limbs))
			{
				Set(result, limbs, v1);
				return Equal32(v3, limbs, 1);
			}

			Divide(v3, limbs, t3, limbs, q, u3);
			SimpleMultiplyLowHalf(limbs, u1, q, t1);
			Add(u1, limbs, v1, limbs);

			if (!LimbDegree(u3, limbs))
			{
				Negate(limbs, result, t1);
				return Equal32(t3, limbs, 1);
			}

			Divide(t3, limbs, u3, limbs, q, v3);
			SimpleMultiplyLowHalf(limbs, v1, q, u1);
			Add(v1, limbs, t1, limbs);

			if (!LimbDegree(v3, limbs))
			{
				Set(result, limbs, u1);
				return Equal32(u3, limbs, 1);
			}

			Divide(u3, limbs, v3, limbs, q, t3);
			SimpleMultiplyLowHalf(limbs, t1, q, v1);
			Add(t1, limbs, u1, limbs);

			if (!LimbDegree(t3, limbs))
			{
				Negate(limbs, result, v1);
				return Equal32(v3, limbs, 1);
			}

			Divide(v3, limbs, t3, limbs, q, u3);
			SimpleMultiplyLowHalf(limbs, u1, q, t1);
			Add(u1, limbs, v1, limbs);

			if (!LimbDegree(u3, limbs))
			{
				Set(result, limbs, t1);
				return Equal32(t3, limbs, 1);
			}

			Divide(t3, limbs, u3, limbs, q, v3);
			SimpleMultiplyLowHalf(limbs, v1, q, u1);
			Add(v1, limbs, t1, limbs);

			if (!LimbDegree(v3, limbs))
			{
				Negate(limbs, result, u1);
				return Equal32(u3, limbs, 1);
			}

			Divide(u3, limbs, v3, limbs, q, t3);
			SimpleMultiplyLowHalf(limbs, t1, q, v1);
			Add(t1, limbs, u1, limbs);
		}
	}
}
