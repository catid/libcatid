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

#if defined(EXTENDED_T)
# define PT_FN PtEAdd /* Version that does produce the T coord */
#else
# define EXTENDED_T
# include "PtAdd.cpp"
# undef PT_FN
# define PT_FN PtAdd /* Version that does not produce the T coord */
#endif

// Extended Twisted Edwards Unified Addition Formula (works when both inputs are the same) in 8M 1D 8A
void BigTwistedEdward::PT_FN(const Leg *in_a, const Leg *in_b, Leg *out)
{
    // A = (Y1 - X1) * (Y2 - X2)
    MrSubtract(in_a+YOFF, in_a+XOFF, C);
    MrSubtract(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, A);

    // B = (Y1 + X1) * (Y2 + X2)
    MrAdd(in_a+YOFF, in_a+XOFF, C);
    MrAdd(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, B);

    // C = 2 * d * T1 * T2 (can remove multiplication by d if inputs are known to be different)
    MrMultiply(in_a+TOFF, in_b+TOFF, C);
    MrMultiplyX(C, curve_d * 2, C);

    // D = 2 * Z1 * Z2
    MrMultiply(in_a+ZOFF, in_b+ZOFF, D);
    MrDouble(D, D);

    // E = B - A, F = D - C, G = D + C, H = B + A
    MrSubtract(B, A, E);
    MrSubtract(D, C, F);
    MrAdd(D, C, G);
    MrAdd(B, A, H);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
#if defined(EXTENDED_T)
    MrMultiply(E, H, out+TOFF);
#endif
    MrMultiply(F, G, out+ZOFF);
}
