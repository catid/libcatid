#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Computes: result = (1/u) (Mod v)
	// Such that: result * u (Mod v) = 1
	// Using Extended Euclid's Algorithm (GCDe)
	// This is not always possible, so it will return false iff not possible.
	bool InvMod(
		const u32 *u,	// Large number, buffer size = u_limbs
		int u_limbs,	// Limbs in u
		const u32 *v,	// Large number, buffer size = limbs
		int limbs,		// Limbs in modulus(v) and result
		u32 *result)	// Large number, buffer size = limbs
	{
		u32 *u1 = (u32*)alloca(limbs*4);
		u32 *u3 = (u32*)alloca(limbs*4);
		u32 *v1 = (u32*)alloca(limbs*4);
		u32 *v3 = (u32*)alloca(limbs*4);
		u32 *t1 = (u32*)alloca(limbs*4);
		u32 *t3 = (u32*)alloca(limbs*4);
		u32 *q = (u32*)alloca((limbs + u_limbs)*4);

		// Unrolled first iteration
		{
			Set32(u1, limbs, 0);
			Set32(v1, limbs, 1);
			Set(u3, limbs, v);

			// v3 = u % v
			Modulus(u, u_limbs, v, limbs, v3);
		}

		for (;;)
		{
			if (!LimbDegree(v3, limbs))
			{
				Subtract(result, v, limbs, u1, limbs);
				return Equal32(u3, limbs, 1);
			}

			Divide(u3, limbs, v3, limbs, q, t3);
			SimpleMultiplyLowHalf(limbs, t1, q, v1);
			Add(t1, limbs, u1, limbs);

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
				Subtract(result, v, limbs, t1, limbs);
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
				Subtract(result, v, limbs, v1, limbs);
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
		}
	}
}
