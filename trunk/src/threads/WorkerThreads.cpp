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

#include <cat/threads/WorkerThreads.hpp>
#include <cat/time/Clock.hpp>
#include <cat/port/SystemInfo.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;

WorkerThread::WorkerThread()
{
	_kill_flag = false;

	_new_head = 0;

	_session_count = 0;

	for (u32 ii = 0; ii < WQPRIO_COUNT; ++ii)
	{
		_workqueues[ii].queued.Clear();
	}
}

WorkerThread::~WorkerThread()
{
}

void WorkerThread::Associate(IWorkerTimer *callbacks)
{
	_new_workers_lock.Enter();

	++_session_count;
	callbacks->_worker_next = _new_head;
	_new_head = callbacks;

	_new_workers_lock.Leave();

	callbacks->_parent->AddRef();
}

void WorkerThread::DeliverBuffers(u32 priority, const BatchSet &buffers)
{
	_workqueues[priority].lock.Enter();
	_workqueues[priority].queued.PushBack(buffers);
	_workqueues[priority].lock.Leave();

	_event_flag.Set();
}

static CAT_INLINE void ExecuteWorkQueue(IWorkerTLS *tls, const BatchSet &queue)
{
	// If there is nothing in the queue,
	if (!queue.head) return;

#if defined(CAT_WORKER_THREADS_REORDER_EVENTS)

	/*
		This version performs a median filter on the events in the queue
		so that events that specify the same callback tend to be batched
		together even when other callbacks also speckle the input queue.
		This improves overall throughput but causes re-ordering.

		For a network workload the speckles would be intermittent messages
		such as time synchronization pings or other events that should get
		a low-latency treatment.  And the large batch that is de-speckled
		would be file transfer or some other flood of bulk packets that are
		not so important to handle with low latency.  So the median filter
		is essentially helping prioritize packets properly.
	*/

	BatchSet buffers;
	buffers.Clear();

	WorkerBuffer *last = reinterpret_cast<WorkerBuffer*>( queue.head );

	for (;;)
	{
		WorkerBuffer *next = reinterpret_cast<WorkerBuffer*>( last->batch_next );

		if (!next) break;

		if (last->callback != next->callback)
		{
			WorkerBuffer *buffer_head = reinterpret_cast<WorkerBuffer*>( buffers.head );

			if (buffer_head)
			{
				if (buffer_head->callback == next->callback)
				{
					// run last now
					last->callback(tls, last);
				}
				else
				{
					// run lastlast now
					buffer_head->callback(tls, buffers);

					// start new batch with last
					buffers = last;
				}
			}
		}
		else
		{
			// add last to batch
			buffers.PushBack(last);
		}

		last = next;
	}

	// Run final batch
	if (last)
	{
		buffers.PushBack(last);

		last->callback(tls, buffers);
	}

#else // CAT_WORKER_THREADS_REORDER_EVENTS

	BatchSet buffers;
	buffers.head = queue.head;

	WorkerBuffer *last = reinterpret_cast<WorkerBuffer*>( queue.head );

	for (;;)
	{
		WorkerBuffer *next = reinterpret_cast<WorkerBuffer*>( last->batch_next );

		if (!next || next->callback != last->callback)
		{
			// Close out the previous buffer group
			buffers.tail = last;
			last->batch_next = 0;

			last->callback(tls, buffers);

			if (!next) break;

			// Start a new one
			buffers.head = next;
		}

		last = next;
	}

#endif // CAT_WORKER_THREADS_REORDER_EVENTS
}

