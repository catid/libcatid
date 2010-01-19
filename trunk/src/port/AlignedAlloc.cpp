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
    u32 offset = CPU_CACHELINE_BYTES - ((u32)*(u64*)&buffer & (CPU_CACHELINE_BYTES-1));
#else
    u32 offset = CPU_CACHELINE_BYTES - (*(u32*)&buffer & (CPU_CACHELINE_BYTES-1));
#endif

    buffer += offset;
    buffer[-1] = static_cast<u8>( offset );

    return buffer;
}

// Frees an aligned pointer
void Aligned::Release(void *ptr)
{
    if (ptr)
    {
        u8 *buffer = reinterpret_cast<u8*>( ptr );

        buffer -= buffer[-1];

        delete []buffer;
    }
}
