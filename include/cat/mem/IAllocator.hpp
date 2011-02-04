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

#ifndef CAT_I_ALLOCATOR_HPP
#define CAT_I_ALLOCATOR_HPP

#include <cat/Platform.hpp>

namespace cat {


struct BatchHead;
struct BatchSet;
class IAllocator;


// For batch allocations, this is the header attached to each one.  This header
// allows for the batched objects to be passed around with a BatchSet (below).
// Normal allocations do not use this header.
struct BatchHead
{
	BatchHead *batch_next;
};

// When passing around a batch of allocated space, use this object to represent
// the two ends of the batch for O(1) concatenation to other batches
struct BatchSet
{
	BatchHead *head, *tail;
};


// Allocator interface
class IAllocator
{
public:
	CAT_INLINE virtual ~IAllocator() {}

	// Returns true if allocator backing store was successfully initialized
	virtual bool Valid() { return true; }

	// Returns 0 on failure
	// May acquire more bytes than requested
    virtual void *Acquire(u32 bytes) = 0;

	// Returns 0 on failure
	// Resizes the given buffer to a new number of bytes
	virtual void *Resize(void *ptr, u32 bytes) = 0;

	// Release a buffer
	// Should not die if pointer is null
	virtual void Release(void *ptr) = 0;

	// Attempt to acquire a number of buffers
	// Returns the number of valid buffers it was able to allocate
	virtual u32 AcquireBatch(BatchSet &set, u32 count, u32 bytes);

	// Attempt to acquire a number of buffers
	// Returns the number of valid buffers it was able to allocate
	virtual void ReleaseBatch(const BatchSet &batch);

	// Delete an object calling the destructor and then freeing memory
    template<class T>
    CAT_INLINE void Delete(T *ptr)
    {
		if (ptr)
		{
			ptr->~T();
			Release(ptr);
		}
    }
};


// Provide placement new constructor and delete pair to allow for
// an easy syntax to create objects:
//   T *a = new (buffer_allocator) T();
// The object can be freed with:
//   buffer_allocator->Delete(a);
// Which insures that the destructor is called before freeing memory
CAT_INLINE void *operator new[](std::size_t bytes, cat::IAllocator *alloc) throw()
{
	return alloc->Acquire((u32)bytes);
}

// Placement "delete": Does not call destructor
CAT_INLINE void operator delete(void *ptr, cat::IAllocator *alloc) throw()
{
	alloc->Release(ptr);
}

} // namespace cat

#endif // CAT_I_ALLOCATOR_HPP
