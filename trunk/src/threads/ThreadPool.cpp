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

#include <cat/threads/ThreadPool.hpp>
#include <cat/io/Logging.hpp>
#include <cat/math/BitMath.hpp>
#include <process.h>
#include <cat/threads/Atomic.hpp>
#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/io/ThreadPoolFiles.hpp>
using namespace std;
using namespace cat;


//// TypedOverlapped

void TypedOverlapped::Set(int new_opcode)
{
    CAT_OBJCLR(ov);
    opcode = new_opcode;
}

void TypedOverlapped::Reset()
{
    CAT_OBJCLR(ov);
}


//// Thread Pool

ThreadPool::ThreadPool()
{
    _port = 0;
    CAT_OBJCLR(_objectRefHead);
	_active_thread_count = 0;
}

bool ThreadPool::SpawnThread()
{
	if (_active_thread_count >= MAX_THREADS)
	{
        WARN("ThreadPool") << "MAX_THREADS too low!  Limited to only " << MAX_THREADS;
		return false;
	}

    HANDLE thread = (HANDLE)_beginthreadex(0, 0, CompletionThread, _port, 0, 0);

    if (thread == (HANDLE)-1)
    {
        FATAL("ThreadPool") << "CreateThread() error: " << GetLastError();
        return false;
    }

	_threads[_active_thread_count++] = thread;
    return true;
}

bool ThreadPool::SpawnThreads()
{
	// Get the number of processors we have been given access to
    ULONG_PTR ulpProcessAffinityMask, ulpSystemAffinityMask;
    GetProcessAffinityMask(GetCurrentProcess(), &ulpProcessAffinityMask, &ulpSystemAffinityMask);
	int processor_count = (int)BitCount(ulpProcessAffinityMask);
	if (processor_count <= 0) processor_count = 1;
	_processor_count = processor_count;

	// Spawn two threads for each processor
	int threads_to_spawn = processor_count * 2;
	int ctr = threads_to_spawn;
	while (ctr--) SpawnThread();

	if (_active_thread_count <= 0)
	{
		FATAL("ThreadPool") << "Unable to spawn any threads";
		return false;
	}

	if (_active_thread_count < threads_to_spawn)
	{
		FATAL("ThreadPool") << "Thread creation failed.  Only spawned " << _active_thread_count << "/" << threads_to_spawn;
		return false;
	}

    INFO("ThreadPool") << "Spawned " << _active_thread_count << " worker threads";
    return true;
}

bool ThreadPool::Associate(HANDLE h, void *key)
{
	if (!_port)
	{
		FATAL("ThreadPool") << "Unable to associate handle since completion port was never created";
		return false;
	}

    HANDLE result = CreateIoCompletionPort(h, _port, (ULONG_PTR)key, 0);

    if (result != _port)
    {
        FATAL("ThreadPool") << "Unable to create completion port: " << GetLastError();
        return false;
    }

    return true;
}


//// ThreadRefObject

ThreadRefObject::ThreadRefObject(int priorityLevel)
{
	_refCount = 1;
	_priorityLevel = priorityLevel;

	ThreadPool::ref()->TrackObject(this);
}

void ThreadRefObject::AddRef()
{
	Atomic::Add(&_refCount, 1);
}

void ThreadRefObject::ReleaseRef()
{
	if (Atomic::Add(&_refCount, -1) == 1)
	{
		ThreadPool::ref()->UntrackObject(this);
		delete this;
	}
}

void ThreadPool::TrackObject(ThreadRefObject *object)
{
	int level = object->_priorityLevel;
    object->last = 0;

    AutoMutex lock(_objectRefLock[level]);

    // Add to the head of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    object->next = _objectRefHead[level];
    if (_objectRefHead[level]) _objectRefHead[level]->last = object;
    _objectRefHead[level] = object;
}

void ThreadPool::UntrackObject(ThreadRefObject *object)
{
	int level = object->_priorityLevel;

	AutoMutex lock(_objectRefLock[level]);

    // Remove from the middle of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    ThreadRefObject *last = object->last, *next = object->next;
    if (last) last->next = next;
    else _objectRefHead[level] = next;
    if (next) next->last = last;
}


