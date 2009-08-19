/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cat/math/BigPseudoMersenne.hpp>
using namespace cat;

void BigPseudoMersenne::MrSquareRoot(const Leg *in, Leg *out)
{
	if ((CachedModulus[0] & 3) == 3)
	{
	    // Square root for modulus p = 3 mod 4
		// out = in ^ (m + 1)/4

		// Same algorithm from MrInvert()
		Leg *T = Get(pm_regs - 4);
		Leg *S = Get(pm_regs - 5);

		// Optimal window size is sqrt(bits-16)
		const int w = 16; // Constant window size, optimal for 256-bit modulus

		// Perform exponentiation for the first w bits
		Copy(in, S);
		int ctr = w - 1;
		while (ctr--)
		{
			MrSquare(S, S);
			MrMultiply(S, in, S);
		}

		// Store result in a temporary register
		Copy(S, T);

		// NOTE: This assumes that modulus_c < 16384 = 2^(w-2)
		int one_frames = (RegBytes()*8 - w*2) / w;
		while (one_frames--)
		{
			// Just multiply once re-using the first result, every 16 bits
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrMultiply(S, T, S);
		}

		// For the final leg just do bitwise exponentiation
		// NOTE: Makes use of the fact that the window size is a power of two
		Leg m_low = 1 - modulus_c;
		for (Leg bit = (Leg)1 << (w - 1); bit >= 4; bit >>= 1)
		{
			MrSquare(S, S);

			if (m_low & bit)
				MrMultiply(S, in, S);
		}

		Copy(S, out);
	}
	else if ((CachedModulus[0] & 7) == 5)
	{
	    // Square root for modulus p = 5 mod 8 using Atkin's method:
		// S = (2*in) ^ (m - 5)/8
		// out = in * S * ((2*in * S^2) - 1)

		// Same algorithm from MrInvert()
		Leg *T = Get(pm_regs - 4);
		Leg *S = Get(pm_regs - 5);

		// Optimal window size is sqrt(bits-16)
		const int w = 16; // Constant window size, optimal for 256-bit modulus

		// Perform exponentiation for the first w bits
		Copy(in, S);
		MrDouble(S, S);
		int ctr = w - 1;
		while (ctr--)
		{
			MrSquare(S, S);
			MrMultiply(S, in, S);
		}

		// Store result in a temporary register
		Copy(S, T);

		// NOTE: This assumes that modulus_c < 8192 = 2^(w-3)
		int one_frames = (RegBytes()*8 - w*2) / w;
		while (one_frames--)
		{
			// Just multiply once re-using the first result, every 16 bits
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
			MrMultiply(S, T, S);
		}

		// For the final leg just do bitwise exponentiation
		// NOTE: Makes use of the fact that the window size is a power of two
		Leg m_low = 0 - modulus_c;
		for (Leg bit = (Leg)1 << (w - 1); bit >= 8; bit >>= 1)
		{
			MrSquare(S, S);

			if (m_low & bit)
				MrMultiply(S, in, S);
		}

		// T = 2*in * S^2 - 1
		MrSquare(S, T);
		MrMultiply(T, in, T);
		MrDouble(T, T);
		MrSubtractX(T, 1);

		// out = in * S * T
		MrMultiply(in, S, out);
		MrMultiply(out, T, out);
	}
}
