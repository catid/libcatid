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

#include <cat/iocp/IOThreadPools.hpp>
#include <cat/io/IOLayer.hpp>
#include <cat/io/Buffers.hpp>
#include <cat/time/Clock.hpp>
#include <cat/port/SystemInfo.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/Settings.hpp>
using namespace cat;


//// IOThreadPools Singleton

static IOThreadPools iothreads;

IOThreadPools *IOThreadPools::ref()
{
	return &iothreads;
}


//// IOThread

CAT_INLINE bool IOThread::HandleCompletion(IOThreadPool *master, OVERLAPPED_ENTRY entries[], u32 count, u32 event_msec, BatchSet &sendq, BatchSet &recvq, UDPEndpoint *&prev_recv_endpoint, u32 &recv_count)
{
	bool exit_flag = false;

	// For each entry,
	for (u32 ii = 0; ii < count; ++ii)
	{
		IOCPOverlapped *ov_iocp = reinterpret_cast<IOCPOverlapped*>( entries[ii].lpOverlapped );
		IOThreadsAssociator *associator = reinterpret_cast<IOThreadsAssociator*>( entries[ii].lpCompletionKey );
		u32 bytes = entries[ii].dwNumberOfBytesTransferred;

		// Terminate thread on zero completion
		if (!ov_iocp)
		{
			exit_flag = true;
			continue;
		}

		// Based on type of IO,
		switch (ov_iocp->io_type)
		{
		case IOTYPE_UDP_SEND:
			{
				UDPEndpoint *udp_endpoint = static_cast<UDPEndpoint*>( associator );
				SendBuffer *buffer = reinterpret_cast<SendBuffer*>( (u8*)ov_iocp - offsetof(SendBuffer, iointernal.ov) );

				// Link to sendq
				if (sendq.tail) sendq.tail->batch_next = buffer;
				else sendq.head = buffer;

				sendq.tail = buffer;
				buffer->batch_next = 0;

				udp_endpoint->ReleaseRef();
			}
			break;

		case IOTYPE_UDP_RECV:
			{
				UDPEndpoint *udp_endpoint = static_cast<UDPEndpoint*>( associator );
				RecvBuffer *buffer = reinterpret_cast<RecvBuffer*>( (u8*)ov_iocp - offsetof(RecvBuffer, iointernal.ov) );

				// Write event completion results to buffer
				buffer->data_bytes = bytes;
				buffer->event_msec = event_msec;

				// If the same UDP endpoint got the last request too,
				if (prev_recv_endpoint == udp_endpoint)
				{
					// Append to recvq
					recvq.tail->batch_next = buffer;
					recvq.tail = buffer;
					++recv_count;
				}
				else
				{
					// If recvq is not empty,
					if (recvq.head)
					{
						// Finalize the recvq and post it
						recvq.tail->batch_next = 0;
						prev_recv_endpoint->OnRecvCompletion(recvq, recv_count);
					}

					// Reset recvq
					recvq.head = recvq.tail = buffer;
					recv_count = 1;
					prev_recv_endpoint = udp_endpoint;
				}
			}
			break;

		case IOTYPE_FILE_WRITE:
			{
				AsyncFile *async_file = static_cast<AsyncFile*>( associator );
				WriteBuffer *buffer = reinterpret_cast<WriteBuffer*>( (u8*)ov_iocp - offsetof(WriteBuffer, iointernal.ov) );

				// Write event completion results to buffer
				buffer->offset = ((u64)buffer->iointernal.ov.OffsetHigh << 32) | buffer->iointernal.ov.Offset;
				buffer->data_bytes = bytes;

				// Deliver the buffer to the worker threads
				WorkerThreads::ref()->DeliverBuffers(WQPRIO_LO, buffer->worker_id, buffer);

				async_file->ReleaseRef();
			}
			break;

		case IOTYPE_FILE_READ:
			{
				AsyncFile *async_file = static_cast<AsyncFile*>( associator );
				ReadBuffer *buffer = reinterpret_cast<ReadBuffer*>( (u8*)ov_iocp - offsetof(ReadBuffer, iointernal.ov) );

				// Write event completion results to buffer
				buffer->offset = ((u64)buffer->iointernal.ov.OffsetHigh << 32) | buffer->iointernal.ov.Offset;
				buffer->data_bytes = bytes;

				// Deliver the buffer to the worker threads
				WorkerThreads::ref()->DeliverBuffers(WQPRIO_LO, buffer->worker_id, buffer);

				async_file->ReleaseRef();
			}
			break;
		}
	}

	// If recvq is not empty,
	if (recvq.head)
	{
		// Finalize the recvq and post it
		recvq.tail->batch_next = 0;
		prev_recv_endpoint->OnRecvCompletion(recvq, recv_count);

		recvq.Clear();
		prev_recv_endpoint = 0;
		recv_count = 0;
	}

	// If sendq is not empty,
	if (sendq.head)
	{
		sendq.tail->batch_next = 0;
		StdAllocator::ii->ReleaseBatch(sendq);

		sendq.Clear();
	}

	return exit_flag;
}

