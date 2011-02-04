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

#include <cat/net/WorkerThreads.hpp>
#include <cat/time/Clock.hpp>
#include <cat/port/SystemInfo.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;

WorkerTLS::WorkerTLS()
{
	// Create 256-bit math library instance
	math = KeyAgreementCommon::InstantiateMath(256);

	// Create CSPRNG instance
	csprng = FortunaFactory::ref()->Create();
}

WorkerTLS::~WorkerTLS()
{
	if (math) delete math;
	if (csprng) delete csprng;
}

bool WorkerTLS::Valid()
{
	return math && csprng;
}


//// WorkerThread

WorkerThread::WorkerThread()
{
	_kill_flag = false;
	_workqueue.head = _workqueue.tail = 0;

	_new_head = 0;

	_session_count = 0;
}

WorkerThread::~WorkerThread()
{
}

void WorkerThread::Associate(WorkerCallbacks *callbacks)
{
	_new_workers_lock.Enter();

	++_session_count;
	callbacks->_worker_next = _new_head;
	_new_head = callbacks;

	_new_workers_lock.Leave();

	callbacks->_parent->AddRef();
}

void WorkerThread::DeliverBuffers(const BatchSet &buffers)
{
	_workqueue_lock.Enter();

	if (_workqueue.tail) _workqueue.tail->batch_next = buffers.head;
	else _workqueue.head = buffers.head;
	_workqueue.tail = buffers.tail;

	_workqueue_lock.Leave();
}

bool WorkerThread::ThreadFunction(void *vmaster)
{
	WorkerThreads *master = (WorkerThreads*)vmaster;

	WorkerTLS tls;
	if (!tls.Valid())
	{
		FATAL("WorkerThread") << "Out of memory initializing thread local storage";
		return false;
	}

	WorkerCallbacks *head = 0, *tail = 0;
	u32 next_tick = Clock::msec();

	while (!_kill_flag)
	{
		// If event is waiting,
		if (_workqueue.head != 0 || _event_flag.Wait(WORKER_TICK_INTERVAL))
		{
			// Grab queue if event is flagged
			_workqueue_lock.Enter();
			BatchSet queue = _workqueue;
			_workqueue.head = _workqueue.tail = 0;
			_workqueue_lock.Leave();

			// If there is anything in the queue,
			if (queue.head)
			{
				BatchSet buffers;
				buffers.head = queue.head;

				RecvBuffer *last = reinterpret_cast<RecvBuffer*>( queue.head );

				while (last)
				{
					RecvBuffer *next = reinterpret_cast<RecvBuffer*>( last->batch_next );

					if (!next || next->callback != last->callback)
					{
						// Close out the previous buffer group
						buffers.tail = last;
						last->batch_next = 0;

						last->callback->OnWorkerRead(&tls, buffers);

						// Start a new one
						buffers.head = next;
					}

					last = next;
				}
			}
		}

		u32 now = Clock::msec();

		// If tick interval is up,
		if ((s32)(now - next_tick) >= 0)
		{
			WorkerCallbacks *node, *prev = 0, *next;

			// For each session,
			for (WorkerCallbacks *node = head; node; node = next)
			{
				next = node->_worker_next;

				// If session is shutting down,
				if (node->_parent->IsShutdown())
				{
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
					node->OnWorkerTick(&tls, now);

					prev = node;
				}
			}

			// Set up next tick
			next_tick += WORKER_TICK_INTERVAL;

			// If next tick has already occurred,
			if ((s32)(now - next_tick) >= 0)
			{
				// Push off next tick one interval into the future
				next_tick = now + WORKER_TICK_INTERVAL;

				WARN("WorkerThread") << "Slow worker tick";
			}
		}

		// If new workers have been added,
		if (_new_head)
		{
			WorkerCallbacks *new_head;

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
}


//// WorkerThreads

WorkerThreads::WorkerThreads()
{
	_population = 0;
	_worker_count = 0;
	_workers = 0;
}

WorkerThreads::~WorkerThreads()
{
	Shutdown();
}

bool WorkerThreads::Startup()
{
	if (_worker_count)
		return true;

	u32 worker_count = system_info.ProcessorCount;
	if (worker_count < 1) worker_count = 1;

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
