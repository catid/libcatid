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

// Simultaneous Add and Subtract for efficient precomputation (A +/- B) in 14M 1D 11a (versus 16M 2D 16a)
void BigTwistedEdwards::PtPrecompAddSub(const Leg *in_a, const Leg *in_b, Leg *sum, Leg *diff, int neg_offset)
{
    // A = (Y1 - X1) * (Y2 - X2)
    MrSubtract(in_a+YOFF, in_a+XOFF, C);
    MrSubtract(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, F);

    // B = (Y1 + X1) * (Y2 + X2)
    MrAdd(in_a+YOFF, in_a+XOFF, E);
    MrAdd(in_b+YOFF, in_b+XOFF, H);
    MrMultiply(E, H, G);

    // I = (Y1 - X1) * (Y2 + X2)
    MrMultiply(C, H, A);

    // J = (Y1 + X1) * (Y2 - X2)
    MrMultiply(E, D, B);

    // C = 2 * d * T1 * T2 (can remove multiplication by d if inputs are known to be different)
    MrMultiply(in_a+TOFF, in_b+TOFF, C);
    MrMultiplyX(C, curve_d * 2, C);

    // D = 2 * Z1 * Z2
    MrMultiply(in_a+ZOFF, in_b+ZOFF, D);
    MrDouble(D, D);

    // E = B - A, F = D - C, G = D + C, H = B + A
    MrSubtract(G, F, E);
    MrAdd(G, F, H);
    MrSubtract(D, C, F);
    MrAdd(D, C, G);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, sum+XOFF);
    MrMultiply(G, H, sum+YOFF);
    MrMultiply(E, H, sum+TOFF);
    MrMultiply(F, G, sum+ZOFF);
    PtNegate(sum, sum + neg_offset);

    // E = J - I, F <-> G, H = J + I
    MrSubtract(B, A, E);
    MrAdd(B, A, H);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, G, diff+XOFF);
    MrMultiply(F, H, diff+YOFF);
    MrMultiply(E, H, diff+TOFF);
    MrMultiply(G, F, diff+ZOFF);
    PtNegate(diff, diff + neg_offset);
}
