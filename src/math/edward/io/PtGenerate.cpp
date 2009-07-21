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

#include <cat/math/BigTwistedEdward.hpp>
using namespace cat;

// Generate a random point on the curve that is not part of a small subgroup
void BigTwistedEdward::PtGenerate(IRandom *prng, Leg *out)
{
    // Generate affine (x,y) point on the curve
    do {
        PtFillRandomX(prng, out);
        PtSolveAffineY(out);
    } while (!PtValidAffine(out));

    // #E(Fp) = large prime * cofactor h
    // Assumes cofactor h = 4
    // P = hP, to insure it is in the large prime-order subgroup
    PtDoubleZ1(out, out);
    PtEDouble(out, out);
}
