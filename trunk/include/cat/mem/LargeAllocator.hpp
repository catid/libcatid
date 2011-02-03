/*
	Copyright (c) 2009-2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_LARGE_ALLOCATOR_HPP
#define CAT_LARGE_ALLOCATOR_HPP

#include <cat/mem/IAllocator.hpp>

#include <cstddef> // size_t
#include <vector> // std::_Construct and std::_Destroy

namespace cat {


// Large-size aligned heap allocator
class LargeAllocator : public IAllocator
{
public:
	CAT_INLINE virtual ~LargeAllocator() {}

	// Acquires memory aligned to a CPU cache-line byte boundary from the heap
    void *Acquire(u32 bytes);

	// Unable to resize
	void *Resize(void *ptr, u32 bytes) { return 0; }

    // Release an aligned pointer
    void Release(void *ptr);

	static LargeAllocator *ii;
};

// Use STLAlignedAllocator in place of the standard STL allocator
// to make use of the AlignedAllocator in STL types.
template<typename T>
class STLLargeAllocatorAllocator
{
public:
	typedef std::size_t size_type;
	typedef std::size_t difference_type;
	typedef T *pointer;
	typedef const T *const_pointer;
	typedef T &reference;
	typedef const T &const_reference;
	typedef T value_type;

	template<typename S>
	struct rebind
	{
		typedef STLLargeAllocatorAllocator<S> other;
	};

	pointer address(reference X) const
	{
		return &X;
	}

	const_pointer address(const_reference X) const
	{
		return &X;
	}

	STLLargeAllocatorAllocator() throw ()
	{
	}

	template<typename S>
	STLLargeAllocatorAllocator(const STLLargeAllocatorAllocator<S> &cp) throw ()
	{
	}

	template<typename S>
	STLLargeAllocatorAllocator<T> &operator=(const STLLargeAllocatorAllocator<S> &cp) throw ()
	{
		return *this;
	}

	pointer allocate(size_type Count, const void *Hint = 0)
	{
		return (pointer)LargeAllocator::Acquire((u32)Count * sizeof(T));
	}

	void deallocate(pointer Ptr, size_type Count)
	{
		LargeAllocator::Release(Ptr);
	}

	void construct(pointer Ptr, const T &Val)
	{
		std::_Construct(Ptr, Val);
	}

	void destroy(pointer Ptr)
	{
		std::_Destroy(Ptr);
	}

	size_type max_size() const
	{
		return 0x00FFFFFF;
	}

	template<typename S>
	bool operator==(STLLargeAllocatorAllocator <S> const &) const throw()
	{
		return true;
	}

	template<typename S>
	bool operator!=(STLLargeAllocatorAllocator <S> const &) const throw()
	{
		return false;
	}
};


} // namespace cat

#endif // CAT_LARGE_ALLOCATOR_HPP