//// ThreadPool

bool ThreadPool::Startup()
{
	INANE("ThreadPool") << "Initializing the thread pool...";

	HANDLE result = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

	if (!result)
	{
		FATAL("ThreadPool") << "Unable to create initial completion port: " << GetLastError();
		return false;
	}

	_port = result;

	if (_active_thread_count <= 0 && !SpawnThreads())
	{
		CloseHandle(_port);
		_port = 0;
		FATAL("ThreadPool") << "Unable to spawn threads";
		return false;
	}

	INANE("ThreadPool") << "...Initialization complete.";

	return true;
}

void ThreadPool::Shutdown()
{
    INANE("ThreadPool") << "Terminating the thread pool...";

    u32 count = _active_thread_count;

    if (!count)
	{
		WARN("ThreadPool") << "Shutdown task (1/3): No threads are active";
	}
	else
    {
        INANE("ThreadPool") << "Shutdown task (1/3): Stopping threads...";

        if (_port)
        while (count--)
        {
            if (!PostQueuedCompletionStatus(_port, 0, 0, 0))
            {
                FATAL("ThreadPool") << "Shutdown task (1/3): !!! Shutdown post error: " << GetLastError();
                return;
            }
        }

		const int SHUTDOWN_WAIT_TIMEOUT = 10000; // 10 seconds

        if (WAIT_OBJECT_0 != WaitForMultipleObjects(_active_thread_count, _threads, TRUE, SHUTDOWN_WAIT_TIMEOUT))
        {
            FATAL("ThreadPool") << "Shutdown task (1/3): !!! Threads refuse to die.  Attempting lethal force.  Error: " << GetLastError();

			// For each thread,
			for (int ii = 0; ii < _active_thread_count; ++ii)
			{
				// If the thread is still stuck,
				if (WAIT_OBJECT_0 != WaitForSingleObject(_threads[ii], 0))
				{
					FATAL("ThreadPool") << "Shutdown task (1/3): !!! Killing thread " << ii << "...";
					// If we can get an exit code for the thread,
					DWORD ExitCode;
					if (GetExitCodeThread(_threads[ii], &ExitCode) != 0)
					{
						// Terminate it
						TerminateThread(_threads[ii], ExitCode);
					}
				}
			}
        }
		else
		{
			for (int ii = 0; ii < _active_thread_count; ++ii)
			{
				CloseHandle(_threads[ii]);
			}
		}

		_active_thread_count = 0;
    }

    INANE("ThreadPool") << "Shutdown task (2/3): Deleting remaining reference-counted objects...";

	for (int ii = 0; ii < REFOBJ_PRIO_COUNT; ++ii)
	{
		ThreadRefObject *kill, *object = _objectRefHead[ii];
		while (object)
		{
			kill = object;
			object = object->next;
			delete kill;
		}
	}

    if (!_port)
	{
		WARN("ThreadPool") << "Shutdown task (3/3): IOCP port not created";
	}
	else
    {
		INANE("ThreadPool") << "Shutdown task (3/3): Closing IOCP port...";

		CloseHandle(_port);
        _port = 0;
    }

    INANE("ThreadPool") << "...Termination complete.";

    delete this;
}


ThreadPoolLocalStorage::ThreadPoolLocalStorage()
{
	// Create 256-bit math library instance
	math = KeyAgreementCommon::InstantiateMath(256);

	// Create CSPRNG instance
	csprng = FortunaFactory::ref()->Create();
}

ThreadPoolLocalStorage::~ThreadPoolLocalStorage()
{
	if (math) delete math;
	if (csprng) delete csprng;
}

bool ThreadPoolLocalStorage::Valid()
{
	return math && csprng;
}


