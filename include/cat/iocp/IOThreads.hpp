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

#ifndef CAT_IOCP_THREADS_HPP
#define CAT_IOCP_THREADS_HPP

#include <cat/threads/Thread.hpp>
#include <cat/net/Sockets.hpp>
#include <cat/port/FastDelegate.h>
#include <cat/threads/RefObject.hpp>
#include <cat/mem/BufferAllocator.hpp>

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


struct IOCPOverlapped;
struct IOTLS;

// bool IOThreadCallback(IOTLS *tls, u32 error, IOCPOverlapped *ov, u32 bytes, u32 event_time)
typedef fastdelegate::FastDelegate5<IOTLS *, u32, IOCPOverlapped *, u32, u32, bool> IOThreadCallback;

struct IOTLS
{
	BufferAllocator *allocator;
};

struct IOCPOverlapped
{
	OVERLAPPED ov;
	IAllocator *allocator;
	IOThreadCallback callback;
};

struct IOCPOverlappedRecvFrom : public IOCPOverlapped
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

class IOThreads;

// IOCP thread
class IOThread : public Thread
{
	CAT_INLINE bool HandleCompletion(IOTLS *tls, u32 event_time, OVERLAPPED_ENTRY entries[], u32 errors[], u32 count);

	void UseVistaAPI(IOThreads *master, IOTLS *tls);
	void UsePreVistaAPI(IOThreads *master, IOTLS *tls);

	virtual bool ThreadFunction(void *port);
};

// IOCP threads
class IOThreads
{
	friend class IOThread;

	u32 _worker_count;
	IOThread *_workers;

	PtGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;
	HANDLE _io_port;

public:
	IOThreads();
	virtual ~IOThreads();

	bool Startup();
	bool Shutdown();
	bool Associate(HANDLE h, RefObject *key);
};


} // namespace cat

#endif // CAT_IOCP_THREADS_HPP
