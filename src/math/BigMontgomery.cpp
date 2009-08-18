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

#include <cat/math/BigMontgomery.hpp>
#include <cstring>
using namespace cat;

BigMontgomery::BigMontgomery(int regs, int bits)
    : BigRTL(regs + MON_OVERHEAD, bits)
{
    mon_regs = regs + MON_OVERHEAD;

    // Reserve a register to contain the full modulus
    CachedModulus = Get(mon_regs - 1);
	TempProduct = Get(mon_regs - 3);
	TempProductHi = TempProduct + library_legs;
}

void BigMontgomery::SetModulus(const Leg *mod)
{
	Copy(mod, CachedModulus);
	mod_inv = MultiplicativeInverseX(-(LegSigned)mod[0]);
}

void BigMontgomery::CopyModulus(Leg *out)
{
	Copy(CachedModulus, out);
}

// Strangely enough, including these all in the same source file improves performance
// in Visual Studio by almost 50%, which is odd because MSVC was one of the first
// compilers to support "link time optimization."

#include "montgomery/reduce/MonInput.cpp"
#include "montgomery/reduce/MonReduceProduct.cpp"
#include "montgomery/addsub/MonAdd.cpp"
#include "montgomery/addsub/MonNegate.cpp"
#include "montgomery/addsub/MonSubtract.cpp"
#include "montgomery/mul/MonMultiply.cpp"
#include "montgomery/mul/MonSquare.cpp"
