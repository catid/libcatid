#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// {q, r} = u / v
	// q is not u or v
	// Return false on divide by zero
	bool Divide(
		const u32 *u,	// numerator, size = u_limbs
		int u_limbs,
		const u32 *v,	// denominator, size = v_limbs
		int v_limbs,
		u32 *q,			// quotient, size = u_limbs
		u32 *r)			// remainder, size = v_limbs
	{
		// calculate v_used and u_used
		int v_used = LimbDegree(v, v_limbs);
		if (!v_used) return false;

		int u_used = LimbDegree(u, u_limbs);

		// if u < v, avoid division
		if (u_used <= v_used && Less(u, u_used, v, v_used))
		{
			// r = u, q = 0
			Set(r, v_limbs, u, u_used);
			Set32(q, u_limbs, 0);
			return true;
		}

		// if v is 32 bits, use faster Divide32 code
		if (v_used == 1)
		{
			// {q, r} = u / v[0]
			Set(q, u_limbs, u);
			Set32(r, v_limbs, Divide32(u_limbs, q, v[0]));
			return true;
		}

		// calculate high zero bits in v's high used limb
		int shift = 32 - Degree32(v[v_used - 1]);
		int uu_used = u_used;
		if (shift > 0) uu_used++;

		u32 *uu = (u32*)alloca(uu_used*4);
		u32 *vv = (u32*)alloca(v_used*4);

		// shift left to fill high MSB of divisor
		if (shift > 0)
		{
			ShiftLeft(v_used, vv, v, shift);
			uu[u_used] = ShiftLeft(u_used, uu, u, shift);
		}
		else
		{
			Set(uu, u_used, u);
			Set(vv, v_used, v);
		}

		int q_high_index = uu_used - v_used;

		if (GreaterOrEqual(uu + q_high_index, v_used, vv, v_used))
		{
			Subtract(uu + q_high_index, v_used, vv, v_used);
			Set32(q + q_high_index, u_used - q_high_index, 1);
		}
		else
		{
			Set32(q + q_high_index, u_used - q_high_index, 0);
		}

		u32 *vq_product = (u32*)alloca((v_used+1)*4);

		// for each limb,
		for (int ii = q_high_index - 1; ii >= 0; --ii)
		{
			u64 q_full = *(u64*)(uu + ii + v_used - 1) / vv[v_used - 1];
			u32 q_low = (u32)q_full;
			u32 q_high = (u32)(q_full >> 32);

			vq_product[v_used] = Multiply32(v_used, vq_product, vv, q_low);

			if (q_high) // it must be '1'
				Add(vq_product + 1, v_used, vv, v_used);

			if (Subtract(uu + ii, v_used + 1, vq_product, v_used + 1))
			{
				--q_low;
				if (Add(uu + ii, v_used + 1, vv, v_used) == 0)
				{
					--q_low;
					Add(uu + ii, v_used + 1, vv, v_used);
				}
			}

			q[ii] = q_low;
		}

		memset(r + v_used, 0, (v_limbs - v_used)*4);
		ShiftRight(v_used, r, uu, shift);

		return true;
	}
}
