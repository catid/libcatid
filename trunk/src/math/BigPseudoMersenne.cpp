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

#include <cat/math/BigPseudoMersenne.hpp>
using namespace cat;

BigPseudoMersenne::BigPseudoMersenne(int regs, int bits, int C)
    : BigRTL(regs + PM_OVERHEAD, bits)
{
    pm_regs = regs + PM_OVERHEAD;
    modulus_c = C;

    // Reserve a register to contain the full modulus
    CachedModulus = Get(pm_regs - 1);
    CopyModulus(CachedModulus);
}

void BigPseudoMersenne::CopyModulus(Leg *out)
{
    // Set low leg to -C, set all bits on the rest
    out[0] = 0 - modulus_c;
    memset(&out[1], 0xFF, (library_legs-1) * sizeof(Leg));
}

// Strangely enough, including these all in the same source file improves performance
// in Visual Studio by almost 50%, which is odd because MSVC was one of the first
// compilers to support "link time optimization."

#include "mersenne/addsub/MrAdd.cpp"
#include "mersenne/addsub/MrNegate.cpp"
#include "mersenne/addsub/MrSubtract.cpp"
#include "mersenne/expm/MrInvert.cpp"
#include "mersenne/expm/MrSquareRoot.cpp"
#include "mersenne/mul/MrMultiply.cpp"
#include "mersenne/mul/MrMultiplyX.cpp"
#include "mersenne/mul/MrSquare.cpp"
#include "mersenne/reduce/MrReduce.cpp"
#include "mersenne/reduce/MrReduceProduct.cpp"
