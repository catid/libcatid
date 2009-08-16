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

void BigPseudoMersenne::MrReduceProductX(Leg overflow, Leg *inout)
{
    // Pseudo-Mersenne reduction
    Leg p_hi, p_lo;
    CAT_LEG_MULADD(overflow, modulus_c, inout[0], p_hi, p_lo);

    inout[0] = p_lo;

    // If the initial sum carried out,
    if ((inout[1] += p_hi) < p_hi)
    {
        // Ripple the carry out as far as needed
        for (int ii = 2; ii < library_legs; ++ii)
            if (++inout[ii]) return;

		while (AddX(inout, modulus_c));
    }
}

void BigPseudoMersenne::MrReduceProduct(const Leg *in_hi, const Leg *in_lo, Leg *out)
{
    // Pseudo-Mersenne reduction
    Leg overflow = MultiplyXAdd(in_hi, modulus_c, in_lo, out);

    MrReduceProductX(overflow, out);
}
