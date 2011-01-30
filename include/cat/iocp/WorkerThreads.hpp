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

#include <cat/threads/Thread.hpp>
#include <cat/threads/RefObject.hpp>
#include <cat/threads/WaitableFlag.hpp>
#include <cat/threads/Mutex.hpp>

namespace cat {


class WorkerThread;
class WorkerThreads;

// Base class for sessions
class WorkerSession : public RefObject
{
	friend class WorkerThread;

	WorkerSession *_next_worker;

public:
	CAT_INLINE virtual ~WorkerSession() {}

	virtual void OnTick(u32 now) = 0;
};


// Worker thread
class WorkerThread : public Thread
{
	virtual bool ThreadFunction(void *master);

	u32 _session_count;

	WaitableFlag _event_flag;
	volatile bool _kill_flag;

	// Protected list of new workers to add to the running list
	volatile bool _new_workers_flag;
	Mutex _new_workers_lock;
	WorkerSession *_new_head;

public:
	WorkerThread();
	virtual ~WorkerThread();

	CAT_INLINE u32 GetSessionCount() { return _session_count; }
	CAT_INLINE void FlagEvent() { _event_flag.Set(); }
	CAT_INLINE void SetKillFlag() { _kill_flag = true; }

	void Add(WorkerSession *session);
};


// Worker threads
class WorkerThreads
{
	u32 _worker_count;
	WorkerThread *_workers;

public:
	WorkerThreads();
	virtual ~WorkerThreads();

	bool Startup();
	bool Shutdown();
	bool Associate(WorkerSession *session);
};


} // namespace cat

#endif // CAT_WORKER_THREADS_HPP
