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
using namespace cat;

BigRTL::BigRTL(int regs, int bits)
{
    library_legs = bits / (8 * sizeof(Leg));
    library_regs = regs + BIG_OVERHEAD;

    // Align library memory accesses to a 16-byte boundary
    library_memory = Aligned::New<Leg>(library_legs * library_regs);
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