bool WorkerThread::ThreadFunction(void *vmaster)
{
	WorkerThreads *master = (WorkerThreads*)vmaster;

	IWorkerTLS *tls = master->_tls_builder->Build();
	if (!tls || !tls->Valid())
	{
		FATAL("WorkerThread") << "Failure building thread local storage";
		return false;
	}

	u32 tick_interval = master->_tick_interval;
	u32 next_tick = 0; // Tick right away

	IWorkerTimer *head = 0, *tail = 0;

	while (!_kill_flag)
	{
		u32 now = Clock::msec();

		// Check if an event is waiting or the timer interval is up
		bool check_events = false;

		for (u32 ii = 0; ii < WQPRIO_COUNT; ++ii)
		{
			if (_workqueues[ii].queued.head)
			{
				check_events = true;
				break;
			}
		}

		// If no events occurred,
		if (!check_events)
		{
			u32 wait_time = next_tick - now;

			if ((s32)wait_time >= 0)
			{
				if (_event_flag.Wait(wait_time))
					check_events = true;

				now = Clock::msec();
			}
		}

		// Grab queue if event is flagged
		if (check_events)
		{
			for (u32 ii = 0; ii < WQPRIO_COUNT; ++ii)
			{
				if (_workqueues[ii].queued.head)
				{
					_workqueues[ii].lock.Enter();
					BatchSet queue = _workqueues[ii].queued;
					_workqueues[ii].queued.Clear();
					_workqueues[ii].lock.Leave();

					ExecuteWorkQueue(tls, queue);
				} // end if queue seems full
			} // next priority level
		} // end if check_events

		// If tick interval is up,
		if ((s32)(now - next_tick) >= 0)
		{
			IWorkerTimer *prev = 0, *next;

			// For each session,
			for (IWorkerTimer *node = head; node; node = next)
			{
				next = node->_worker_next;

				// If session is shutting down,
				if (node->_parent->IsShutdown())
				{
					WARN("WorkerThreads") << "Removing shutdown worker " << node;

					// Unlink from list
					if (prev) prev->_worker_next = next;
					else head = next;
					if (!next) tail = prev;

					// Release reference
					node->_parent->ReleaseRef();

					_new_workers_lock.Enter();
					_session_count--;
					_new_workers_lock.Leave();

#if !defined(CAT_NO_ATOMIC_POPCOUNT)
					Atomic::Add(&master->_population, -1);
#endif // CAT_NO_ATOMIC_POPCOUNT
				}
				else
				{
					node->OnWorkerTick(tls, now);

					prev = node;
				}
			}

			// Set up next tick
			next_tick += tick_interval;

			// If next tick has already occurred,
			if ((s32)(now - next_tick) >= 0)
			{
				// Push off next tick one interval into the future
				next_tick = now + tick_interval;

				INANE("WorkerThread") << "Slow worker tick";
			}
		}

		// If new workers have been added,
		if (_new_head)
		{
			IWorkerTimer *new_head;

			_new_workers_lock.Enter();
			new_head = _new_head;
			_new_head = 0;
			_new_workers_lock.Leave();

			// Insert at end of linked worker list
			if (tail) tail->_worker_next = new_head;
			else head = new_head;

			tail = new_head;
		}
	}

	return true;
}


//// WorkerThreads

WorkerThreads::WorkerThreads()
{
	_population = 0;
	_worker_count = 0;
	_workers = 0;
	_tls_builder = 0;
	_round_robin_worker_id = 0;
}

WorkerThreads::~WorkerThreads()
{
	Shutdown();
}

bool WorkerThreads::Startup(u32 worker_tick_interval, IWorkerTLSBuilder *tls_builder, u32 worker_count_override)
{
	if (_worker_count)
		return false;

	_tick_interval = worker_tick_interval;
	_tls_builder = tls_builder;

	u32 worker_count = system_info.ProcessorCount;
	if (worker_count < 1) worker_count = 1;

	// If worker count override is set,
	if (worker_count_override != 0)
	{
		// Use it instead of the number of processors
		worker_count = worker_count_override;
	}

	_workers = new WorkerThread[worker_count];
	if (!_workers)
	{
		FATAL("WorkerThreads") << "Out of memory while allocating " << worker_count << " worker thread objects";
		return false;
	}

	_worker_count = worker_count;

	// For each worker,
	for (u32 ii = 0; ii < worker_count; ++ii)
	{
		// Start its thread
		if (!_workers[ii].StartThread(this))
		{
			FATAL("WorkerThreads") << "StartThread error " << GetLastError();
			return false;
		}

		_workers[ii].SetIdealCore(ii);
	}

	return true;
}

bool WorkerThreads::Shutdown()
{
	u32 worker_count = _worker_count;

	// For each worker,
	for (u32 ii = 0; ii < worker_count; ++ii)
	{
		// Set the kill flag
		_workers[ii].SetKillFlag();
	}

	const int SHUTDOWN_WAIT_TIMEOUT = 15000; // 15 seconds

	// For each worker thread,
	for (u32 ii = 0; ii < worker_count; ++ii)
	{
		if (!_workers[ii].WaitForThread(SHUTDOWN_WAIT_TIMEOUT))
		{
			FATAL("WorkerThreads") << "Thread " << ii << "/" << worker_count << " refused to die!  Attempting lethal force...";
			_workers[ii].AbortThread();
		}
	}

	_worker_count = 0;

	// Free worker thread objects
	if (_workers)
	{
		delete []_workers;
		_workers = 0;
	}

	// Free TLS builder object
	if (_tls_builder)
	{
		delete _tls_builder;
		_tls_builder = 0;
	}

	return true;
}

u32 WorkerThreads::FindLeastPopulatedWorker()
{
	u32 lowest_session_count = _workers[0].GetSessionCount();
	u32 worker_id = 0;

	// For each other worker,
	for (u32 ii = 1, worker_count = _worker_count; ii < worker_count; ++ii)
	{
		u32 session_count = _workers[ii].GetSessionCount();

		// If the session count is lower,
		if (session_count < lowest_session_count)
		{
			// Use this one
			lowest_session_count = session_count;
			worker_id = ii;
		}
	}

	return worker_id;
}

#if defined(CAT_NO_ATOMIC_POPCOUNT)

u32 WorkerThreads::GetTotalPopulation()
{
	u32 session_count = _workers[0].GetSessionCount();

	// For each other worker,
	for (u32 ii = 1, worker_count = _worker_count; ii < worker_count; ++ii)
	{
		session_count += _workers[ii].GetSessionCount();
	}

	return session_count;
}

#endif // CAT_NO_ATOMIC_POPCOUNT