unsigned int WINAPI ThreadPool::CompletionThread(void *port)
{
    DWORD bytes;
    void *key = 0;
    TypedOverlapped *ov = 0;
    int error;

	ThreadPoolLocalStorage tls;

	if (!tls.Valid())
	{
		FATAL("ThreadPool") << "Unable to initialize thread local storage objects";
		return 1;
	}

	for (;;)
    {
        if (GetQueuedCompletionStatus((HANDLE)port, &bytes, (PULONG_PTR)&key, (OVERLAPPED**)&ov, INFINITE))
            error = 0;
        else
        {
            error = WSAGetLastError();

            switch (error)
            {
            case WSA_OPERATION_ABORTED:
            case ERROR_CONNECTION_ABORTED:
            case ERROR_NETNAME_DELETED:  // Operation on closed socket failed
            case ERROR_MORE_DATA:        // UDP buffer not large enough for whole packet
            case ERROR_PORT_UNREACHABLE: // Got an ICMP response back that the destination port is unreachable
            case ERROR_SEM_TIMEOUT:      // Half-open TCP AcceptEx() has reset
                // Operation failure codes (we don't differentiate between them)
                break;

            default:
                // Report other errors this library hasn't been designed to handle yet
                FATAL("WorkerThread") << SocketGetLastErrorString() << " (key=" << key << ", ov="
                    << ov << ", opcode=" << (ov ? ov->opcode : -1) << ", bytes=" << bytes << ")";
                break;
            }
        }

        // Terminate thread when we receive a zeroed completion packet
        if (!bytes && !key && !ov)
            return 0;

        switch (ov->opcode)
        {
        case OVOP_ACCEPT_EX:
            ( (TCPServer*)key )->OnAcceptExComplete( error, (AcceptExOverlapped*)ov );
            ( (TCPServer*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_SERVER_RECV:
            ( (TCPConnection*)key )->OnWSARecvComplete( error, bytes );
            ( (TCPConnection*)key )->ReleaseRef();
            // TCPServer tracks the overlapped buffer lifetime
            break;

        case OVOP_SERVER_SEND:
            ( (TCPConnection*)key )->OnWSASendComplete( error, bytes );
            ( (TCPConnection*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_SERVER_CLOSE:
            ( (TCPConnection*)key )->OnDisconnectExComplete( error );
            ( (TCPConnection*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_CONNECT_EX:
            ( (TCPClient*)key )->OnConnectExComplete( error );
            ( (TCPClient*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_CLIENT_RECV:
            ( (TCPClient*)key )->OnWSARecvComplete( error, bytes );
            ( (TCPClient*)key )->ReleaseRef();
            // TCPClient tracks the overlapped buffer lifetime
            break;

        case OVOP_CLIENT_SEND:
            ( (TCPClient*)key )->OnWSASendComplete( error, bytes );
            ( (TCPClient*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_CLIENT_CLOSE:
            ( (TCPClient*)key )->OnDisconnectExComplete( error );
            ( (TCPClient*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_RECVFROM:
            ( (UDPEndpoint*)key )->OnWSARecvFromComplete( &tls, error, (RecvFromOverlapped*)ov, bytes );
            ( (UDPEndpoint*)key )->ReleaseRef();
            // UDPEndpoint tracks the overlapped buffer lifetime
            break;

        case OVOP_SENDTO:
            ( (UDPEndpoint*)key )->OnWSASendToComplete( error, bytes );
            ( (UDPEndpoint*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

		case OVOP_READFILE_EX:
			( (AsyncFile*)key )->OnRead(&tls, (ReadFileOverlapped*)ov, error ? 0 : bytes);
			( (AsyncFile*)key )->ReleaseRef();
			RegionAllocator::ii->Release(ov);
			break;

		case OVOP_READFILE_BULK:
			( (AsyncFile*)key )->OnReadBulk(&tls, (ReadFileBulkOverlapped*)ov, error ? 0 : bytes);
			( (AsyncFile*)key )->ReleaseRef();
			RegionAllocator::ii->Release(ov);
			break;

		case OVOP_WRITEFILE_EX:
			( (AsyncFile*)key )->ReleaseRef();
			RegionAllocator::ii->Release(ov);
			break;
        }
    }

	return 0;
}
