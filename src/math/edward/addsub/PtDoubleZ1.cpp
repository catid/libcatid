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

#if defined(EXTENDED_T)
# undef PT_FN
# define PT_FN PtEDoubleZ1 /* Version that does produce the T coord */
#else
# define EXTENDED_T
# include "PtDoubleZ1.cpp"
# undef PT_FN
# define PT_FN PtDoubleZ1 /* Version that does not produce the T coord */
#endif

// Extended Twisted Edwards Dedicated Doubling Formula in 4M 3S 4a
void BigTwistedEdwards::PT_FN(const Leg *in, Leg *out)
{
    // A = X1^2, B = Y1^2, C = 2 * Z1^2 = 2
    MrSquare(in+XOFF, A);
    MrSquare(in+YOFF, B);
    //MrSquare(in+ZOFF, C);
    //MrDouble(C, C);

    // G = -A + B, F = G - C = G - 2, H = -A - B
    MrNegate(A, A);
    MrAdd(A, B, G);
    //MrSubtract(G, C, F);
    Copy(G, F);
    MrSubtractX(F, 2); // C = 2
    MrSubtract(A, B, H);

    // E = (X1 + Y1)^2 + H
    MrAdd(in+XOFF, in+YOFF, E);
    MrSquare(E, E);
    MrAdd(E, H, E);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
#if defined(EXTENDED_T)
    MrMultiply(E, H, out+TOFF);
#endif
    MrMultiply(F, G, out+ZOFF);
}

#undef EXTENDED_T
