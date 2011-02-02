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

// Win 9x has a limit of 16 but we don't support that anyway
static const u32 MAX_IO_GATHER = 32;


//// IOThread

CAT_INLINE bool IOThread::HandleCompletion(IOTLS *tls, OVERLAPPED_ENTRY entries[], u32 count, u32 event_time)
{
	// Batch release sends
	IOCPOverlappedSendTo *send_list[MAX_IO_GATHER];
	u32 send_list_size = 0;
	IAllocator *prev_allocator = 0;

	// Batch process receives
	IOCPOverlappedRecvFrom *recv_list[MAX_IO_GATHER];
	u32 recv_bytes_list[MAX_IO_GATHER];
	u32 recv_list_size = 0;
	UDPEndpoint *prev_recv_endpoint = 0;

	// Batch release references
	UDPEndpoint *prev_endpoint = 0;
	s32 batched_refs = 0;

	for (u32 ii = 0; ii < count; ++ii)
	{
		UDPEndpoint *udp_endpoint = reinterpret_cast<UDPEndpoint*>( entries[ii].lpCompletionKey );
		IOCPOverlapped *ov_iocp = reinterpret_cast<IOCPOverlapped*>( entries[ii].lpOverlapped );
		u32 bytes = entries[ii].dwNumberOfBytesTransferred;

		// Terminate thread on zero completion
		if (!ov_iocp) return true;

		// Based on type of IO,
		switch (ov_iocp->io_type)
		{
		case IOTYPE_UDP_SEND:
			// For each send buffer that was batched with this one,
			for (IOCPOverlappedSendTo *ov_send = reinterpret_cast<IOCPOverlappedSendTo*>( ov_iocp ); ov_send ; ov_send = ov_send->next)
			{
				IAllocator *allocator = ov_send->allocator;

				// If the same allocator was used twice in a row and there is space,
				if (prev_allocator == allocator && send_list_size < MAX_IO_GATHER)
				{
					// Store this for batched release
					send_list[send_list_size++] = ov_send;
				}
				else
				{
					// If sends exist,
					if (send_list_size)
					{
						// Release the batch
						prev_allocator->ReleaseBatch((void**)send_list, send_list_size);
					}

					// Reset the list
					prev_allocator = allocator;
					send_list[0] = ov_send;
					send_list_size = 1;
				}
			}
			break;

		case IOTYPE_UDP_RECV:
			{
				IOCPOverlappedRecvFrom *ov_recv = reinterpret_cast<IOCPOverlappedSendTo*>( ov_iocp );

				// If the same allocator was used twice in a row and there is space,
				if (prev_recv_endpoint == udp_endpoint && recv_list_size < MAX_IO_GATHER)
				{
					// Store this for batched release
					recv_list[recv_list_size] = ov_recv;
					recv_bytes_list[recv_list_size] = bytes;
					++recv_list_size;
				}
				else
				{
					// If sends exist,
					if (recv_list_size)
					{
						// Deliver the batch
						prev_recv_endpoint->OnRead(tls, recv_list, recv_bytes_list, recv_list_size, event_time);
					}

					// Reset the list
					recv_list[0] = ov_recv;
					recv_bytes_list[0] = bytes;
					recv_list_size = 1;
				}
			}
			break;
		}

		// If the same endpoint received another event,
		if (udp_endpoint == prev_endpoint)
		{
			// Incremented the batched refs
			++batched_refs;
		}
		else
		{
			// If refs exist,
			if (batched_refs)
			{
				// Release the batch
				udp_endpoint->ReleaseRef(batched_refs);
			}

			// Set the endpoint reference
			prev_endpoint = udp_endpoint;
		}
	}

	// Deliver remaining received
	if (recv_list_size)
		prev_recv_endpoint->OnRead(tls, recv_list, recv_bytes_list, recv_list_size, event_time);

	// Release remaining sent
	if (send_list_size > 0)
		prev_allocator->ReleaseBatch((void**)send_list, send_list_size);

	// Release remaining refs
	if (batched_refs > 0)
		prev_endpoint->ReleaseRef(batched_refs);

	return false;
}

