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

#include <cat/math/BigTwistedEdwards.hpp>
using namespace cat;

// Verify that the point (x,y) exists on the given curve
bool BigTwistedEdwards::PtValidAffine(const Leg *in)
{
    // 0 = 1 + d*x^2*y^2 + x^2 - y^2

    // A = x^2
    MrSquare(in+XOFF, A);

    // B = y^2
    MrSquare(in+YOFF, B);

    // C = A * B * d + 1 + A - B
    MrMultiply(A, B, C);
    MrMultiplyX(C, curve_d, C);
    MrAddX(C, 1);
    MrAdd(C, A, C);
    MrSubtract(C, B, C);
    MrReduce(C);

    // If the result is zero, it is on the curve
    return IsZero(C);
}
