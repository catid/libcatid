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

// 07/21/09 moved them back
// 06/23/09 began, moved macros from Platform.hpp

#ifndef CAT_BITMATH_HPP
#define CAT_BITMATH_HPP

#include <cat/Platform.hpp>

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#endif

namespace cat {


namespace BitMath {


// Bit Scan Forward (BSF)
// Scans from bit 0 to MSB
// Undefined when input is zero
CAT_INLINE u32 BSF32(u32 x);
CAT_INLINE u32 BSF64(u64 x);

// Bit Scan Reverse (BSR)
// Scans from MSB to bit 0
// Undefined when input is zero
CAT_INLINE u32 BSR32(u32 x);
CAT_INLINE u32 BSR64(u64 x);


} // namespace BitMath


u32 BitMath::BSF32(u32 x)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64) && !defined(CAT_DEBUG)

	u32 index;
    _BitScanForward((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_INTEL)

    CAT_ASM_BEGIN
        BSF eax, [x]
    CAT_ASM_END

#elif defined(CAT_ASM_ATT)

	u32 retval;

    CAT_ASM_BEGIN
		"BSFl %1, %%eax"
		: "=a" (retval)
		: "r" (x)
		: "cc"
    CAT_ASM_END

    return retval;

#else

	return BSR32(x ^ (x - 1));

#endif
}


u32 BitMath::BSR32(u32 x)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64) && !defined(CAT_DEBUG)

	u32 index;
    _BitScanReverse((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_INTEL)

    CAT_ASM_BEGIN
        BSR eax, [x]
    CAT_ASM_END

#elif defined(CAT_ASM_ATT)

	u32 retval;

    CAT_ASM_BEGIN
		"BSRl %1, %%eax"
		: "=a" (retval)
		: "r" (x)
		: "cc"
    CAT_ASM_END

    return retval;

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
#if defined(CAT_COMPILER_MSVC) && !defined(CAT_DEBUG) && defined(CAT_WORD_64)

	u32 index;
    _BitScanForward64((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_ATT) && defined(CAT_WORD_64)

	u32 retval;

    CAT_ASM_BEGIN
		"BSFq %1, %%rax"
		: "=a" (retval)
		: "r" (x)
		: "cc"
    CAT_ASM_END

    return retval;

#else

	return BSR64(x ^ (x - 1));

#endif
}


u32 BitMath::BSR64(u64 x)
{
#if defined(CAT_COMPILER_MSVC) && !defined(CAT_DEBUG) && defined(CAT_WORD_64)

	u32 index;
    _BitScanReverse64((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_ATT) && defined(CAT_WORD_64)

	u32 retval;

    CAT_ASM_BEGIN
		"BSRq %1, %%rax"
		: "=a" (retval)
		: "r" (x)
		: "cc"
    CAT_ASM_END

    return retval;

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


} // namespace cat

#endif // CAT_BITMATH_HPP
