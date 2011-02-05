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

#ifndef CAT_WORKER_THREADS_HPP
#define CAT_WORKER_THREADS_HPP

#include <cat/threads/RefObject.hpp>
#include <cat/threads/Thread.hpp>
#include <cat/threads/WaitableFlag.hpp>
#include <cat/threads/Mutex.hpp>
#include <cat/threads/Atomic.hpp>
#include <cat/mem/IAllocator.hpp>

#if defined(CAT_NO_ATOMIC_ADD)
#define CAT_NO_ATOMIC_POPCOUNT
#endif

namespace cat {


class IWorkerTLS;
class IWorkerTLSBuilder;
class WorkerCallbacks;
class WorkerThread;
class WorkerThreads;


static const u32 MAX_WORKER_THREADS = 32;
static const u32 WORKER_TICK_INTERVAL = 20;

// Interface for worker thread-local storage
class IWorkerTLS
{
public:
	CAT_INLINE virtual ~IWorkerTLS() {}

	virtual bool Valid() = 0;
};

class IWorkerTLSBuilder
{
public:
	CAT_INLINE virtual ~IWorkerTLSBuilder() {}

	virtual IWorkerTLS *Build() = 0;
};

template<class LocalStorageT>
class WorkerTLSBuilder : public IWorkerTLSBuilder
{
public:
	CAT_INLINE virtual ~WorkerTLSBuilder() {}

	IWorkerTLS *Build() { return new LocalStorageT; }
};


// A buffer specialized for handling by the worker threads
struct WorkerBuffer : public BatchHead
{
	WorkerCallbacks *callback;
};


class WorkerCallbacks
{
	friend class WorkerThread;

	RefObject *_parent;
	WorkerCallbacks *_worker_prev, *_worker_next;

protected:
	CAT_INLINE void InitializeWorkerCallbacks(RefObject *obj) { _parent = obj; }

	virtual void OnWorkerRead(IWorkerTLS *tls, const BatchSet &buffers) = 0;
	virtual void OnWorkerTick(IWorkerTLS *tls, u32 now) = 0;
};


class WorkerThread : public Thread
{
	virtual bool ThreadFunction(void *master);

	u32 _session_count;

	WaitableFlag _event_flag;
	volatile bool _kill_flag;

	// Protected list of new workers to add to the running list
	Mutex _new_workers_lock;
	WorkerCallbacks *_new_head;

	// Queue of buffers waiting to be processed
	Mutex _workqueue_lock;
	BatchSet _workqueue;

public:
	WorkerThread();
	virtual ~WorkerThread();

	CAT_INLINE u32 GetSessionCount() { return _session_count; }
	CAT_INLINE void FlagEvent() { _event_flag.Set(); }
	CAT_INLINE void SetKillFlag() { _kill_flag = true; }

	void DeliverBuffers(const BatchSet &buffers);
	void Associate(WorkerCallbacks *callbacks);
};


class WorkerThreads
{
	friend class WorkerThread;

#if !defined(CAT_NO_ATOMIC_POPCOUNT)
	volatile u32 _population;
	u8 _padding[CAT_DEFAULT_CACHE_LINE_SIZE];
#endif // CAT_NO_ATOMIC_POPCOUNT

	u32 _worker_count;
	WorkerThread *_workers;

	IWorkerTLSBuilder *_tls_builder;

public:
	WorkerThreads();
	virtual ~WorkerThreads();

	CAT_INLINE u32 GetWorkerCount() { return _worker_count; }

	u32 FindLeastPopulatedWorker();

#if defined(CAT_NO_ATOMIC_POPCOUNT)
	u32 GetTotalPopulation();
#else
	CAT_INLINE u32 GetTotalPopulation() { return _population; }
#endif // CAT_NO_ATOMIC_POPCOUNT

	CAT_INLINE void DeliverBuffers(u32 worker_id, const BatchSet &buffers)
	{
		_workers[worker_id].DeliverBuffers(buffers);
	}

	bool Startup(IWorkerTLSBuilder *tls_builder);

	bool Shutdown();

	CAT_INLINE u32 AssignWorker(WorkerCallbacks *callbacks)
	{
#if !defined(CAT_NO_ATOMIC_POPCOUNT)
		Atomic::Add(&_population, 1);
#endif // CAT_NO_ATOMIC_POPCOUNT

		u32 worker_id = FindLeastPopulatedWorker();

		_workers[worker_id].Associate(callbacks);

		return worker_id;
	}
};


} // namespace cat

#endif // CAT_WORKER_THREADS_HPP
