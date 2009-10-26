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

// Solve for Y given the X point on a curve
void BigTwistedEdwards::PtSolveAffineY(Leg *inout)
{
    // y = sqrt[(1 + x^2) / (1 - d*x^2)]

    // B = x^2
    MrSquare(inout+XOFF, B);

    // A = 1/(1 - d*B)
    MrMultiplyX(B, curve_d, A);
    MrNegate(A, A);
    MrAddX(A, 1);
    MrInvert(A, A);

    // y = sqrt(A*(B+1))
    MrAddX(B, 1);
    MrMultiply(A, B, inout+YOFF);
    MrSquareRoot(inout+YOFF, inout+YOFF);
    MrReduce(inout+YOFF);
}
