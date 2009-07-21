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

void BigRTL::Negate(const Leg *in, Leg *out)
{
    int ii;

    // Ripple the borrow in as far as needed
    for (ii = 0; ii < library_legs; ++ii)
        if ((out[ii] = ~in[ii] + 1))
            break;

    // Invert remaining bits
    for (; ii < library_legs; ++ii)
        out[ii] = ~in[ii];
}
