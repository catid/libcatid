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

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


struct IOCPOverlapped;
struct IOTLS;
class IOThread;
class IOThreads;
class UDPEndpoint;
class AsyncFile;

enum IOType
{
	IOTYPE_UDP_SEND,
	IOTYPE_UDP_RECV,
	IOTYPE_FILE_WRITE,
	IOTYPE_FILE_READ
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

struct IOCPOverlappedSendTo : IOCPOverlapped
{
};

typedef IOCPOverlappedRecvFrom IOLayerRecvOverhead;
typedef IOCPOverlappedSendTo IOLayerSendOverhead;

struct IOCPOverlappedReadFile : IOCPOverlapped
{
};

struct IOCPOverlappedWriteFile : IOCPOverlapped
{
};

typedef IOCPOverlappedReadFile IOLayerReadOverhead;
typedef IOCPOverlappedWriteFile IOLayerWriteOverhead;

static const u32 IOTHREADS_BUFFER_READ_BYTES = 1450;
static const u32 IOTHREADS_BUFFER_COUNT = 10000;

typedef BOOL (WINAPI *PGetQueuedCompletionStatusEx)(
	HANDLE CompletionPort,
	LPOVERLAPPED_ENTRY lpCompletionPortEntries,
	ULONG ulCount,
	PULONG ulNumEntriesRemoved,
	DWORD dwMilliseconds,
	BOOL fAlertable
	);


// IOCP thread
class CAT_EXPORT IOThread : public Thread
{
	CAT_INLINE bool HandleCompletion(IOThreads *master, OVERLAPPED_ENTRY entries[], u32 count,
		u32 event_msec, BatchSet &sendq, BatchSet &recvq,
		UDPEndpoint *&prev_recv_endpoint, u32 &recv_count);

	void UseVistaAPI(IOThreads *master);
	void UsePreVistaAPI(IOThreads *master);

	virtual bool ThreadFunction(void *vmaster);
};


// IOCP threads
class CAT_EXPORT IOThreads
{
	friend class IOThread;

	u32 _worker_count;
	IOThread *_workers;

	PGetQueuedCompletionStatusEx _GetQueuedCompletionStatusEx;
	HANDLE _io_port;

	BufferAllocator *_recv_allocator;

public:
	IOThreads();
	virtual ~IOThreads();

	CAT_INLINE BufferAllocator *GetRecvAllocator() { return _recv_allocator; }

	bool Startup();
	bool Shutdown();
	bool Associate(UDPEndpoint *udp_endpoint);
	bool Associate(AsyncFile *file);
};


} // namespace cat

#endif // CAT_IOCP_IO_THREADS_HPP
