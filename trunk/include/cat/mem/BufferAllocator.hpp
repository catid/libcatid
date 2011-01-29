/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_BUFFER_ALLOCATOR_HPP
#define CAT_BUFFER_ALLOCATOR_HPP

#include <cat/threads/Mutex.hpp>

#include <cstddef> // size_t
#include <vector> // std::_Construct and std::_Destroy

namespace cat {


/*
	The buffer allocator is optimized for allocating memory space of a
	prescribed size that need to be aligned to the cache line size.

	It preallocates a number of buffers and tries to allocate from this
	set.  If it runs out of space, it will return zero.

	Allocation and deallocation are thread-safe.  It is optimized to
	be used for allocating in one thread and deallocating in another,
	since it uses two locks and only causes contention if the allocator
	runs out of space and needs to lazily move all the freed buffers
	into the acquire list.  In any case, the lock time is minimized. 
*/

// Aligned buffer array heap allocator
class BufferAllocator : public IAllocator
{
	struct BufferTail
	{
		BufferTail *next;
	};

	u32 _buffer_bytes, _buffer_count;
	u8 *_buffers;

	Mutex _acquire_lock;
	BufferTail *_acquire_head;

	Mutex _release_lock;
	BufferTail *_release_head;

public:
	// Specify the number of bytes needed per buffer, which
	// will be bumped up to the next CPU cache line size, and
	// the number of buffers to preallocate
	BufferAllocator(u32 buffer_min_size, u32 buffer_count);
	virtual ~BufferAllocator();

	CAT_INLINE bool Valid() { return _buffers != 0; }

	// Acquires buffer aligned to a CPU cache-line byte boundary from the heap
	// Returns 0 if out of memory
    void *Acquire(u32 bytes);

	// Unable to resize
	void *Resize(void *ptr, u32 bytes) { return 0; }

	// Release a buffer pointer
	void Release(void *buffer);

	// Attempt to acquire a number of buffers
	// Returns the number of valid buffers it was able to allocate
	u32 AcquireMultiple(void **buffers, u32 count, u32 bytes);

	// Release a number of buffers simultaneously
	void ReleaseMultiple(void **buffers, u32 count);
};


} // namespace cat


#endif // CAT_BUFFER_ALLOCATOR_HPP
