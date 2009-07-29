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

u8 BigRTL::Add(int legs_a, const Leg *in_a, int legs_b, const Leg *in_b, Leg *out)
{
#if !defined(CAT_NO_LEGPAIR)

    // Add first two legs without a carry-in
    LegPair sum = (LegPair)in_a[0] + in_b[0];
    out[0] = (Leg)sum;

    // Add remaining legs
    int ii;
    for (ii = 1; ii < legs_b; ++ii)
    {
        sum = ((sum >> CAT_LEG_BITS) + in_a[ii]) + in_b[ii];
        out[ii] = (Leg)sum;
    }

    for (; ii < legs_a; ++ii)
    {
        sum = (sum >> CAT_LEG_BITS) + in_a[ii];
        out[ii] = (Leg)sum;
    }

    return (u8)(sum >> CAT_LEG_BITS);

#else

    // Add first two legs without a carry-in
    Leg t = in_a[0];
    Leg s = t + in_b[0];
    u8 c = s < t;

    out[0] = s;

    // Add remaining legs
    int ii;
    for (ii = 1; ii < legs_b; ++ii)
    {
        // Calculate sum
        Leg a = in_a[ii];
        Leg b = in_b[ii];
        Leg sum = a + b + c;

        // Calculate carry
        c = c ? sum <= a : sum < a;

        out[ii] = sum;
    }

    for (; ii < legs_a; ++ii)
    {
        // Calculate sum
        Leg a = in_a[ii];
        Leg sum = a + c;

        // Calculate carry
        c = c ? sum <= a : sum < a;

        out[ii] = sum;
    }

    return c;

#endif
}
