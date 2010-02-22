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
#include <cat/threads/RegionAllocator.hpp>
#include <cat/port/FastDelegate.h>

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


//// Reference Object priorities

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
    CAT_INLINE virtual ~ThreadRefObject() {}

public:
    void AddRef();
    void ReleaseRef();
};


//// TLS

class ThreadPoolLocalStorage
{
public:
	BigTwistedEdwards *math;
	FortunaOutput *csprng;

	ThreadPoolLocalStorage();
	~ThreadPoolLocalStorage();

	bool Valid();
};


//// Event Completion Objects

// Base class
struct AsyncBase;

// Data subclass
template<class TParams> struct AsyncData;

typedef fastdelegate::FastDelegate4<ThreadPoolLocalStorage *, int, AsyncBase *, u32, bool> CompletionCallback;

// Base class (OS-specific)
#if defined(CAT_OS_WINDOWS)

struct AsyncBase
{
protected:
	OVERLAPPED _ov;
	u8 *_data;
	u32 _overhead_bytes;
	u32 _data_bytes;
	CompletionCallback _callback;

public:
	CAT_INLINE OVERLAPPED *GetOv() { return &_ov; }
	CAT_INLINE u8 *GetData() { return _data; }
	CAT_INLINE u32 GetDataBytes() { return _data_bytes; }
	CAT_INLINE u32 GetOverheadBytes() { return _overhead_bytes; }
	CAT_INLINE CompletionCallback &GetCallback() { return _callback; }

	// Release memory
	CAT_INLINE void Release()
	{
		RegionAllocator::ii->Release(this);
	}

	CAT_INLINE void Reset(CompletionCallback &callback, u64 offset = 0)
	{
		_ov.hEvent = 0;
		_ov.Internal = 0;
		_ov.InternalHigh = 0;
		_ov.OffsetHigh = (u32)(offset >> 32);
		_ov.Offset = (u32)offset;
		_callback = callback;
	}

	CAT_INLINE u64 GetOffset()
	{
		return ((u64)_ov.OffsetHigh << 32) | _ov.Offset;
	}

	template<class TParams>
	CAT_INLINE void Subclass(AsyncData<TParams> * &ptr)
	{
		ptr = reinterpret_cast<AsyncData<TParams> *>( this );
	}
	template<class TParams>
	CAT_INLINE AsyncData<TParams> *Subclass()
	{
		return reinterpret_cast<AsyncData<TParams> *>( this );
	}

	CAT_INLINE AsyncBase *SetBuffer(void *buffer, u32 bytes)
	{
		_data = reinterpret_cast<u8*>( buffer );
		_data_bytes = bytes;
		return this;
	}

	CAT_INLINE AsyncBase *SetSize(u32 bytes)
	{
		_data_bytes = bytes;
		return this;
	}

	// Resize object to consume a different number of data bytes,
	// returning new value pointer to the object
	CAT_INLINE AsyncBase *Resize(u32 data_bytes)
	{
		AsyncBase *ptr = reinterpret_cast< AsyncBase* > (
			RegionAllocator::ii->Resize(this, _overhead_bytes + data_bytes) );

		if (ptr)
		{
			ptr->_data = reinterpret_cast<u8*>( ptr ) + _overhead_bytes;
			ptr->_data_bytes = data_bytes;
		}

		return ptr;
	}

	CAT_INLINE void Zero()
	{
		CAT_CLR(GetData(), GetDataBytes());
	}
};

#else
#error "This really only works for Windows right now"
#endif


// Complete Type + Subtype + Data definition
template<class TParams>
struct AsyncData : public AsyncBase, public TParams
{
private:
	// Access this with GetData() to avoid casting-related bugs
	u8 _trailing[1];

public:
	typedef AsyncData<TParams> mytype;

	static CAT_INLINE u32 OVERHEAD()
	{
		return (u32)offsetof(mytype, _data);
	}

	// Acquire memory
	static CAT_INLINE mytype *Acquire(u32 data_bytes = 0)
	{
		const u32 OVERHEAD_BYTES = (u32)offsetof(mytype, _data);

		// Acquire memory for object
		mytype *ptr = reinterpret_cast< mytype* > (
			RegionAllocator::ii->Acquire(OVERHEAD_BYTES + data_bytes) );

		if (ptr)
		{
			ptr->_data = reinterpret_cast<u8*>( ptr ) + OVERHEAD_BYTES;
			ptr->_overhead_bytes = OVERHEAD_BYTES;
			ptr->_data_bytes = data_bytes;
		}

		return ptr;
	}
	static CAT_INLINE bool Acquire(mytype * &ptr, u32 data_bytes = 0)
	{
		return !!(ptr = Acquire(data_bytes));
	}
};


// Simple async data
struct AsyncEmptyType {};

typedef AsyncData<AsyncEmptyType> AsyncSimpleData;


//// Shutdown

class ShutdownWait;
class ShutdownObserver;

class ShutdownWait
{
	friend class ShutdownObserver;

	HANDLE _event;
	ShutdownObserver *_observer;

	void OnShutdownDone();

public:
	// Priority number must be higher than users'
	ShutdownWait(int priorityLevel);
	/*virtual*/ ~ShutdownWait();

	CAT_INLINE ShutdownObserver *GetObserver() { return _observer; }

	bool WaitForShutdown(u32 milliseconds);
};

class ShutdownObserver : public ThreadRefObject
{
	friend class ShutdownWait;

	ShutdownWait *_wait;

private:
	ShutdownObserver(int priorityLevel, ShutdownWait *wait);
	~ShutdownObserver();
};


/*
    class ThreadPool

    Startup()  : Call to start up the thread pool
    Shutdown() : Call to destroy the thread pool and objects
*/
class ThreadPool : public Singleton<ThreadPool>
{
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

public:
    bool Startup();
    void Shutdown();
	bool Associate(HANDLE h, ThreadRefObject *key);

	int GetProcessorCount() { return _processor_count; }
	int GetThreadCount() { return _active_thread_count; }
};


} // namespace cat

#endif // CAT_THREAD_POOL_HPP
