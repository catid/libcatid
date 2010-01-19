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

#ifndef CAT_THREAD_POOL_HPP
#define CAT_THREAD_POOL_HPP

#include <cat/Singleton.hpp>
#include <cat/threads/Mutex.hpp>
#include <cat/crypt/tunnel/KeyAgreement.hpp>

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
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


enum RefObjectPriorities
{
	REFOBJ_PRIO_0,
	REFOBJ_PRIO_COUNT = 32,
};

/*
    class ThreadRefObject

    Base class for any thread-safe reference-counted thread pool object

	Designed this way so that all of these objects can be automatically deleted
*/
class ThreadRefObject
{
    friend class ThreadPool;
    ThreadRefObject *last, *next;

	int _priorityLevel;
    volatile u32 _refCount;

public:
    ThreadRefObject(int priorityLevel);
    virtual ~ThreadRefObject() {}

public:
    void AddRef();
    void ReleaseRef();
};


// TLS
class ThreadPoolLocalStorage
{
public:
	BigTwistedEdwards *math;
	FortunaOutput *csprng;

	ThreadPoolLocalStorage();
	~ThreadPoolLocalStorage();

	bool Valid();
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
    friend class TCPConnection;
    friend class TCPClient;
    friend class UDPEndpoint;
	friend class AsyncReadFile;
    static unsigned int WINAPI CompletionThread(void *port);

    CAT_SINGLETON(ThreadPool);

protected:
    HANDLE _port;
	static const int MAX_THREADS = 256;
	HANDLE _threads[MAX_THREADS];
	int _processor_count;
	int _active_thread_count;

protected:
    friend class ThreadRefObject;

	// Track sockets for graceful termination
    Mutex _objectRefLock[REFOBJ_PRIO_COUNT];
    ThreadRefObject *_objectRefHead[REFOBJ_PRIO_COUNT];

    void TrackObject(ThreadRefObject *object);
    void UntrackObject(ThreadRefObject *object);

protected:
    bool SpawnThread();
    bool SpawnThreads();
    bool Associate(HANDLE h, void *key);

public:
    bool Startup();
    void Shutdown();

	int GetProcessorCount() { return _processor_count; }
	int GetThreadCount() { return _active_thread_count; }
};


} // namespace cat

#endif // CAT_THREAD_POOL_HPP
