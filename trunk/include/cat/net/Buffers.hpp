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

#ifndef CAT_NET_BUFFERS_HPP
#define CAT_NET_BUFFERS_HPP

#include <cat/io/IOLayer.hpp>
#include <cat/mem/ResizableBuffer.hpp>
#include <cat/threads/WorkerThreads.hpp>

namespace cat {


// A buffer specialized for writing to a socket
struct SendBuffer : public BatchHead, public ResizableBuffer<SendBuffer>
{
	// IO layer specific overhead pimpl
	IOLayerSendOverhead iointernal;
};


// A buffer specialized for reading data from a socket
// Compatible with WorkerBuffer object
struct RecvBuffer : BatchHead
{
	union
	{
		// IO layer specific overhead pimpl
		IOLayerRecvOverhead iointernal;

		// Worker layer specific overhead
		struct
		{
			IWorkerCallbacks *callback;
			UNetAddr addr;

			// TODO: Sphynx specific
			u32 send_time;
		};
	};

	// Shared overhead
	u32 data_bytes;
	u32 event_msec;
};


} // namespace cat

#endif // CAT_NET_BUFFERS_HPP