void IOThread::UseVistaAPI(IOThreadPool *master)
{
	PGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx =
		IOThreadPools::ref()->GetIOThreadImports()->pGetQueuedCompletionStatusEx;
	HANDLE port = master->GetIOPort();

	static const u32 MAX_IO_GATHER = 32;
	OVERLAPPED_ENTRY entries[MAX_IO_GATHER];
	unsigned long ulEntriesRemoved;

	BatchSet sendq, recvq;
	sendq.Clear();
	recvq.Clear();

	UDPEndpoint *prev_recv_endpoint = 0;
	u32 recv_count = 0;

	while (pGetQueuedCompletionStatusEx(port, entries, MAX_IO_GATHER, &ulEntriesRemoved, INFINITE, FALSE))
	{
		u32 event_time = Clock::msec();

		// Quit if we received the quit signal
		if (HandleCompletion(master, entries, ulEntriesRemoved, event_time, sendq, recvq, prev_recv_endpoint, recv_count))
			break;
	}
}

void IOThread::UsePreVistaAPI(IOThreadPool *master)
{
	HANDLE port = master->GetIOPort();

	DWORD bytes;
	ULONG_PTR key;
	LPOVERLAPPED ov;

	static const u32 MAX_IO_GATHER = 4;
	OVERLAPPED_ENTRY entries[MAX_IO_GATHER];
	u32 count = 0;

	BatchSet sendq, recvq;
	sendq.Clear();
	recvq.Clear();

	UDPEndpoint *prev_recv_endpoint = 0;
	u32 recv_count = 0;

	CAT_FOREVER
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
		if (HandleCompletion(master, entries, count, event_time, sendq, recvq, prev_recv_endpoint, recv_count))
			break;

		count = 0;
	}
}

bool IOThread::ThreadFunction(void *vmaster)
{
	IOThreadPool *master = reinterpret_cast<IOThreadPool*>( vmaster );

	if (IOThreadPools::ref()->GetIOThreadImports()->pGetQueuedCompletionStatusEx)
		UseVistaAPI(master);
	else
		UsePreVistaAPI(master);

	return true;
}


//// IOThreadPool

IOThreadPool::IOThreadPool()
{
	_io_port = 0;

	_worker_count = 0;
	_workers = 0;
}

IOThreadPool::~IOThreadPool()
{
	Shutdown();
}

bool IOThreadPool::Startup(u32 max_worker_count)
{
	// If startup was previously attempted,
	if (_worker_count || _io_port)
	{
		// Clean up and try again
		Shutdown();
	}

	// Initialize the worker count to the number of processors
	u32 worker_count = system_info.ProcessorCount;
	if (worker_count < 1) worker_count = 1;

	// If worker count override is set,
	u32 worker_count_override = Settings::ref()->getInt("IOThreadPools.Count", 0);
	if (worker_count_override != 0)
	{
		// Use it instead of the number of processors
		worker_count = worker_count_override;
	}

	// Impose max worker count if it is specified
	if (max_worker_count && worker_count > max_worker_count)
		worker_count = max_worker_count;

	_workers = new IOThread[worker_count];
	if (!_workers)
	{
		CAT_FATAL("IOThreadPools") << "Out of memory while allocating " << worker_count << " worker thread objects";
		return false;
	}

	_worker_count = worker_count;

	_io_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

	if (!_io_port)
	{
		CAT_FATAL("IOThreadPools") << "CreateIoCompletionPort error " << GetLastError();
		return false;
	}

	// For each worker,
	for (u32 ii = 0; ii < worker_count; ++ii)
	{
		// Start its thread
		if (!_workers[ii].StartThread(this))
		{
			CAT_FATAL("IOThreadPools") << "StartThread error " << GetLastError();
			return false;
		}
	}

	return true;
}

