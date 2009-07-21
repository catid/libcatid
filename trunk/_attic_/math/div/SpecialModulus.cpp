#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// r = x % m
	// For m in the special form: 2^(N=wordbits*m_limbs) - c
	// Assumes c < 2^28, to allow for numerators 4x larger than m
	void SpecialModulus(
		const u32 *x,	// numerator, size = x_limbs
		int x_limbs,	// always > m_limbs
		u32 c,			// small subtracted constant in m
		int m_limbs,	// number of limbs in modulus
		u32 *r)			// remainder, size = m_limbs
	{
		const u32 *q = x + m_limbs;
		int q_limbs = LimbDegree(q, x_limbs - m_limbs); // always >= 0
		u32 r_overflow = 0;
		u32 *qr = (u32*)alloca((x_limbs+1)*4);

		// Unrolled first loop to avoid some copies
		if (!q_limbs)
		{
			// r = x(mod 2^N)
			Set(r, m_limbs, x);
		}
		else
		{
			// q|r = q*c / b^t
			qr[q_limbs++] = Multiply32(q_limbs, qr, q, c);

			if (q_limbs <= m_limbs)
				r_overflow += Add(r, x, m_limbs, qr, q_limbs);
			else
			{
				r_overflow += Add(r, x, m_limbs, qr, m_limbs);

				q = qr + m_limbs;

				for (;;)
				{
					q_limbs -= m_limbs;
					q_limbs = LimbDegree(q, q_limbs);
					if (!q_limbs) break;

					// q|r = q*c / b^t
					qr[q_limbs++] = Multiply32(q_limbs, qr, q, c);

					if (q_limbs <= m_limbs)
					{
						r_overflow += Add(r, m_limbs, qr, q_limbs);
						break;
					}
					else
						r_overflow += Add(r, m_limbs, qr, m_limbs);
				}
			}
		}

		// r is probably a few moduli larger than it should be

		// Get it down to a number that could be just one modulus too large
		// To subtract 2^N - c, we just need to add c a few times
		if (r_overflow) Add32(r, m_limbs, r_overflow * c);

		// If number is one modulus too large, adding c to it will cause an
		// overflow, and we can use the low words of the result
		Set(qr, m_limbs, r);
		if (Add32(qr, m_limbs, c))
			Set(r, m_limbs, qr);
	}
}
