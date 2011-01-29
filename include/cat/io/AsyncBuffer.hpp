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

#ifndef CAT_ASYNC_BUFFER_HPP
#define CAT_ASYNC_BUFFER_HPP

#include <cat/Platform.hpp>
#include <cat/port/FastDelegate.h>

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


class AsyncBuffer;


// bool AsyncCallback(int error, AsyncBuffer *buffer, u32 bytes);
typedef fastdelegate::FastDelegate4<int, AsyncBuffer *, u32, bool> AsyncCallback;


// Overlapped base object
#if defined(CAT_OS_WINDOWS)
	typedef OVERLAPPED AsyncOv;
#else
#error "TODO"
#endif


/*
	AsyncBuffer: Utility object representing the buffers for a single I/O operation.

	This is flexible enough to represent network and file IO buffers.
*/
class AsyncBuffer
{
private:
	AsyncOv _ov;
	AsyncCallback _callback;
	u8 *_data;
	u32 _data_bytes, _tag_bytes;
	u8 _tag[1];

public:
	// Reset AsyncOv and set offset and callback
	CAT_INLINE void Reset(const AsyncCallback &callback, u64 offset = 0)
	{
#if defined(CAT_OS_WINDOWS)
		_ov.hEvent = 0;
		_ov.Internal = 0;
		_ov.InternalHigh = 0;
		_ov.OffsetHigh = (u32)(offset >> 32);
		_ov.Offset = (u32)offset;
#endif
		_callback = callback;
	}

public:
	static CAT_INLINE u32 OVERHEAD() { return (u32)(offsetof(AsyncBuffer, _tag)); }

	CAT_INLINE AsyncOv *GetOv() { return &_ov; }
	CAT_INLINE u64 GetOffset() { return ((u64)_ov.OffsetHigh << 32) | _ov.Offset; }

	CAT_INLINE bool Call(int error, AsyncBuffer *buffer, u32 bytes)
	{
		if (!_callback) return true;
		return _callback(error, buffer, bytes);
	}

	CAT_INLINE void Zero()
	{
		CAT_CLR(_data, _data_bytes);
	}

public:
	CAT_INLINE u32 GetTagBytes() { return _tag_bytes; }

	CAT_INLINE u8 *GetTagData() { return _tag; }

	template<class T>
	CAT_INLINE T *GetTag() { return reinterpret_cast<T*>( _tag ); }

	template<class T>
	CAT_INLINE T *GetTag(T * &ptr) { return (ptr = reinterpret_cast<T*>( _tag )); }

public:
	CAT_INLINE u32 GetDataBytes() { return _data_bytes; }

	CAT_INLINE u8 *GetData() { return reinterpret_cast<u8*>( _data ); }

	template<class T>
	CAT_INLINE T *GetData() { return reinterpret_cast<T*>( _data ); }

	template<class T>
	CAT_INLINE T *GetData(T * &ptr) { return (ptr = reinterpret_cast<T*>( _data )); }

public:
	// Only works on Acquired() buffers, not Wrap()ed buffers
	static CAT_INLINE AsyncBuffer *Promote(void *vdata)
	{
		u8 *data = reinterpret_cast<u8*>( vdata );
		const u32 OVERHEAD_BYTES = (u32)(offsetof(AsyncBuffer, _tag));

		if (!data) return 0;
		return reinterpret_cast<AsyncBuffer*>( data - OVERHEAD_BYTES );
	}
};


} // namespace cat

#endif // CAT_ASYNC_BUFFER_HPP
