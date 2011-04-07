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
#include <cat/lang/Delegates.hpp>

#if defined(CAT_NO_ATOMIC_ADD)
#define CAT_NO_ATOMIC_POPCOUNT
#endif

namespace cat {


class IWorkerTLS;
class IWorkerTLSBuilder;
class IWorkerTimer;
class WorkerThread;
class WorkerThreads;


static const u32 MAX_WORKER_THREADS = 32;
static const u32 INVALID_WORKER_ID = ~(u32)0;


// Interface for worker thread-local storage
class CAT_EXPORT IWorkerTLS
{
public:
	CAT_INLINE virtual ~IWorkerTLS() {}

	virtual bool Valid() = 0;
};

class CAT_EXPORT IWorkerTLSBuilder
{
public:
	CAT_INLINE virtual ~IWorkerTLSBuilder() {}

	virtual IWorkerTLS *Build() = 0;
};

template<class LocalStorageT>
class CAT_EXPORT WorkerTLSBuilder : public IWorkerTLSBuilder
{
public:
	CAT_INLINE virtual ~WorkerTLSBuilder() {}

	IWorkerTLS *Build() { return new LocalStorageT; }
};


// A buffer specialized for handling by the worker threads
typedef Delegate2<void, IWorkerTLS *, const BatchSet &> WorkerDelegate;

struct WorkerBuffer : public BatchHead
{
	WorkerDelegate callback;
};


// An element in the timer object array
typedef Delegate2<void, IWorkerTLS *, u32> WorkerTimerDelegate;

struct WorkerTimer
{
	RefObject *object;
	WorkerTimerDelegate timer;
};


enum WorkQueuePriorities
{
	WQPRIO_HI = 0,
	WQPRIO_LO = 1,
	WQPRIO_COUNT = 2
};

// Queue of buffers waiting to be processed
struct WorkerThreadQueue
{
	Mutex lock;
	BatchSet queued;
};

class CAT_EXPORT WorkerThread : public Thread
{
	virtual bool ThreadFunction(void *master);

	WaitableFlag _event_flag;
	volatile bool _kill_flag;

	WorkerThreadQueue _workqueues[WQPRIO_COUNT];

	// Thread-safe array of new timers to add to the running array
	Mutex _new_timers_lock;
	WorkerTimer *_new_timers;
	u32 _new_timers_count, _new_timers_allocated;

	// Array of running timers
	WorkerTimer *_timers;
	u32 _timers_count, _timers_allocated;

	void TickTimers(); // locks if needed

public:
	WorkerThread();
	virtual ~WorkerThread();

	CAT_INLINE u32 GetTimerCount() { return _timers_count + _new_timers_count; }
	CAT_INLINE void FlagEvent() { _event_flag.Set(); }
	CAT_INLINE void SetKillFlag() { _kill_flag = true; }

	void DeliverBuffers(u32 priority, const BatchSet &buffers);
	bool Associate(RefObject *object, WorkerTimerDelegate timer);
};


class CAT_EXPORT WorkerThreads
{
	friend class WorkerThread;

#if !defined(CAT_NO_ATOMIC_POPCOUNT)
	volatile u32 _population;
	u8 _padding[CAT_DEFAULT_CACHE_LINE_SIZE];
#endif // CAT_NO_ATOMIC_POPCOUNT

	u32 _tick_interval;

	u32 _worker_count;
	WorkerThread *_workers;

	u32 _round_robin_worker_id;

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

	CAT_INLINE void DeliverBuffers(u32 priority, u32 worker_id, const BatchSet &buffers)
	{
		_workers[worker_id].DeliverBuffers(priority, buffers);
	}

	CAT_INLINE void DeliverBuffersRoundRobin(u32 priority, const BatchSet &buffers)
	{
		// Yes to really insure fairness this should be synchronized,
		// but I am trying hard to eliminate locks everywhere and this
		// should still round-robin spin pretty well without locks.
		u32 worker_id = _round_robin_worker_id + 1;
		if (worker_id >= _worker_count) worker_id = 0;
		_round_robin_worker_id = worker_id;

		DeliverBuffers(priority, worker_id, buffers);
	}

	bool Startup(u32 worker_tick_interval, IWorkerTLSBuilder *tls_builder, u32 worker_count_override);

	bool Shutdown();

	CAT_INLINE u32 AssignWorker(RefObject *object, WorkerTimerDelegate timer)
	{
#if !defined(CAT_NO_ATOMIC_POPCOUNT)
		Atomic::Add(&_population, 1);
#endif // CAT_NO_ATOMIC_POPCOUNT

		u32 worker_id = FindLeastPopulatedWorker();

		if (!_workers[worker_id].Associate(object, timer))
			return INVALID_WORKER_ID;

		return worker_id;
	}
};


} // namespace cat

#endif // CAT_WORKER_THREADS_HPP
