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

#include <cat/io/Logging.hpp>
#include <cat/io/Settings.hpp>
#include <cat/io/ThreadPoolFiles.hpp>
#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/threads/RegionAllocator.hpp>
#include <cat/threads/ThreadPool.hpp>
#include <cat/time/Clock.hpp>
#include <process.h>
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
    _socketRefHead = 0;
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
    ULONG_PTR ulpProcessAffinityMask, ulpSystemAffinityMask;

	// Spawn two threads per processor
    GetProcessAffinityMask(GetCurrentProcess(), &ulpProcessAffinityMask, &ulpSystemAffinityMask);

    while (ulpProcessAffinityMask)
    {
        if (ulpProcessAffinityMask & 1)
        {
            SpawnThread();
            SpawnThread();
        }

        ulpProcessAffinityMask >>= 1;
    }

    if (_active_thread_count <= 0)
    {
        FATAL("ThreadPool") << "Unable to spawn any threads";
        return false;
    }

    INFO("ThreadPool") << "Spawned " << _active_thread_count << " worker threads";
    return true;
}

bool ThreadPool::Associate(HANDLE h, void *key)
{
    HANDLE result = CreateIoCompletionPort(h, _port, (ULONG_PTR)key, 0);

    if (!result)
    {
        FATAL("ThreadPool") << "Unable to create completion port: " << SocketGetLastErrorString();
        return false;
    }

    _port = result;

    if (_active_thread_count <= 0 && !SpawnThreads())
    {
        CloseHandle(_port);
        _port = 0;
        return false;
    }

    return true;
}


void ThreadPool::TrackSocket(SocketRefObject *object)
{
    object->last = 0;

    AutoMutex lock(_socketLock);

    // Add to the head of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    object->next = _socketRefHead;
    if (_socketRefHead) _socketRefHead->last = object;
    _socketRefHead = object;
}

void ThreadPool::UntrackSocket(SocketRefObject *object)
{
    AutoMutex lock(_socketLock);

    // Remove from the middle of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    SocketRefObject *last = object->last, *next = object->next;
    if (last) last->next = next;
    else _socketRefHead = next;
    if (next) next->last = last;
}

void ThreadPool::Startup()
{
	INANE("ThreadPool") << "Initializing the thread pool...";

	WSADATA wsaData;

    // Request Winsock 2.2
    if (NO_ERROR != WSAStartup(MAKEWORD(2,2), &wsaData))
    {
        FATAL("ThreadPool") << "WSAStartup error: " << SocketGetLastErrorString();
        return;
    }

	INANE("ThreadPool") << "...Initialization complete.";
}

void ThreadPool::Shutdown()
{
    INANE("ThreadPool") << "Terminating the thread pool...";

    u32 count = _active_thread_count;

    if (!count)
	{
		WARN("ThreadPool") << "Shutdown task (1/4): No threads are active";
	}
	else
    {
        INANE("ThreadPool") << "Shutdown task (1/4): Stopping threads...";

        if (_port)
        while (count--)
        {
            if (!PostQueuedCompletionStatus(_port, 0, 0, 0))
            {
                FATAL("ThreadPool") << "Shutdown post error: " << GetLastError();
                return;
            }
        }

        if (WAIT_FAILED == WaitForMultipleObjects(_active_thread_count, _threads, TRUE, INFINITE))
        {
            FATAL("ThreadPool") << "error waiting for thread termination: " << GetLastError();
            return;
        }

		for (int ii = 0; ii < _active_thread_count; ++ii)
		{
			CloseHandle(_threads[ii]);
		}

		_active_thread_count = 0;
    }

    INANE("ThreadPool") << "Shutdown task (2/4): Deleting remaining open sockets...";

    SocketRefObject *kill, *object = _socketRefHead;
    while (object)
    {
        kill = object;
        object = object->next;
        delete kill;
    }

    if (!_port)
	{
		WARN("ThreadPool") << "Shutdown task (3/4): IOCP port not created";
	}
	else
    {
		INANE("ThreadPool") << "Shutdown task (3/4): Closing IOCP port...";

		CloseHandle(_port);
        _port = 0;
    }

    INANE("ThreadPool") << "Shutdown task (4/4): WSACleanup()...";

    WSACleanup();

    INANE("ThreadPool") << "...Termination complete.";

    delete this;
}


unsigned int WINAPI ThreadPool::CompletionThread(void *port)
{
    DWORD bytes;
    void *key = 0;
    TypedOverlapped *ov = 0;
    int error;

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
            ( (TCPServerConnection*)key )->OnWSARecvComplete( error, bytes );
            ( (TCPServerConnection*)key )->ReleaseRef();
            // TCPServer tracks the overlapped buffer lifetime
            break;

        case OVOP_SERVER_SEND:
            ( (TCPServerConnection*)key )->OnWSASendComplete( error, bytes );
            ( (TCPServerConnection*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_SERVER_CLOSE:
            ( (TCPServerConnection*)key )->OnDisconnectExComplete( error );
            ( (TCPServerConnection*)key )->ReleaseRef();
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
            ( (UDPEndpoint*)key )->OnWSARecvFromComplete( error, (RecvFromOverlapped*)ov, bytes );
            ( (UDPEndpoint*)key )->ReleaseRef();
            // UDPEndpoint tracks the overlapped buffer lifetime
            break;

        case OVOP_SENDTO:
            ( (UDPEndpoint*)key )->OnWSASendToComplete( error, bytes );
            ( (UDPEndpoint*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

		case OVOP_READFILE_EX:
			{
				ReadFileOverlapped *readOv = reinterpret_cast<ReadFileOverlapped*>( ov );
				readOv->callback(GetTrailingBytes(readOv), bytes);
			}
			break;
        }
    }
}
