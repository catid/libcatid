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

// 06/23/09 began

#ifndef CAT_ATOMIC_HPP
#define CAT_ATOMIC_HPP

#include <cat/Platform.hpp>

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#endif

namespace cat {


namespace Atomic {


// Compare-and-Swap (CAS)
// On 32-bit architectures, the arguments point to 64-bit values
// On 64-bit architectures, the arguments point to 128-bit values
// Returns true if the old value was equal to the expected value
CAT_INLINE bool CAS(volatile void *x, const void *expected_old_value, const void *new_value);

// Add y to x, returning the previous state of x
CAT_INLINE u32 Add(volatile u32 *x, s32 y);

// Set x to new value, returning the previous state of x
CAT_INLINE u32 Set(volatile u32 *x, u32 new_value);

// Bit Test and Set (BTS)
// Returns true if the bit was 1 and is still 1, otherwise false
CAT_INLINE bool BTS(volatile u32 *x, int bit);

// Bit Test and Reset (BTR)
// Returns true if the bit was 1 and is now 0, otherwise false
CAT_INLINE bool BTR(volatile u32 *x, int bit);


} // namespace Atomic


//// Compare-and-Swap

#if defined(CAT_WORD_64)


bool Atomic::CAS(volatile void *x, const void *expected_old_value, const void *new_value)
{
#if defined(CAT_COMPILER_MSVC)

    __int64 ComparandResult[2] = { ((u64*)expected_old_value)[0],
                                   ((u64*)expected_old_value)[1] };

    // Requires MSVC 2008 or newer
    return 1 == _InterlockedCompareExchange128((__int64*)x, ((u64*)new_value)[1],
                                               ((u64*)new_value)[0], ComparandResult);

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

	u128 *target = (u128*)x;
	u64 *replace = (u64*)new_value;
	u128 *expected = (u128*)expected_old_value;
	bool retval;

    CAT_ASM_BEGIN
		"lock; CMPXCHG16B %0\n\t"
		"sete %%al"
		: "=m" (*target), "=a" (retval)
		: "m" (*target), "b" (replace[0]), "c" (replace[1]), "A" (*expected)
		: "memory", "cc"
    CAT_ASM_END

    return retval;

#else

#error "Missing implementation for your architecture"

#endif
}


#else // 32-bit version:


bool Atomic::CAS(volatile void *x, const void *expected_old_value, const void *new_value)
{
#if defined(CAT_ASM_INTEL) && defined(CAT_ISA_X86)

    CAT_ASM_BEGIN
		push ebx
        mov eax, new_value
		push esi
        mov ebx, dword ptr[eax]
        mov ecx, dword ptr[eax+4]
        mov edx, expected_old_value
        mov esi, x
        mov eax, dword ptr[edx]
        mov edx, dword ptr[edx+4]
        lock CMPXCHG8B qword ptr[esi]
		pop ebx
        mov eax, 0
		pop esi
        setz al
    CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

	u64 *target = (u64*)x;
	u32 *replace = (u32*)new_value;
	u64 *expected = (u64*)expected_old_value;
	bool retval;

    CAT_ASM_BEGIN
		"lock; CMPXCHG8B %0\n\t"
		"sete %%al"
		: "=m" (*target), "=a" (retval)
		: "m" (*target), "b" (replace[0]), "c" (replace[1]), "A" (*expected)
		: "memory", "cc"
    CAT_ASM_END

    return retval;

#else

#error "Missing implementation for your architecture"

#endif
}


#endif // defined(CAT_WORD_64)


//// Add y to x, returning the previous state of x

u32 Atomic::Add(volatile u32 *x, s32 y)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64)

    return _InterlockedAdd((volatile LONG*)x, y) - y;

#elif defined(CAT_ASM_INTEL) && defined(CAT_WORD_32) && defined(CAT_ISA_X86)

    CAT_ASM_BEGIN
        mov edx,x
        mov eax,y
        lock XADD [edx],eax
    CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

	u32 retval;

    CAT_ASM_BEGIN
		"lock; XADDl %%eax, %0\n\t"
		: "=m" (*x), "=a" (retval)
		: "m" (*x), "a" (y)
		: "memory", "cc"
    CAT_ASM_END

    return retval;

#else

#error "Missing implementation for your architecture"

#endif
}


//// Set x to new value, returning the previous state of x

u32 Atomic::Set(volatile u32 *x, u32 new_value)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64)

    return _InterlockedExchange((volatile LONG*)x, new_value);

#elif defined(CAT_ASM_INTEL) && defined(CAT_WORD_32) && defined(CAT_ISA_X86)

    CAT_ASM_BEGIN
        mov edx,x
        mov eax,new_value
        lock XCHG [edx],eax
    CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

	u32 retval;

    CAT_ASM_BEGIN
		"lock; XCHGl %%eax, %0\n\t"
		: "=m" (*x), "=a" (retval)
		: "m" (*x), "a" (new_value)
		: "memory", "cc"
    CAT_ASM_END

    return retval;

#else

#error "Missing implementation for your architecture"

#endif
}


//// Bit Test and Set (BTS)

bool Atomic::BTS(volatile u32 *x, int bit)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64)

    return !!_interlockedbittestandset((volatile LONG*)x, bit);

#elif defined(CAT_ASM_INTEL) && defined(CAT_WORD_32) && defined(CAT_ISA_X86)

    CAT_ASM_BEGIN
        mov edx,x
        mov ecx,bit
        lock BTS [edx],ecx
        mov eax,0
        setc al
    CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

	bool retval;

    CAT_ASM_BEGIN
		"lock; BTSl %2, %0\n\t"
		"setc %%al"
		: "=m" (*x), "=a" (retval)
		: "Ir" (bit)
		: "memory", "cc"
    CAT_ASM_END

    return retval;

#else

#error "Missing implementation for your architecture"

#endif
}


//// Bit Test and Reset (BTR)

bool Atomic::BTR(volatile u32 *x, int bit)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64)

    return !!_interlockedbittestandreset((volatile LONG*)x, bit);

#elif defined(CAT_ASM_INTEL) && defined(CAT_WORD_32) && defined(CAT_ISA_X86)

    CAT_ASM_BEGIN
        mov edx,x
        mov ecx,bit
        lock BTR [edx],ecx
        mov eax,0
        setc al
    CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)

	bool retval;

    CAT_ASM_BEGIN
		"lock; BTRl %2, %0\n\t"
		"setc %%al"
		: "=m" (*x), "=a" (retval)
		: "Ir" (bit)
		: "memory", "cc"
    CAT_ASM_END

    return retval;

#else

#error "Missing implementation for your architecture"

#endif
}



} // namespace cat

#endif // CAT_ATOMIC_HPP
