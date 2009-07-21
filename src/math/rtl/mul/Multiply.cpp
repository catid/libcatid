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

void BigRTL::Multiply(const Leg *in_a, const Leg *in_b, Leg *out)
{
    out[library_legs] = MultiplyX(in_a, in_b[0], out);

    for (int ii = 1; ii < library_legs; ++ii)
        out[library_legs + ii] = MultiplyXAdd(in_a, in_b[ii], out + ii, out + ii);
}

void BigRTL::MultiplyLow(const Leg *in_a, const Leg *in_b, Leg *out)
{
    MultiplyX(in_a, in_b[0], out);

    for (int ii = 1; ii < library_legs; ++ii)
        MultiplyXAdd(library_legs - ii, in_a, in_b[ii], out + ii, out + ii);
}
