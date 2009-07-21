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

bool BigRTL::Greater(const Leg *in_a, const Leg *in_b)
{
    int legs = library_legs;

    while (legs-- > 0)
    {
        Leg a = in_a[legs];
        Leg b = in_b[legs];
        if (a < b) return false;
        if (a > b) return true;
    }

    return false;
}

bool BigRTL::Less(const Leg *in_a, const Leg *in_b)
{
    int legs = library_legs;

    while (legs-- > 0)
    {
        Leg a = in_a[legs];
        Leg b = in_b[legs];
        if (a > b) return false;
        if (a < b) return true;
    }

    return false;
}

bool BigRTL::Equal(const Leg *in_a, const Leg *in_b)
{
    return 0 == memcmp(in_a, in_b, library_legs * sizeof(Leg));
}

bool BigRTL::EqualX(const Leg *in, Leg x)
{
    if (in[0] != x) return false;

    for (int ii = 1; ii < library_legs; ++ii)
        if (in[ii]) return false;

    return true;
}

bool BigRTL::IsZero(const Leg *in)
{
    for (int ii = 0; ii < library_legs; ++ii)
        if (in[ii]) return false;

    return true;
}
