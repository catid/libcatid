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

u8 BigRTL::SubtractX(Leg *inout, Leg x)
{
    Leg t = inout[0];
    inout[0] = t - x;

    // If the initial difference did not borrow in, return 0
    if (t >= x) return 0;

    // Ripple the borrow in as far as needed
    for (int ii = 1; ii < library_legs; ++ii)
        if (inout[ii]--) return 0;

    return 1;
}