bool IOThreadPool::Shutdown()
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
				CAT_FATAL("IOThreadPools") << "PostQueuedCompletionStatus error " << GetLastError();
			}
		}
	}

	const int SHUTDOWN_WAIT_TIMEOUT = 15000; // 15 seconds

	// For each worker thread,
	for (u32 ii = 0; ii < worker_count; ++ii)
	{
		if (!_workers[ii].WaitForThread(SHUTDOWN_WAIT_TIMEOUT))
		{
			CAT_FATAL("IOThreadPools") << "Thread " << ii << "/" << worker_count << " refused to die!  Attempting lethal force...";
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

	return true;
}

bool IOThreadPool::Associate(IOThreadsAssociator *associator)
{
	if (!_io_port)
	{
		CAT_FATAL("IOThreadPools") << "Unable to associate handle since completion port was never created";
		return false;
	}

	HANDLE result = CreateIoCompletionPort(associator->GetHandle(), _io_port, (ULONG_PTR)associator, 0);

	if (result != _io_port)
	{
		CAT_FATAL("IOThreadPools") << "Associating handle error " << GetLastError();
		return false;
	}

	return true;
}


//// IOThreadPools

IOThreadPools::IOThreadPools()
{
	_recv_allocator = 0;

	// Attempt to use Vista+ API
	_imports.pGetQueuedCompletionStatusEx = (PGetQueuedCompletionStatusEx)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetQueuedCompletionStatusEx");
	_imports.pSetFileCompletionNotificationModes = (PSetFileCompletionNotificationModes)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetFileCompletionNotificationModes");
	_imports.pSetFileIoOverlappedRange = (PSetFileIoOverlappedRange)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetFileIoOverlappedRange");
	_imports.pSetFileValidData = (PSetFileValidData)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetFileValidData");
}

IOThreadPools::~IOThreadPools()
{
	Shutdown();
}

bool IOThreadPools::Startup()
{
	AutoMutex lock(_lock);

	// If startup was previously attempted,
	if (_recv_allocator)
	{
		// Clean up and try again
		Shutdown();
	}

	_recv_allocator = new BufferAllocator(sizeof(RecvBuffer) + IOTHREADS_BUFFER_READ_BYTES, IOTHREADS_BUFFER_COUNT);

	if (!_recv_allocator || !_recv_allocator->Valid())
	{
		CAT_FATAL("IOThreadPools") << "Out of memory while allocating " << IOTHREADS_BUFFER_COUNT << " buffers for a shared pool";
		return false;
	}

	return _shared_pool.Startup();
}

bool IOThreadPools::Shutdown()
{
	AutoMutex lock(_lock);

	// If allocator was created,
	if (_recv_allocator)
	{
		delete _recv_allocator;
		_recv_allocator = 0;
	}

	// For each pool,
	for (pools_iter ii = _private_pools.begin(); ii != _private_pools.end(); ++ii)
		ii->Shutdown();

	_private_pools.clear();

	return _shared_pool.Shutdown();
}

IOThreadPool *IOThreadPools::AssociatePrivate(IOThreadsAssociator *associator)
{
	AutoMutex lock(_lock);

	_private_pools.push_front(IOThreadPool());

	IOThreadPool &pool = _private_pools.front();

	if (!pool.Startup(1) || !pool.Associate(associator))
	{
		_private_pools.pop_front();
		return 0;
	}

	return &pool;
}

bool IOThreadPools::DissociatePrivate(IOThreadPool *pool)
{
	AutoMutex lock(_lock);

	// For each pool,
	for (pools_iter ii = _private_pools.begin(); ii != _private_pools.end(); ++ii)
	{
		if (pool == &*ii)
		{
			bool success = pool->Shutdown();

			_private_pools.erase(ii);

			return success;
		}
	}

	return false;
}

bool IOThreadPools::AssociateShared(IOThreadsAssociator *associator)
{
	return _shared_pool.Associate(associator);
}
