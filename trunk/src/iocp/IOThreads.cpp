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

#include <cat/iocp/IOThreads.hpp>
#include <cat/time/Clock.hpp>
#include <cat/port/SystemInfo.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;

static const u32 MAX_ENTRIES = 32;

CAT_INLINE bool IOThread::HandleCompletion(IOTLS *tls, u32 event_time, OVERLAPPED_ENTRY entries[], u32 errors[], u32 count)
{
	for (u32 ii = 0; ii < count; ++ii)
	{
		RefObject *obj = reinterpret_cast<RefObject*>( entries[ii].lpCompletionKey );
		IOCPOverlapped *nov = reinterpret_cast<IOCPOverlapped*>( entries[ii].lpOverlapped );
		u32 bytes = entries[ii].dwNumberOfBytesTransferred;

		// Terminate thread when we receive a zeroed completion packet
		if (!bytes && !obj && !nov)
			return true;

		// Invoke callback
		if (!nov->callback || nov->callback(tls, errors[ii], nov, bytes, event_time))
		{
			nov->allocator->Release(nov);
		}

		obj->ReleaseRef();
	}

	return false;
}
/*
void IOThread::UseVistaAPI(IOThreads *master, IOTLS *tls)
{
	PtGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx = master->pGetQueuedCompletionStatusEx;
	HANDLE port = master->_io_port;

	OVERLAPPED_ENTRY entries[MAX_ENTRIES];
	unsigned long ulEntriesRemoved;

	while (pGetQueuedCompletionStatusEx(port, entries, MAX_ENTRIES, &ulEntriesRemoved, INFINITE, FALSE))
	{
		u32 event_time = Clock::msec();

		// Quit if we received the quit signal
		if (HandleCompletion(tls, event_time, entries, ulEntriesRemoved))
			break;
	}
}
*/
void IOThread::UsePreVistaAPI(IOThreads *master, IOTLS *tls)
{
	HANDLE port = master->_io_port;

	DWORD bytes;
	ULONG_PTR key;
	LPOVERLAPPED ov;

	u32 errors[MAX_ENTRIES];
	OVERLAPPED_ENTRY entries[MAX_ENTRIES];
	u32 count = 0;

	for (;;)
	{
		BOOL bResult = GetQueuedCompletionStatus(port, &bytes, &key, &ov, INFINITE);

		u32 event_time = Clock::msec();
		u32 error = bResult ? GetLastError() : 0;

		// Attempt to pull off a number of events at a time
		do 
		{
			entries[count].lpOverlapped = ov;
			entries[count].lpCompletionKey = key;
			entries[count].dwNumberOfBytesTransferred = bytes;
			errors[count] = error;
			if (++count >= MAX_ENTRIES) break;

			bResult = GetQueuedCompletionStatus((HANDLE)port, &bytes, &key, &ov, 0);
		} while (bResult || ov);

		// Quit if we received the quit signal
		if (HandleCompletion(tls, event_time, entries, errors, count))
			break;

		count = 0;
	}
}

bool IOThread::ThreadFunction(void *vmaster)
{
	IOTLS tls;

	IOThreads *master = reinterpret_cast<IOThreads*>( vmaster );

	tls.allocator = new BufferAllocator(IOTLS_BUFFER_MIN_BYTES, IOTLS_BUFFER_COUNT);

	if (!tls.allocator || !tls.allocator->Valid())
	{
		FATAL("IOThread") << "Out of memory initializing BufferAllocator";
		return false;
	}

	// If it is available,
// TODO: Get the old stuff working first and then try to figure out how to get the error code out of the new Vista API
//	if (master->pGetQueuedCompletionStatusEx)
//		UseVistaAPI(master);
//	else
		UsePreVistaAPI(master);

	return true;
}


IOThreads::IOThreads()
{
	_io_port = 0;
	_worker_count = 0;
	_workers = 0;

	// Attempt to use Vista+ API
	pGetQueuedCompletionStatusEx = (PtGetQueuedCompletionStatusEx)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetQueuedCompletionStatusEx");
}

IOThreads::~IOThreads()
{
	Shutdown();
}

bool IOThreads::Startup()
{
	if (_worker_count || _io_port)
		return true;

	u32 worker_count = system_info.ProcessorCount;
	if (worker_count < 1) worker_count = 1;

	_workers = new IOThread[worker_count];
	if (!_workers)
	{
		FATAL("IOThreads") << "Out of memory while allocating " << worker_count << " worker thread objects";
		return false;
	}

	_worker_count = worker_count;

	_io_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

	if (!_io_port)
	{
		FATAL("IOThreads") << "CreateIoCompletionPort error " << GetLastError();
		return false;
	}

	// For each worker,
	for (u32 ii = 0; ii < worker_count; ++ii)
	{
		// Start its thread
		if (!_workers[ii].StartThread(this))
		{
			FATAL("IOThreads") << "StartThread error " << GetLastError();
			return false;
		}
	}

	return true;
}

bool IOThreads::Shutdown()
{
	u32 worker_count = _worker_count;

	// If port was created,
	if (_io_port)
	{
		// For each worker,
		for (u32 ii = 0; ii < worker_count; ++ii)
		{
			// Post a completion event that kills the worker threads
			if (!PostQueuedCompletionStatus(_io_port, 0, 0, 0))
			{
				FATAL("IOThreads") << "PostQueuedCompletionStatus error " << GetLastError();
			}
		}
	}

	const int SHUTDOWN_WAIT_TIMEOUT = 15000; // 15 seconds

	// For each worker thread,
	for (u32 ii = 0; ii < worker_count; ++ii)
	{
		if (!_workers[ii].WaitForThread(SHUTDOWN_WAIT_TIMEOUT))
		{
			FATAL("IOThreads") << "Thread " << ii << "/" << worker_count << " refused to die!  Attempting lethal force...";
			_workers[ii].AbortThread();
		}
	}

	_worker_count = 0;

	// Free worker thread objects
	if (_workers)
	{
		delete []_workers;
		_workers = 0;
	}

	// If port was created,
	if (_io_port)
	{
		CloseHandle(_io_port);
		_io_port = 0;
	}

	return true;
}

bool IOThreads::Associate(HANDLE h, RefObject *key)
{
	if (!_io_port)
	{
		FATAL("IOThreads") << "Unable to associate handle since completion port was never created";
		return false;
	}

	HANDLE result = CreateIoCompletionPort(h, _io_port, (ULONG_PTR)key, 0);

	if (result != _io_port)
	{
		FATAL("IOThreads") << "CreateIoCompletionPort error " << GetLastError();
		return false;
	}

	return true;
}
