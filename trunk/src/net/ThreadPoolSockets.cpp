/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/io/Logging.hpp>
#include <cat/threads/RegionAllocator.hpp>
using namespace std;
using namespace cat;


//// Windows-style IOCP

#if defined (CAT_MS_SOCKET_API)

namespace cat
{

	// Get a buffer used for posting data over the network
	u8 *GetPostBuffer(u32 bytes)
	{
		TypedOverlapped *sendOv = AcquireBuffer<TypedOverlapped>(bytes);
		if (!sendOv)
		{
			FATAL("IOCPSockets") << "Unable to allocate a send buffer: Out of memory";
			return 0;
		}

		return GetTrailingBytes(sendOv);
	}

	// Resize a previously acquired buffer larger or smaller
	void *ResizePostBuffer(void *buffer, u32 newBytes)
	{
		TypedOverlapped *sendOv = reinterpret_cast<TypedOverlapped*>(
			RegionAllocator::ii->Resize(
			reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped),
			sizeof(TypedOverlapped) + newBytes) );

		if (!sendOv)
		{
			FATAL("IOCPSockets") << "Unable to resize a send buffer: Out of memory";
			return 0;
		}

		return GetTrailingBytes(sendOv);
	}

	// Release a post buffer
	void ReleasePostBuffer(void *buffer)
	{
		RegionAllocator::ii->Release(
			reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped));
	}

} // namespace cat


// Amount of data to receive overlapped, tuned to exactly fit a
// 2048-byte buffer in the region allocator.
static const int RECV_DATA_SIZE = 2048 - sizeof(TypedOverlapped) - 8; // -8 for rebroadcast inflation
static const int RECVFROM_DATA_SIZE = 2048 - sizeof(RecvFromOverlapped) - 8;

#include "win/TCPServer.cpp"
#include "win/TCPConnection.cpp"
#include "win/TCPClient.cpp"
#include "win/UDPEndpoint.cpp"


//// Linux-style eventfd

#elif defined(CAT_OS_LINUX)

#include "linux/TCPServer.cpp"
#include "linux/TCPConnection.cpp"
#include "linux/TCPClient.cpp"
#include "linux/UDPEndpoint.cpp"


//// BSD-style kevent

#elif defined(CAT_OS_OSX) || defined(CAT_OS_BSD)

#include "bsd/TCPServer.cpp"
#include "bsd/TCPConnection.cpp"
#include "bsd/TCPClient.cpp"
#include "bsd/UDPEndpoint.cpp"


//// Fall-back

#else

#include "generic/TCPServer.cpp"
#include "generic/TCPConnection.cpp"
#include "generic/TCPClient.cpp"
#include "generic/UDPEndpoint.cpp"

#endif
