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

#if defined(CAT_ARCH_32)


//// Compare-and-Swap

bool Atomic::CAS(volatile void *x, const void *expected_old_value, const void *new_value)
{
#if defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK
    {
        mov esi,new_value
        mov ebx,[esi]
        mov ecx,[esi+4]
        mov edi,expected_old_value
        mov esi,x
        mov eax,[edi]
        mov edx,[edi+4]
        lock CMPXCHG8B [esi]
        mov eax,0
        setz al
    }

#endif
}


//// Add y to x, returning the previous state of x
u32 Atomic::Add(volatile u32 *x, s32 y)
{
#if defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK
    {
        mov esi,x
        mov eax,y
        lock XADD [esi],eax
    }

#endif
}


//// Set x to new value, returning the previous state of x
u32 Atomic::Set(volatile u32 *x, u32 new_value)
{
#if defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK
    {
        mov esi,x
        mov eax,new_value
        lock XCHG [esi],eax
    }

#endif
}


//// Bit Test and Set (BTS)

bool Atomic::BTS(volatile u32 *x, int bit)
{
#if defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK
    {
        mov esi,x
        mov ebx,bit
        lock BTS [esi],ebx
        mov eax,0
        setc al
    }

#endif
}


//// Bit Test and Reset (BTR)

bool Atomic::BTR(volatile u32 *x, int bit)
{
#if defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK
    {
        mov esi,x
        mov ebx,bit
        lock BTR [esi],ebx
        mov eax,0
        setc al
    }

#endif
}


#endif // defined(CAT_ARCH_32)
