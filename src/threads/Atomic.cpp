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

#include <cat/threads/Atomic.hpp>
using namespace cat;

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#endif


//// Compare-and-Swap


#if defined(CAT_ARCH_64)


bool Atomic::CAS(volatile void *x, const void *expected_old_value, const void *new_value)
{
#if defined(CAT_COMPILER_MSVC)

    __int64 ComparandResult[2] = { ((u64*)expected_old_value)[0],
                                   ((u64*)expected_old_value)[1] };

    // Requires MSVC 2008 or newer
    return 1 == _InterlockedCompareExchange128((__int64*)x, ((u64*)new_value)[1],
                                               ((u64*)new_value)[0], ComparandResult);

#elif defined(CAT_ASM_INTEL)

    CAT_ASM
    {
		push ebx
        mov eax,new_value
		push esi
        mov ebx,[eax]
        mov ecx,[eax+4]
        mov edx,expected_old_value
        mov esi,x
        mov eax,[edx]
        mov edx,[edx+4]
        lock CMPXCHG8B [esi]
		pop ebx
        mov eax,0
		pop esi
        setz al
    }

#elif defined(CAT_ASM_ATT)

	u32 bit;
    CAT_ASM
    (
        "BSR %%rdx,%%rax"
		:"=a"(bit)
		:"d"(x)
	);
	return bit;

#endif
}


#else // !defined(CAT_ARCH_64)


bool Atomic::CAS(volatile void *x, const void *expected_old_value, const void *new_value)
{
#if defined(CAT_ASM_INTEL)

    CAT_ASM
    {
		push ebx
        mov eax,new_value
		push esi
        mov ebx,[eax]
        mov ecx,[eax+4]
        mov edx,expected_old_value
        mov esi,x
        mov eax,[edx]
        mov edx,[edx+4]
        lock CMPXCHG8B [esi]
		pop ebx
        mov eax,0
		pop esi
        setz al
    }

#elif defined(CAT_ASM_ATT)

	u32 success;
    CAT_ASM
    (
        "movl %%esi,new_value;
         movl %%ebx,[%%esi];
         movl %%ecx,[%%esi+4];
         movl %%edi,expected_old_value;
         movl %%esi,x;
         movl %%eax,[%%edi];
         movl %%edx,[%%edi+4];
         lock CMPXCHG8B [%%esi];
         movl %%eax,0;
         setz %%al"
		:"=a"(success)

        "BSF %%edx,%%eax"
		:"=a"(bit)
		:"d"(x)
	);
	return bit;

#endif
}


#endif // defined(CAT_ARCH_64)


//// Add y to x, returning the previous state of x
u32 Atomic::Add(volatile u32 *x, s32 y)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_ARCH_64)

    return _InterlockedAdd((volatile LONG*)x, y) - y;

#elif defined(CAT_ASM_INTEL)

    CAT_ASM
    {
        mov edx,x
        mov eax,y
        lock XADD [edx],eax
    }

#elif defined(CAT_ASM_ATT)

	u32 bit;
    CAT_ASM
    (
        "BSR %%rdx,%%rax"
		:"=a"(bit)
		:"d"(x)
	);
	return bit;

#endif
}


//// Set x to new value, returning the previous state of x
u32 Atomic::Set(volatile u32 *x, u32 new_value)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_ARCH_64)

    return _InterlockedExchange((volatile LONG*)x, new_value);

#elif defined(CAT_ASM_INTEL)

    CAT_ASM
    {
        mov edx,x
        mov eax,new_value
        lock XCHG [edx],eax
    }

#elif defined(CAT_ASM_ATT)

	u32 bit;
    CAT_ASM
    (
        "BSR %%rdx,%%rax"
		:"=a"(bit)
		:"d"(x)
	);
	return bit;

#endif
}


//// Bit Test and Set (BTS)

bool Atomic::BTS(volatile u32 *x, int bit)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_ARCH_64)

    return !!_interlockedbittestandset((volatile LONG*)x, bit);

#elif defined(CAT_ASM_INTEL)

    CAT_ASM
    {
        mov edx,x
        mov ecx,bit
        lock BTS [edx],ecx
        mov eax,0
        setc al
    }

#elif defined(CAT_ASM_ATT)

	u32 bit;
    CAT_ASM
    (
        "BSR %%rdx,%%rax"
		:"=a"(bit)
		:"d"(x)
	);
	return bit;

#endif
}


//// Bit Test and Reset (BTR)

bool Atomic::BTR(volatile u32 *x, int bit)
{
#if defined(CAT_COMPILER_MSVC) && defined(CAT_ARCH_64)

    return !!_interlockedbittestandreset((volatile LONG*)x, bit);

#elif defined(CAT_ASM_INTEL)

    CAT_ASM
    {
        mov edx,x
        mov ecx,bit
        lock BTR [edx],ecx
        mov eax,0
        setc al
    }

#elif defined(CAT_ASM_ATT)

	u32 bit;
    CAT_ASM
    (
        "BSR %%rdx,%%rax"
		:"=a"(bit)
		:"d"(x)
	);
	return bit;

#endif
}
