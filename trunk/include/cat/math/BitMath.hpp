/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

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
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


// Next highest power of two (e.g. 13 -> 16)
CAT_INLINE u32 NextHighestPow2(u32 n)
{
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	return n + 1;
}
CAT_INLINE u64 NextHighestPow2(u64 n)
{
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;
	return n + 1;
}

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

// Returns the count of bits set in the input for types up to 128 bits
template<typename T> CAT_INLINE T BitCount(T v)
{
	// From Stanford Bit Twiddling Hacks collection
	// http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
	v = v - ((v >> 1) & (T)~(T)0/3);
	v = (v & (T)~(T)0/15*3) + ((v >> 2) & (T)~(T)0/15*3);
	v = (v + (v >> 4)) & (T)~(T)0/255*15;
	return (T)(v * ((T)~(T)0/255)) >> ((sizeof(v) - 1) * 8);
}

/*
	Reconstruct a 32-bit or 64-bit counter that increments by one each time,
	given a truncated sample of its low bits, and the last accepted value of the
	counter.  This supports roll-over of the counter and in the partial bits.

	Normally this function centers allowable distance from the last accepted
	value of the counter, so that it has equal ability to decode counter values
	ahead and behind the last accepted value.

	There are some other practical ways to center the allowable distance.

	In the case of synchronized timestamps between two hosts on a network, the
	allowable distance in the counter should not be measured from the current
	time since it is almost guaranteed to be in the past.
	If synchronization is off in a long RTT network path, then forward error may
	be possible and not insignificant.  This would be a way to detect timestamp
	synchronization error.

	To center allowable distance around values other than the current counter
	value, simply subtract a bias.  Example:

		We want to represent a timestamp in the past, indicating when a packet was
		transmitted in milliseconds.  The netcode would disconnect a user after 15
		seconds of silence, so only 15000 counter values are strictly required to
		represent how long ago something happened.
	
		With 14 bits, we can represent 0 to 16383 milliseconds in the past.
		But since time synchronization isn't perfect, there may be error either
		forwards or backwards in time.  I would prefer to bias the allowable error
		to the future since most packets won't really take 15 seconds to arrive.
		So, let's allow for 1000 milliseconds error in the future and 384 error over
		15 seconds ago.

		So the idea is to
		| 384 | 15000 | 1000 |
		          ^ place center point around here
		|<--------|--------->|  to catch timestamps in this area.
		              ^ This is now.
		          ^ So subtract off 15000/2 + (1000 - 384)/2 to get to the offset point.

		The reconstruction supports counter roll-over so this can go negative and
		there will be no trouble.

	Code:

		// Remote host has transmitted a partial_timestamp, which attempts to be
		// synchronized with local time.  We will now reconstruct the full_timestamp:
		u32 time_now = Clock::msec();
		u32 full_timestamp = ReconstructCounter<14>(time_now - (15000 + 1000 - 384) / 2, partial_timestamp);
*/
template<int BITS, typename T> CAT_INLINE T ReconstructCounter(T center_count, u32 partial_low_bits)
{
	const u32 IV_MSB = (1 << BITS); // BITS < 32
	const u32 IV_MASK = (IV_MSB - 1);

	s32 diff = partial_low_bits - (u32)(center_count & IV_MASK);
	return ((center_count & ~(T)IV_MASK) | partial_low_bits)
		- (((IV_MSB >> 1) - (diff & IV_MASK)) & IV_MSB)
		+ (diff & IV_MSB);
}


u32 BSF32(u32 x)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64) && !defined(CAT_DEBUG)

	u32 index;
    _BitScanForward((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_INTEL) && defined(CAT_ISA_X86)

    CAT_ASM_BEGIN
        BSF eax, [x]
    CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

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


u32 BSR32(u32 x)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64) && !defined(CAT_DEBUG)

	u32 index;
    _BitScanReverse((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_INTEL) && defined(CAT_ISA_X86)

    CAT_ASM_BEGIN
        BSR eax, [x]
    CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

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


u32 BSF64(u64 x)
{
#if defined(CAT_COMPILER_MSVC) && !defined(CAT_DEBUG) && defined(CAT_WORD_64)

	u32 index;
    _BitScanForward64((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_ATT) && defined(CAT_WORD_64) && defined(CAT_ISA_X86)

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


u32 BSR64(u64 x)
{
#if defined(CAT_COMPILER_MSVC) && !defined(CAT_DEBUG) && defined(CAT_WORD_64)

	u32 index;
    _BitScanReverse64((unsigned long*)&index, x);
    return index;

#elif defined(CAT_ASM_ATT) && defined(CAT_WORD_64) && defined(CAT_ISA_X86)

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
