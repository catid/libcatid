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

#include <cat/math/BigRTL.hpp>
using namespace cat;

u8 BigRTL::Double(const Leg *in, Leg *out)
{
    // Double low leg first
    Leg last = in[0];
    out[0] = last << 1;

    // Shift up the rest by 1 bit; actually pretty fast this way!
    for (int ii = 1; ii < library_legs; ++ii)
    {
        Leg next = in[ii];
        out[ii] = (next << 1) | (last >> (CAT_LEG_BITS-1));
        last = next;
    }

    return (u8)(last >> (CAT_LEG_BITS-1));
}
