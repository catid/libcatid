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

#include "CombaMul.cpp"

void BigRTL::Multiply(const Leg *in_a, const Leg *in_b, Leg *out)
{
	switch (library_legs)
	{
	// The compiler really grinds to build this, so I have limited the number of cases that use template metaprogramming
#if defined(CAT_ARCH_64)
	case 4: CombaMul<4>(in_a, in_b, out); return;
	case 6: CombaMul<6>(in_a, in_b, out); return;
#endif
	case 8: CombaMul<8>(in_a, in_b, out); return;
#if defined(CAT_ARCH_32)
	case 12: CombaMul<12>(in_a, in_b, out); return;
	case 16: CombaMul<16>(in_a, in_b, out); return;
#endif
	}

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
