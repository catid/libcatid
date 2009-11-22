/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

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
