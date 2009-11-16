/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cat/threads/ThreadPool.hpp>
#include <cat/io/Logging.hpp>
#include <cat/time/Clock.hpp>
#include <cat/io/Settings.hpp>
#include <cat/threads/RegionAllocator.hpp>
#include <process.h>
#include <algorithm>
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
}

bool ThreadPool::SpawnThread()
{
    HANDLE thread = (HANDLE)_beginthreadex(0, 0, CompletionThread, port, 0, 0);

    if (thread == (HANDLE)-1)
    {
        FATAL("ThreadPool") << "CreateThread() error: " << GetLastError();
        return false;
    }

    threads.push_back(thread);
    return true;
}

bool ThreadPool::SpawnThreads()
{
    ULONG_PTR ulpProcessAffinityMask, ulpSystemAffinityMask;

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

    if (threads.size() <= 0)
    {
        FATAL("ThreadPool") << "error to spawn any threads.";
        return false;
    }

    INFO("ThreadPool") << "Spawned " << (u32)threads.size() << " worker threads";
    return true;
}

bool ThreadPool::Associate(SOCKET s, void *key)
{
    HANDLE result = CreateIoCompletionPort((HANDLE)s, port, (ULONG_PTR)key, 0);

    if (!result)
    {
        FATAL("ThreadPool") << "Unable to create completion port: " << SocketGetLastErrorString();
        return false;
    }

    port = result;

    if (threads.size() <= 0 && !SpawnThreads())
    {
        CloseHandle(port);
        port = 0;
        return false;
    }

    return true;
}


void ThreadPool::TrackSocket(SocketRefObject *object)
{
    object->last = 0;

    AutoMutex lock(socketLock);

    // Add to the head of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    object->next = socketRefHead;
    if (socketRefHead) socketRefHead->last = object;
    socketRefHead = object;
}

void ThreadPool::UntrackSocket(SocketRefObject *object)
{
    AutoMutex lock(socketLock);

    // Remove from the middle of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    SocketRefObject *last = object->last, *next = object->next;
    if (last) last->next = next;
    else socketRefHead = next;
    if (next) next->last = last;
}

void ThreadPool::Startup()
{
    WSADATA wsaData;

    // Request Winsock 2.2
    if (NO_ERROR != WSAStartup(MAKEWORD(2,2), &wsaData))
    {
        FATAL("ThreadPool") << "WSAStartup error: " << SocketGetLastErrorString();
        return;
    }
}

void ThreadPool::Shutdown()
{
    INFO("ThreadPool") << "Terminating the thread pool...";

    u32 count = (u32)threads.size();

    if (count)
    {
        INFO("ThreadPool") << "Shutdown task (1/4): Stopping threads...";

        if (port)
        while (count--)
        {
            if (!PostQueuedCompletionStatus(port, 0, 0, 0))
            {
                FATAL("ThreadPool") << "Post error: " << GetLastError();
                return;
            }
        }

        if (WAIT_FAILED == WaitForMultipleObjects((DWORD)threads.size(), &threads[0], TRUE, INFINITE))
        {
            FATAL("ThreadPool") << "error waiting for thread termination: " << GetLastError();
            return;
        }

        for_each(threads.begin(), threads.end(), CloseHandle);

        threads.clear();
    }

    INFO("ThreadPool") << "Shutdown task (2/4): Deleting managed sockets...";

    SocketRefObject *kill, *object = socketRefHead;
    while (object)
    {
        kill = object;
        object = object->next;
        delete kill;
    }

    INFO("ThreadPool") << "Shutdown task (3/4): Closing IOCP port...";

    if (port)
    {
        CloseHandle(port);
        port = 0;
    }

    INFO("ThreadPool") << "Shutdown task (4/4): WSACleanup()...";

    WSACleanup();

    INFO("ThreadPool") << "...Termination complete.";

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
        }
    }
}
