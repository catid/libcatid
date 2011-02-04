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

#ifndef CAT_IOCP_IO_THREADS_HPP
#define CAT_IOCP_IO_THREADS_HPP

#include <cat/threads/Thread.hpp>
#include <cat/net/Sockets.hpp>
#include <cat/threads/RefObject.hpp>
#include <cat/mem/BufferAllocator.hpp>
#include <cat/iocp/UDPEndpoint.hpp>

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


struct IOCPOverlapped;
struct IOTLS;
class IOThread;
class IOThreads;

static const u32 IOTHREADS_BUFFER_DATA_BYTES = 1450;
static const u32 IOTHREADS_BUFFER_TOTAL_BYTES = sizeof(IOCPOverlappedRecvFrom) + IOTHREADS_BUFFER_DATA_BYTES;
static const u32 IOTHREADS_BUFFER_COUNT = 10000;

//#define CAT_ALLOW_UNBOUNDED_RECV_BUFFERS

enum IOType
{
	IOTYPE_UDP_SEND,
	IOTYPE_UDP_RECV
};

struct IOCPOverlapped
{
	OVERLAPPED ov;

	// A value from enum IOType
	u32 io_type;
};

struct IOCPOverlappedRecvFrom : IOCPOverlapped
{
	int addr_len;
	sockaddr_in6 addr;
};

typedef BOOL (WINAPI *PtGetQueuedCompletionStatusEx)(
	__in   HANDLE CompletionPort,
	__out  LPOVERLAPPED_ENTRY lpCompletionPortEntries,
	__in   ULONG ulCount,
	__out  PULONG ulNumEntriesRemoved,
	__in   DWORD dwMilliseconds,
	__in   BOOL fAlertable
	);


// IOCP thread
class IOThread : public Thread
{
	CAT_INLINE bool HandleCompletion(IOThreads *master, OVERLAPPED_ENTRY entries[], u32 count, u32 event_time);

	void UseVistaAPI(IOThreads *master);
	void UsePreVistaAPI(IOThreads *master);

	virtual bool ThreadFunction(void *vmaster);
};


// IOCP threads
class IOThreads
{
	friend class IOThread;

	u32 _worker_count;
	IOThread *_workers;

	PtGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;
	HANDLE _io_port;

	BufferAllocator *_buffer_allocator;

public:
	IOThreads();
	virtual ~IOThreads();

	CAT_INLINE BufferAllocator *GetAllocator() { return _buffer_allocator; }

	bool Startup();
	bool Shutdown();
	bool Associate(UDPEndpoint *udp_endpoint);
};


} // namespace cat

#endif // CAT_IOCP_IO_THREADS_HPP
