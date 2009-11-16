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

#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <cat/Singleton.hpp>
#include <cat/threads/Mutex.hpp>

#if defined(CAT_OS_WINDOWS)
#include <windows.h>
#endif

namespace cat {


//// Overlapped Opcodes

// Overlapped opcodes that describe the purpose of the OVERLAPPED structure
enum OverlappedOpcodes
{
	// Sockets
    OVOP_ACCEPT_EX,    // AcceptEx() completion, remote client connected
    OVOP_SERVER_RECV,  // WSARecv() completion for local server
    OVOP_CLIENT_RECV,  // WSARecv() completion, for local client
    OVOP_RECVFROM,     // WSARecvFrom() completion, for local endpoint
    OVOP_CONNECT_EX,   // ConnectEx() completion, local client connected
    OVOP_SERVER_SEND,  // WSASend() completion, local server sent something
    OVOP_CLIENT_SEND,  // WSASend() completion, local client sent something
    OVOP_SENDTO,       // WSASendTo() completion, local endpoint sent something
    OVOP_SERVER_CLOSE, // DisconnectEx() completion, graceful close
    OVOP_CLIENT_CLOSE, // DisconnectEx() completion, graceful close

	// File I/O
	OVOP_READFILE_EX, // ReadFileEx() completion, data hopefully available
};

// Base class for any typed OVERLAPPED structure
struct TypedOverlapped
{
    OVERLAPPED ov;
    int opcode;

    void Set(int new_opcode);

    // Reset after an I/O operation to prepare for the next one
    void Reset();
};


/*
    class ThreadPool

    Startup()  : Call to start up the thread pool
    Shutdown() : Call to destroy the thread pool and objects

	This thread pool is specialized for sockets and files.
*/
class ThreadPool : public Singleton<ThreadPool>
{
    friend class TCPServer;
    friend class TCPServerConnection;
    friend class TCPClient;
    friend class UDPEndpoint;
    static unsigned int WINAPI CompletionThread(void *port);

    CAT_SINGLETON(ThreadPool);

protected:
    HANDLE _port;
    std::vector<HANDLE> _threads;

protected:
    // Track sockets for graceful termination
    Mutex _socketLock;
    SocketRefObject *_socketRefHead;

    friend class SocketRefObject;
    void TrackSocket(SocketRefObject *object);
    void UntrackSocket(SocketRefObject *object);

protected:
    bool SpawnThread();
    bool SpawnThreads();
    bool Associate(HANDLE h, void *key);

public:
    void Startup();
    void Shutdown();
};


} // namespace cat

#endif // THREAD_POOL_HPP
