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

#include <cat/math/BitMath.hpp>
using namespace cat;

#if defined(CAT_ARCH_32)


//// Bit Scan Forward (BSF)

u32 BitMath::BSF32(u32 x)
{
#if defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK
    {
        BSF eax, [x]
    }

#endif
}


//// Bit Scan Reverse (BSR)

u32 BitMath::BSR32(u32 x)
{
#if defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK
    {
        BSR eax, [x]
    }

#endif
}


#endif // defined(CAT_ARCH_32)
