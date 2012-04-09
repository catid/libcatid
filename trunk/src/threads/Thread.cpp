/*
	Copyright (c) 2009-2012 Christopher A. Taylor.  All rights reserved.

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

#include <cat/threads/Thread.hpp>
#include <cat/time/Clock.hpp>
using namespace cat;


//// Thread priority modification

bool cat::SetExecPriority(ThreadPrio prio)
{
#if defined(CAT_OS_WINDOWS)
	int level;

	// Convert to Windows API constant
	switch (prio)
	{
	case P_IDLE:	level = THREAD_PRIORITY_IDLE;	break;
	case P_LOW:		level = THREAD_PRIORITY_LOWEST;	break;
	case P_NORMAL:	level = THREAD_PRIORITY_NORMAL;	break;
	case P_HIGH:	level = THREAD_PRIORITY_ABOVE_NORMAL;	break;
	case P_HIGHEST:	level = THREAD_PRIORITY_HIGHEST;	break;
	}

	return 0 != ::SetThreadPriority(::GetCurrentThread(), level);
#else
	return false;
#endif
}


//// GetThreadID

#if !defined(CAT_OS_WINDOWS)
# include <unistd.h>
# include <sys/syscall.h>
# if defined(CAT_OS_LINUX)
#  include <linux/unistd.h>
# elif defined(CAT_OS_BSD) || defined(CAT_OS_OSX)
#  include <bsd/unistd.h>
# endif
#endif

u32 cat::GetThreadID()
{
#if defined(CAT_OS_WINDOWS)

	return GetCurrentThreadId();

#elif defined(CAT_OS_LINUX) || defined(CAT_OS_BSD) || defined(CAT_OS_OSX)

	s32 thread_id = syscall(__NR_gettid);
	if (thread_id != -1) return thread_id;

	return getpid();

#else

	return (u32)pthread_self();

#endif
}


//// TLSClaim

CAT_SINGLETON(TLSClaim);

bool TLSClaim::OnInitialize()
{
	_next_index = 0;
	return true;
}

u32 TLSClaim::Claim(const char *key_name)
{
	SanitizedKey skey(key_name);
	KeyAdapter key(skey);

	AutoMutex lock(_lock);

	HashItem *item = _map.Lookup(key);
	u32 item_index;

	// If key not found,
	if (!item)
	{
		item = _map.Create(key);

		item_index = _next_index++;

		if (item)
		{
			// TODO: Use binary instead of string here when I use this for something performance-critical
			item->SetValueInt(item_index);
		}
	}
	else
	{
		item_index = item->GetValueInt();
	}

	return item_index;
}


//// SlowThreadLocalStorage

CAT_REF_SINGLETON(SlowTLS);

bool SlowTLS::OnInitialize()
{
	_map = new HashTable<TLSItem>;
	if (!_map) return false;

	return true;
}

void SlowTLS::OnFinalize()
{
	if (_map)
	{
		delete _map;
		_map = 0;
	}
}

ThreadLocalStorage *SlowTLS::Get()
{
	u32 tid = GetThreadID();
	KeyAdapter key((const char*)&tid, sizeof(tid), tid);

	AutoMutex lock(_lock);

	TLSItem *item = _map->Lookup(key);

	if (!item)
	{
		item = _map->Create(key);
		if (!item) return 0;
	}

	return &item->tls;
}


//// Thread

#if defined(CAT_OS_WINDOWS)

#include <process.h>

unsigned int __stdcall Thread::ThreadWrapper(void *this_object)
{
	Thread *thread_object = reinterpret_cast<Thread*>( this_object );

	bool success = thread_object->Entrypoint(thread_object->_caller_param);

	unsigned int exitCode = success ? 0 : 1;

	CAT_DEBUG_CHECK_MEMORY();

	// Invoke any thread-atexit() callbacks
	thread_object->InvokeAtExit();

	CAT_DEBUG_CHECK_MEMORY();

	CAT_FENCE_COMPILER;

	thread_object->_thread_running = false;

	CAT_FENCE_COMPILER;

	// Using _beginthreadex() and _endthreadex() since _endthread() calls CloseHandle()
	_endthreadex(exitCode);

	// Should not get here
	return exitCode;
}

#else

void *Thread::ThreadWrapper(void *this_object)
{
	Thread *thread_object = static_cast<Thread*>( this_object );

	bool success = thread_object->Entrypoint(thread_object->_caller_param);

	CAT_DEBUG_CHECK_MEMORY();

	thread_object->_thread_running = false;

	// Invoke any thread-atexit() callbacks
	thread_object->InvokeAtExit();

	CAT_DEBUG_CHECK_MEMORY();

	return (void*)(success ? 0 : 1);
}

#endif


//// Thread

Thread::Thread()
{
	_thread_running = false;
	_cb_count = 0;
}

bool Thread::StartThread(void *param)
{
	if (_thread_running)
		return false;

	_caller_param = param;
	_thread_running = true;

#if defined(CAT_OS_WINDOWS)

	// Using _beginthreadex() and _endthreadex() since _endthread() calls CloseHandle()
	u32 thread_id;
	_thread = (HANDLE)_beginthreadex(0, 0, &Thread::ThreadWrapper, static_cast<void*>( this ), 0, &thread_id);

	if (!_thread)
		_thread_running = false;

	return _thread != 0;

#else

	_thread_running = true;
	if (pthread_create(&_thread, 0, &Thread::ThreadWrapper, static_cast<void*>( this )))
	{
		_thread_running = false;
		return false;
	}

	return true;

#endif
}

void Thread::SetIdealCore(u32 index)
{
#if defined(CAT_OS_WINDOWS)

	if (_thread)
		SetThreadIdealProcessor(_thread, index);

#endif
}

void Thread::AbortThread()
{
	if (!_thread_running)
		return;

#if defined(CAT_OS_WINDOWS)

	if (_thread)
	{
		DWORD ExitCode;

		// If we can get an exit code for the thread,
		if (GetExitCodeThread(_thread, &ExitCode) != 0)
		{
			TerminateThread(_thread, ExitCode);
		}

		CloseHandle(_thread);
		_thread = 0;
	}

#else

	pthread_cancel(_thread);

#endif

	_thread_running = false;
}

bool Thread::WaitForThread(int ms)
{
	if (!_thread_running)
		return true;

	bool success = false;

#if defined(CAT_OS_WINDOWS)

	if (_thread)
	{
		// Signal termination event and block waiting for thread to signal termination
		if (WaitForSingleObject(_thread, (ms >= 0) ? ms : INFINITE) != WAIT_FAILED)
		{
			success = true;

			CloseHandle(_thread);
			_thread = 0;
		}
	}

#else

	// TODO: No way to specify a wait timeout for POSIX threads?
	if (pthread_join(_thread, 0) == 0)
		success = true;

#endif

	if (success)
		_thread_running = false;

	return success;
}

bool Thread::AtExit(const AtExitCallback &cb)
{
	// If no more room,
	if (_cb_count >= MAX_CALLBACKS)
	{
		CAT_FATAL("Thread") << "Too many thread-atexit() callbacks";
		return false;
	}

	// Insert at end of callbacks array
	_callbacks[_cb_count++] = cb;
	return true;
}

void Thread::InvokeAtExit()
{
	// For each callback,
	for (int ii = 0, count = _cb_count; ii < count; ++ii)
	{
		// Invoke it
		_callbacks[ii]();
	}

	// Invoke TLS finalization callbacks
	for (int ii = 0; ii < MAX_TLS_BINS; ++ii)
	{
		ITLS *tls = _tls[ii];
		if (tls)
		{
			tls->OnFinalize();
			delete tls;
		}
	}
}
