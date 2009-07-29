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

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#endif


u32 BitMath::BSF32(u32 x)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_ARCH_64) && !defined(CAT_DEBUG)

	u32 index;
    _BitScanForward((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_INTEL)

    CAT_ASM_BEGIN
        BSF eax, [x]
    CAT_ASM_END

#elif defined(CAT_ASM_ATT)

    CAT_ASM_BEGIN
		"BSF %[x],%eax"
		:[x]"mr"(x)
    CAT_ASM_END

#else

	return BSR32(x ^ (x - 1));

#endif
}


u32 BitMath::BSR32(u32 x)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_ARCH_64) && !defined(CAT_DEBUG)

	u32 index;
    _BitScanReverse((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_INTEL)

    CAT_ASM_BEGIN
        BSR eax, [x]
    CAT_ASM_END

#elif defined(CAT_ASM_ATT)

    CAT_ASM_BEGIN
		"BSR %[x],%eax"
		:[x]"mr"(x)
    CAT_ASM_END

#else

	// Adapted from the Stanford Bit Twiddling Hacks collection
    register u32 shift, r;

    r = (x > 0xFFFF) << 4; x >>= r;
    shift = (x > 0xFF) << 3; x >>= shift; r |= shift;
    shift = (x > 0xF) << 2; x >>= shift; r |= shift;
    shift = (x > 0x3) << 1; x >>= shift; r |= shift;
    r |= (x >> 1);
    return r;

#endif
}


u32 BitMath::BSF64(u64 x)
{
#if defined(CAT_COMPILER_MSVC) && !defined(CAT_DEBUG) && defined(CAT_ARCH_64)

	u32 index;
    _BitScanForward64((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_ATT) && defined(CAT_ARCH_64)

    CAT_ASM_BEGIN
		"BSF %[x],%rax"
		:[x]"mr"(x)
    CAT_ASM_END

#else

	return BSR64(x ^ (x - 1));

#endif
}


u32 BitMath::BSR64(u64 x)
{
#if defined(CAT_COMPILER_MSVC) && !defined(CAT_DEBUG) && defined(CAT_ARCH_64)

	u32 index;
    _BitScanReverse64((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_ATT) && defined(CAT_ARCH_64)

    CAT_ASM_BEGIN
		"BSR %[x],%rax"
		:[x]"mr"(x)
    CAT_ASM_END

#else

	// Adapted from the Stanford Bit Twiddling Hacks collection
    register u32 shift, r;

    r = (x > 0xFFFFFFFF) << 5; x >>= r;
    shift = (x > 0xFFFF) << 4; x >>= shift; r |= shift;
    shift = (x > 0xFF) << 3; x >>= shift; r |= shift;
    shift = (x > 0xF) << 2; x >>= shift; r |= shift;
    shift = (x > 0x3) << 1; x >>= shift; r |= shift;
    r |= (u32)(x >> 1);
    return r;

#endif
}
