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

#ifndef CAT_NET_SEND_BUFFER_HPP
#define CAT_NET_SEND_BUFFER_HPP

#include <cat/io/IOLayer.hpp>
#include <cat/mem/StdAllocator.hpp>

namespace cat {


static const u32 SEND_BUFFER_PREALLOCATION = 200;

// A buffer specialized for writing to a socket
struct SendBuffer : public BatchHead
{
	// Shared data
	u32 data_bytes;

	union
	{
		// IO layer specific overhead pimpl
		IOLayerSendOverhead iointernal;

		// Worker data
		u32 allocated_bytes;
	};

	static CAT_INLINE u8 *Acquire(u32 trailing_bytes)
	{
		u32 allocated = trailing_bytes;
		if (allocated < SEND_BUFFER_PREALLOCATION)
			allocated = SEND_BUFFER_PREALLOCATION;

		SendBuffer *buffer = StdAllocator::ii->AcquireTrailing<SendBuffer>(allocated);
		if (!buffer) return 0;

		//buffer->allocated_bytes = trailing_bytes;
		buffer->allocated_bytes = allocated;
		return GetTrailingBytes(buffer);
	}

	static CAT_INLINE SendBuffer *Promote(u8 *ptr)
	{
		return reinterpret_cast<SendBuffer*>( ptr - sizeof(SendBuffer) );
	}

	static CAT_INLINE u8 *Resize(SendBuffer *buffer, u32 new_trailing_bytes)
	{
		if (!buffer) return Acquire(new_trailing_bytes);

		if (new_trailing_bytes <= buffer->allocated_bytes)
			return GetTrailingBytes(buffer);

		buffer = StdAllocator::ii->ResizeTrailing(buffer, new_trailing_bytes);
		if (!buffer) return 0;

		buffer->allocated_bytes = new_trailing_bytes;

		return GetTrailingBytes(buffer);
	}

	static CAT_INLINE u8 *Resize(u8 *ptr, u32 new_trailing_bytes)
	{
		if (!ptr) return Acquire(new_trailing_bytes);
		SendBuffer *buffer = Promote(ptr);

		if (new_trailing_bytes <= buffer->allocated_bytes)
			return ptr;

		buffer = StdAllocator::ii->ResizeTrailing(buffer, new_trailing_bytes);
		if (!buffer) return 0;

		buffer->allocated_bytes = new_trailing_bytes;

		return GetTrailingBytes(buffer);
	}

	static CAT_INLINE void Shrink(u8 *ptr, u32 new_trailing_bytes)
	{
		Promote(ptr)->data_bytes = new_trailing_bytes;
	}

	CAT_INLINE void Release()
	{
		StdAllocator::ii->Release(this);
	}

	static CAT_INLINE void Release(SendBuffer *buffer)
	{
		StdAllocator::ii->Release(buffer);
	}

	static CAT_INLINE void Release(u8 *ptr)
	{
		if (ptr) SendBuffer::Promote(ptr)->Release();
	}
};


} // namespace cat

#endif // CAT_NET_SEND_BUFFER_HPP
