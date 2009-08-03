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

#include <cat/port/AlignedAlloc.hpp>
using namespace cat;


Aligned Aligned::ii;


static int CPU_CACHELINE_BYTES = 0;

static int DetermineCacheLineBytes()
{
#if defined(CAT_ASM_INTEL) && defined(CAT_ISA_X86)
	u32 cacheline = 0;
	CAT_ASM_BEGIN
		push ebx
		xor ecx, ecx
next:	mov eax, 4
		cpuid
		test eax, 31
		jz done
		or [cacheline], ebx
		lea ecx, [ecx+1]
		jmp next
done:	pop ebx
	CAT_ASM_END

	return (cacheline & 4095) + 1;
#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)
	u32 cacheline = 0;
	CAT_ASM_BEGIN
		"xorl %%ecx, %%ecx\n\t"
		"next: movl $4, %%eax\n\t"
		"cpuid\n\t"
		"testl $31, %%eax\n\t"
		"jz done\n\t"
		"orl %%ebx, %0\n\t"
		"leal 1(%%ecx), %%ecx\n\t"
		"jmp next\n\t"
		"done: ;"
		: "=r" (cacheline)
		: /* no inputs */
		: "cc", "%ebx"
	CAT_ASM_END

	return (cacheline & 4095) + 1;
#else
	return 64;
#endif
}

// Allocates memory aligned to a CPU cache-line byte boundary from the heap
void *Aligned::Acquire(int bytes)
{
	if (!CPU_CACHELINE_BYTES)
		CPU_CACHELINE_BYTES = DetermineCacheLineBytes();

    u8 *buffer = new u8[CPU_CACHELINE_BYTES + bytes];
    if (!buffer) return 0;

#if defined(CAT_WORD_64)
    u8 offset = CPU_CACHELINE_BYTES - ((u8)*(u64*)&buffer & (CPU_CACHELINE_BYTES-1));
#else
    u8 offset = CPU_CACHELINE_BYTES - ((u8)*(u32*)&buffer & (CPU_CACHELINE_BYTES-1));
#endif

    buffer += offset;
    buffer[-1] = (u8)offset;

    return buffer;
}

// Frees an aligned pointer
void Aligned::Release(void *ptr)
{
    if (ptr)
    {
        u8 *buffer = (u8 *)ptr;

        buffer -= buffer[-1];

        delete []buffer;
    }
}