void IOThread::UseVistaAPI(IOTLS *tls, IOThreads *master)
{
	PtGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx = master->pGetQueuedCompletionStatusEx;
	HANDLE port = master->_io_port;

	OVERLAPPED_ENTRY entries[MAX_IO_GATHER];
	unsigned long ulEntriesRemoved;

	while (pGetQueuedCompletionStatusEx(port, entries, MAX_IO_GATHER, &ulEntriesRemoved, INFINITE, FALSE))
	{
		u32 event_time = Clock::msec();

		// Quit if we received the quit signal
		if (HandleCompletion(tls, entries, ulEntriesRemoved, event_time))
			break;
	}
}

void IOThread::UsePreVistaAPI(IOTLS *tls, IOThreads *master)
{
	HANDLE port = master->_io_port;

	DWORD bytes;
	ULONG_PTR key;
	LPOVERLAPPED ov;

	OVERLAPPED_ENTRY entries[MAX_IO_GATHER];
	u32 count = 0;

	for (;;)
	{
		BOOL bResult = GetQueuedCompletionStatus(port, &bytes, &key, &ov, INFINITE);

		u32 event_time = Clock::msec();

		// Attempt to pull off a number of events at a time
		do 
		{
			entries[count].lpOverlapped = ov;
			entries[count].lpCompletionKey = key;
			entries[count].dwNumberOfBytesTransferred = bytes;
			if (++count >= MAX_IO_GATHER) break;

			bResult = GetQueuedCompletionStatus((HANDLE)port, &bytes, &key, &ov, 0);
		} while (bResult || ov);

		// Quit if we received the quit signal
		if (HandleCompletion(tls, entries, count, event_time))
			break;

		count = 0;
	}
}

bool IOThread::ThreadFunction(void *vmaster)
{
	IOThreads *master = reinterpret_cast<IOThreads*>( vmaster );

	IAllocator *private_allocator = new BufferAllocator(IOTHREADS_BUFFER_TOTAL_BYTES, IOTLS_BUFFER_COUNT);

	if (!private_allocator || !private_allocator->Valid())
	{
		FATAL("IOThread") << "Out of memory initializing BufferAllocator";
		return false;
	}

	// Initialize TLS
	IOTLS tls;
	tls.allocators[0] = private_allocator;
	tls.allocators[1] = master->GetAllocator();
	tls.allocators[2] = StdAllocator::ii;

	// TODO: Test both of these

	// If it is available,
	if (master->pGetQueuedCompletionStatusEx)
		UseVistaAPI(&tls, master);
	else
		UsePreVistaAPI(&tls, master);

	return true;
}


//// IOThreads

IOThreads::IOThreads()
{
	_io_port = 0;
	_worker_count = 0;
	_workers = 0;
	_iothreads_allocator = 0;

	// Attempt to use Vista+ API
	pGetQueuedCompletionStatusEx = (PtGetQueuedCompletionStatusEx)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetQueuedCompletionStatusEx");
}

IOThreads::~IOThreads()
{
	Shutdown();
}

bool IOThreads::Startup()
{
	// If startup was previously attempted,
	if (_worker_count || _io_port || _iothreads_allocator)
	{
		// Clean up and try again
		Shutdown();
	}

	_iothreads_allocator = new BufferAllocator(IOTHREADS_BUFFER_TOTAL_BYTES, IOTHREADS_BUFFER_COUNT);
	if (!_iothreads_allocator || !_iothreads_allocator->Valid())
	{
		FATAL("IOThreads") << "Out of memory while allocating " << IOTHREADS_BUFFER_COUNT << " buffers for a shared pool";
		return false;
	}

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

	// Free worker thread objects
	if (_workers)
	{
		delete []_workers;
		_workers = 0;
	}

	_worker_count = 0;

	// If port was created,
	if (_io_port)
	{
		CloseHandle(_io_port);
		_io_port = 0;
	}

	// If allocator was created,
	if (_iothreads_allocator)
	{
		delete _iothreads_allocator;
		_iothreads_allocator = 0;
	}

	return true;
}

bool IOThreads::Associate(UDPEndpoint *udp_endpoint)
{
	if (!_io_port)
	{
		FATAL("IOThreads") << "Unable to associate handle since completion port was never created";
		return false;
	}

	HANDLE result = CreateIoCompletionPort((HANDLE)udp_endpoint->GetSocket(), _io_port, (ULONG_PTR)udp_endpoint, 0);

	if (result != _io_port)
	{
		FATAL("IOThreads") << "CreateIoCompletionPort error " << GetLastError();
		return false;
	}

	return true;
}
