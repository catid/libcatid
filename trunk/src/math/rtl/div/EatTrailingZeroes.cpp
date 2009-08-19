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

int BigRTL::EatTrailingZeroes(Leg *inout)
{
	// Count number of trailing zero legs
	int trailing_zero_legs = 0;
	for (; trailing_zero_legs < library_legs && !inout[trailing_zero_legs]; ++trailing_zero_legs);

	// Move out the zero legs
	MoveLegsRight(inout, trailing_zero_legs, inout);

	// Count number of trailing zero bits
	int trailing_zero_bits = CAT_TRAILING_ZEROES(inout[0]);

	// Shift out the zero bits
	ShiftRight(library_legs, inout, trailing_zero_bits, inout);

	// Return number of eatten zero bits
	return trailing_zero_legs * CAT_LEG_BITS + trailing_zero_bits;
}
