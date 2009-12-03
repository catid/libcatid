/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#include <cat/threads/LoopThread.hpp>
#include <cat/time/Clock.hpp>
using namespace cat;

#if defined(CAT_THREAD_WINDOWS)

#include <process.h>

unsigned int __stdcall LoopThread::ThreadWrapper(void *this_object)
{
	LoopThread *thread_object = static_cast<LoopThread*>( this_object );

	bool error = thread_object->ThreadFunction(thread_object->caller_param);

	// Using _beginthreadex() and _endthreadex() since _endthread() calls CloseHandle()
	_endthreadex(0);

	return error ? 1 : 0;
}

#elif defined(CAT_THREAD_POSIX)

void *LoopThread::ThreadWrapper(void *this_object)
{
	LoopThread *thread_object = static_cast<LoopThread*>( this_object );

	bool error = thread_object->ThreadFunction(thread_object->caller_param);

	return (void*)(error ? 1 : 0);
}

#endif

//// LoopThread

LoopThread::LoopThread()
{
#if defined(CAT_THREAD_WINDOWS)

	_quit_signal = 0;
	_thread = 0;

#elif defined(CAT_THREAD_POSIX)

	_quit_signal = false;
	_thread_started = false;

#endif
}

LoopThread::~LoopThread()
{
	StopThread();
}

bool LoopThread::StartThread(void *param)
{
	caller_param = param;

#if defined(CAT_THREAD_WINDOWS)

	if (_quit_signal || _thread)
		return false;

	// Create an event to signal when the entropy collection thread should terminate
	_quit_signal = CreateEvent(0, FALSE, FALSE, 0);
	if (!_quit_signal) return false;

	// Using _beginthreadex() and _endthreadex() since _endthread() calls CloseHandle()
	_thread = (HANDLE)_beginthreadex(0, 0, &LoopThread::ThreadWrapper, static_cast<void*>( this ), 0, 0);
	return _thread != 0;

#elif defined(CAT_THREAD_POSIX)

	if (_thread_started)
		return false;

	_thread_started = true;
	_quit_signal = false;
	if (pthread_create(&_thread, 0, &LoopThread::ThreadWrapper, static_cast<void*>( this )))
	{
		_thread_started = false;
		return false;
	}

	return true;

#endif
}

bool LoopThread::StopThread()
{
	bool success = false;

#if defined(CAT_THREAD_WINDOWS)

	if (_thread && _quit_signal)
	{
		// Signal termination event and block waiting for thread to signal termination
		if (SetEvent(_quit_signal) &&
			WaitForSingleObject(_thread, INFINITE) != WAIT_FAILED)
		{
			success = true;
		}
	}

	if (_quit_signal)
	{
		if (!CloseHandle(_quit_signal))
			success = false;
		_quit_signal = 0;
	}

	if (_thread)
	{
		if (!CloseHandle(_thread))
			success = false;
		_thread = 0;
	}

#elif defined(CAT_THREAD_POSIX)

	if (_thread_started)
	{
		_thread_started = false;
		_quit_signal = true;

		if (pthread_join(_thread, 0) == 0)
			success = true;
	}

#endif

	return success;
}

// Returns false if it is time to quit
bool LoopThread::WaitForQuitSignal(int msec)
{
#if defined(CAT_THREAD_WINDOWS)

	switch (WaitForSingleObject(_quit_signal, msec))
	{
	case WAIT_FAILED: // Signal object has been destroyed unexpectedly
	case WAIT_ABANDONED: // LoopThread creator has quit unexpectedly
	case WAIT_OBJECT_0: // Signaled
	default: // Unexpected error
		return false;
	case WAIT_TIMEOUT: // Not signaled
		return true;
	}

#elif defined(CAT_THREAD_POSIX)

	// I would prefer something like WaitForSingleObject()
	if (_quit_signal) return false;
	Clock::sleep(msec);
	return !_quit_signal;

#endif
}
