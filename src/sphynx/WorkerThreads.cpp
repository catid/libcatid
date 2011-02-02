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

#include <cat/sphynx/WorkerThreads.hpp>
#include <cat/time/Clock.hpp>
#include <cat/port/SystemInfo.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;


//// WorkerTLS

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
	_rpc_head = _rpc_tail = 0;

	_new_workers_flag = false;
	_new_head = 0;

	_session_count = 0;
}

WorkerThread::~WorkerThread()
{
}

bool WorkerThread::ThreadFunction(void *vmaster)
{
	static const u32 TICK_INTERVAL = 20;

	WorkerThreads *master = (WorkerThreads*)vmaster;

	WorkerSession *head = 0, *tail = 0;
	u32 next_tick = Clock::msec();

	while (!_kill_flag)
	{
		// If event is waiting,
		if (_event_flag.Wait(TICK_INTERVAL))
		{
			// TODO: Handle events
		}

		u32 now = Clock::msec();

		// If tick interval is up,
		if ((s32)(now - next_tick) >= 0)
		{
			WorkerSession *node, *prev = 0, *next;

			// For each session,
			for (WorkerSession *node = head; node; node = next)
			{
				next = node->_next_worker;

				// If session is shutting down,
				if (node->IsShutdown())
				{
					// Unlink from list
					if (prev) prev->_next_worker = next;
					else head = next;
					if (!next) tail = prev;

					// Release reference
					node->ReleaseRef();

					_new_workers_lock.Enter();
					_session_count--;
					_new_workers_lock.Leave();
				}
				else
				{
					node->OnTick(now);

					prev = node;
				}
			}

			// Set up next tick
			next_tick += TICK_INTERVAL;

			// If next tick has already occurred,
			if ((s32)(now - next_tick) >= 0)
			{
				// Push off next tick one interval into the future
				next_tick = now + TICK_INTERVAL;

				WARN("WorkerThread") << "Slow worker tick";
			}
		}

		// If new workers have been added,
		if (_new_workers_flag)
		{
			WorkerSession *new_head;

			_new_workers_lock.Enter();

			new_head = _new_head;
			_new_head = 0;
			_new_workers_flag = false;

			_new_workers_lock.Leave();

			// Insert at end of linked worker list
			if (tail) tail->_next_worker = new_head;
			else head = new_head;

			tail = new_head;
		}
	}
}

void WorkerThread::Add(WorkerSession *session)
{
	_new_workers_lock.Enter();

	++_session_count;
	session->_next_worker = _new_head;
	_new_head = session;
	_new_workers_flag = true;

	_new_workers_lock.Leave();

	// Add reference to session
	session->AddRef();
}

void WorkerThread::QueueRecvFrom(OverlappedRecvFrom *ov)
{
	ov->next = 0;

	_rpc_lock.Enter();

	OverlappedRecvFrom *tail = _rpc_tail;
	if (tail) tail->next = ov;
	else _rpc_head = ov;

	_rpc_tail = ov;

	_rpc_lock.Leave();
}


//// WorkerThreads

WorkerThreads::WorkerThreads()
{
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

bool WorkerThreads::Associate(WorkerSession *session)
{

}
