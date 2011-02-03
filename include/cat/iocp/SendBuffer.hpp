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

#ifndef CAT_IOCP_SEND_BUFFER_HPP
#define CAT_IOCP_SEND_BUFFER_HPP

#include <cat/iocp/IOThreads.hpp>
#include <cat/mem/StdAllocator.hpp>

namespace cat {


// A buffer specialized for writing to a socket
class SendBuffer : public IOCPOverlapped
{
	friend class IOThread;
	friend class UDPEndpoint;

	SendBuffer *_next_buffer;
	u32 _data_bytes;

	u8 _data[1];

public:
	CAT_INLINE u8 *GetData() { return _data; }
	CAT_INLINE u32 GetDataBytes() { return _data_bytes; }

	CAT_INLINE void Reset(u64 offset = 0)
	{
		allocator = StdAllocator::ii;

		ov.hEvent = 0;
		ov.Internal = 0;
		ov.InternalHigh = 0;
		ov.OffsetHigh = (u32)(offset >> 32);
		ov.Offset = (u32)offset;

		io_type = IOTYPE_UDP_SEND;
	}

	// Acquire memory for a send buffer
	static CAT_INLINE SendBuffer *Acquire(SendBuffer * &ptr, u32 data_bytes = 0)
	{
		const u32 OVERHEAD_BYTES = (u32)(offsetof(SendBuffer, _data));

		SendBuffer *buffer = reinterpret_cast<SendBuffer*>(
			StdAllocator::ii->Acquire(OVERHEAD_BYTES + data_bytes) );

		if (!buffer) return 0;
		buffer->_data_bytes = data_bytes;
		return (ptr = buffer);
	}

	// Acquire memory for a send buffer
	static CAT_INLINE u8 *Acquire(u32 data_bytes = 0)
	{
		SendBuffer *buffer;

		if (!Acquire(buffer, data_bytes)) return 0;

		buffer->_data_bytes = data_bytes;
		return buffer->_data;
	}

	// Acquire memory for a send buffer
	template<class T>
	static CAT_INLINE T *Acquire(T * &data)
	{
		SendBuffer *buffer;

		if (!Acquire(buffer, sizeof(T))) return 0;

		buffer->_data_bytes = sizeof(T);
		return (data = reinterpret_cast<T*>( buffer->_data ));
	}

	// Change number of data bytes allocated to the buffer
	// Returns a new data pointer that may be different from the old data pointer
	CAT_INLINE SendBuffer *Resize(u32 data_bytes)
	{
		const u32 OVERHEAD_BYTES = (u32)(offsetof(SendBuffer, _data));

		SendBuffer *buffer = reinterpret_cast<SendBuffer*>(
			StdAllocator::ii->Resize(this, OVERHEAD_BYTES + data_bytes) );

		if (!buffer) return 0;
		buffer->_data_bytes = data_bytes;
		return buffer;
	}

	// Change number of data bytes allocated to the buffer
	// Returns a new data pointer that may be different from the old data pointer
	static CAT_INLINE u8 *Resize(void *vdata, u32 data_bytes)
	{
		u8 *data = reinterpret_cast<u8*>( vdata );
		const u32 OVERHEAD_BYTES = (u32)(offsetof(SendBuffer, _data));

		if (!data) return Acquire(data_bytes);

		SendBuffer *buffer = reinterpret_cast<SendBuffer*>( data - OVERHEAD_BYTES );

		buffer = buffer->Resize(data_bytes);

		if (!buffer) return 0;
		buffer->_data_bytes = data_bytes;
		return buffer->_data;
	}

	// Promote a data pointer to the full send buffer
	static CAT_INLINE SendBuffer *Promote(void *vdata)
	{
		u8 *data = reinterpret_cast<u8*>( vdata );
		const u32 OVERHEAD_BYTES = (u32)(offsetof(SendBuffer, _data));

		if (!data) return 0;
		return reinterpret_cast<SendBuffer*>( data - OVERHEAD_BYTES );
	}

	// Release memory
	CAT_INLINE void Release()
	{
		StdAllocator::ii->Release(this);
	}

	static CAT_INLINE void Release(SendBuffer *buffer)
	{
		StdAllocator::ii->Release(buffer);
	}

	static CAT_INLINE void Release(void *vdata)
	{
		SendBuffer *buffer = Promote(vdata);
		if (buffer) StdAllocator::ii->Release(buffer);
	}
};


} // namespace cat

#endif // CAT_IOCP_SEND_BUFFER_HPP
