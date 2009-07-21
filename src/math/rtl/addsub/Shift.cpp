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

Leg BigRTL::ShiftLeft(const Leg *in, int shift, Leg *out)
{
    return ShiftLeft(library_legs, in, shift, out);
}

Leg BigRTL::ShiftLeft(int legs, const Leg *in, int shift, Leg *out)
{
    if (!shift)
    {
        memcpy(out, in, legs * sizeof(Leg));
        return 0;
    }

    Leg carry = in[0];

    out[0] = carry << shift;

    for (int ii = 1; ii < legs; ++ii)
    {
        Leg x = in[ii];
        out[ii] = (x << shift) | (carry >> (CAT_LEG_BITS - shift));
        carry = x;
    }

    return carry >> (CAT_LEG_BITS - shift);
}

Leg BigRTL::ShiftRight(int legs, const Leg *in, int shift, Leg *out)
{
    if (!shift)
    {
        memcpy(out, in, legs * sizeof(Leg));
        return 0;
    }

    Leg carry = in[legs-1];

    out[legs-1] = carry >> shift;

    for (int ii = legs-2; ii >= 0; --ii)
    {
        Leg x = in[ii];
        out[ii] = (x >> shift) | (carry << (CAT_LEG_BITS - shift));
        carry = x;
    }

    return carry << (CAT_LEG_BITS - shift);
}
