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
#include <cat/port/AlignedAlloc.hpp>
#include <cstring>
using namespace cat;

BigRTL::BigRTL(int regs, int bits)
{
    library_legs = bits / (8 * sizeof(Leg));
    library_regs = regs + BIG_OVERHEAD;

    // Align library memory accesses to a 16-byte boundary
	library_memory = new (Aligned::ii) Leg[library_legs * library_regs];
}

BigRTL::~BigRTL()
{
    if (library_memory)
    {
        // Clear and free memory for registers
        memset(library_memory, 0, library_legs * library_regs * sizeof(Leg));
        Aligned::Delete(library_memory);
    }
}

Leg *BigRTL::Get(int reg_index)
{
    return &library_memory[library_legs * reg_index];
}

void BigRTL::Copy(const Leg *in, Leg *out)
{
    memcpy(out, in, library_legs * sizeof(Leg));
}

void BigRTL::CopyX(Leg in, Leg *out)
{
    // Set low leg to input, zero the rest
    out[0] = in;
    memset(&out[1], 0, (library_legs-1) * sizeof(Leg));
}

int BigRTL::LegsUsed(const Leg *in)
{
    for (int legs = library_legs - 1; legs >= 0; --legs)
        if (in[legs]) return legs + 1;

    return 0;
}

// Strangely enough, including these all in the same source file improves performance
// in Visual Studio by almost 50%, which is odd because MSVC was one of the first
// compilers to support "link time optimization."

#include "rtl/io/Load.cpp"
#include "rtl/io/LoadString.cpp"
#include "rtl/io/Save.cpp"
#include "rtl/addsub/Add.cpp"
#include "rtl/addsub/Add1.cpp"
#include "rtl/addsub/AddX.cpp"
#include "rtl/addsub/Compare.cpp"
#include "rtl/addsub/Double.cpp"
#include "rtl/addsub/DoubleAdd.cpp"
#include "rtl/addsub/Negate.cpp"
#include "rtl/addsub/Shift.cpp"
#include "rtl/addsub/Subtract.cpp"
#include "rtl/addsub/SubtractX.cpp"
#include "rtl/mul/Square.cpp"
#include "rtl/mul/Multiply.cpp"
#include "rtl/mul/MultiplyX.cpp"
#include "rtl/mul/MultiplyXAdd.cpp"
#include "rtl/div/Divide.cpp"
#include "rtl/div/DivideAsm64.cpp"
#include "rtl/div/DivideGeneric.cpp"
#include "rtl/div/ModularInverse.cpp"
