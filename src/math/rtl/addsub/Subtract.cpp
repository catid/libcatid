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

u8 BigRTL::Subtract(const Leg *in_a, const Leg *in_b, Leg *out)
{
    return Subtract(library_legs, in_a, in_b, out);
}

u8 BigRTL::Subtract(int legs, const Leg *in_a, const Leg *in_b, Leg *out)
{
#if !defined(CAT_NO_LEGPAIR)

    // Subtract first two legs without a borrow-in
    LegPairSigned diff = (LegPairSigned)in_a[0] - in_b[0];
    out[0] = (Leg)diff;

    // Subtract remaining legs
    for (int ii = 1; ii < legs; ++ii)
    {
        diff = ((diff >> CAT_LEG_BITS) + in_a[ii]) - in_b[ii];
        out[ii] = (Leg)diff;
    }

    return (u8)(diff >> CAT_LEG_BITS) & 1;

#else

    // Subtract first two legs without a borrow-in
    Leg t = in_a[0];
    Leg s = in_b[0];
    Leg w = t - s;
    u8 c = t < s;

    out[0] = w;

    // Subtract remaining legs
    for (int ii = 1; ii < legs; ++ii)
    {
        // Calculate difference
        Leg a = in_a[ii];
        Leg b = in_b[ii];
        Leg d = a - b - c;

        // Calculate borrow-out
        c = c ? (a < d || b == ~(Leg)0) : (a < b);

        out[ii] = d;
    }

    return c;

#endif
}
