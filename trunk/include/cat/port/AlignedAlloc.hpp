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

#ifndef CAT_ALIGNED_ALLOC_HPP
#define CAT_ALIGNED_ALLOC_HPP

#include <cat/Platform.hpp>

namespace cat {


// Small to medium -size aligned heap allocator
class Aligned
{
public:
	CAT_INLINE Aligned() {}

	// Acquires memory aligned to a CPU cache-line byte boundary from the heap
    static void *Acquire(u32 bytes);

	// Resizes an aligned pointer
	static void *Resize(void *ptr, u32 bytes);

    // Release an aligned pointer
    static void Release(void *ptr);

    template<class T>
    static inline void Delete(T *ptr)
    {
        ptr->~T();
        Release(ptr);
    }

	static Aligned ii;
};


// Large-size aligned heap allocator
class LargeAligned
{
public:
	// Acquires memory aligned to a CPU cache-line byte boundary from the heap
    static void *Acquire(u32 bytes);

    // Release an aligned pointer
    static void Release(void *ptr);
};


u32 GetCacheLineBytes();


} // namespace cat

#include <cstddef>

// Provide placement new constructor and delete pair to allow for
// an easy syntax to create objects from the RegionAllocator:
//   T *a = new (Aligned()) T();
// The object can be freed with:
//   Aligned::Delete(a);
// Which insures that the destructor is called before freeing memory
CAT_INLINE void *operator new[](std::size_t bytes, cat::Aligned &) throw()
{
	return cat::Aligned::Acquire((int)bytes);
}

// Placement "delete": Does not call destructor
CAT_INLINE void operator delete(void *ptr, cat::Aligned &) throw()
{
	cat::Aligned::Release(ptr);
}

#endif // CAT_ENDIAN_NEUTRAL_HPP
