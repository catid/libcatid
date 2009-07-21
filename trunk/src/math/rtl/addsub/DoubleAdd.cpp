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

Leg BigRTL::DoubleAdd(const Leg *in_a, const Leg *in_b, Leg *out)
{
    return DoubleAdd(library_legs, in_a, in_b, out);
}

// out = in_a[] * 2 + in_b[]
Leg BigRTL::DoubleAdd(int legs, const Leg *in_a, const Leg *in_b, Leg *out)
{
#if !defined(CAT_NO_LEGPAIR)

    LegPair x = ((LegPair)in_a[0] << 1) + in_b[0];
    out[0] = (Leg)x;

    for (int ii = 1; ii < legs; ++ii)
    {
        x = (x >> CAT_LEG_BITS) + ((LegPair)in_a[ii] << 1) + in_b[ii];
        out[ii] = (Leg)x;
    }

    return x >> CAT_LEG_BITS;

#else

    Leg p_hi;

    CAT_LEG_MULADD(in_a[0], 2, in_b[0], p_hi, out[0]);

    for (int ii = 1; ii < legs; ++ii)
        CAT_LEG_MULADD2(in_a[ii], 2, in_b[ii], p_hi, p_hi, out[ii]);

    return p_hi;

#endif
}
