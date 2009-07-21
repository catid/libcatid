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

void BigPseudoMersenne::MrNegate(const Leg *in, Leg *out)
{
    // It's like SubtractX: out = m - in = ~in-c+1 = ~in - (c-1)
    Leg t = ~in[0];
    Leg x = modulus_c - 1;
    out[0] = t - x;

    int ii = 1;

    // If the initial difference borrowed in,
    if (t < x)
    {
        // Ripple the borrow in as far as needed
        while (ii < library_legs)
        {
            t = ~in[ii];
            out[ii++] = t - 1;
            if (t) break;
        }
    }

    // Invert remaining bits
    for (; ii < library_legs; ++ii)
        out[ii] = ~in[ii];
}
