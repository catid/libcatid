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

#include <cat/threads/LoopThread.hpp>
#include <cat/time/Clock.hpp>
using namespace cat;


//// LoopThread

LoopThread::LoopThread()
{
#if defined(CAT_OS_WINDOWS)
	_quit_signal = 0;
#else
	_quit_signal = false;
#endif
}

LoopThread::~LoopThread()
{
	WaitForThread();
}

bool LoopThread::StartThread(void *param)
{
	if (_thread_running)
		return false;

#if defined(CAT_OS_WINDOWS)

	// Create an event to signal when the entropy collection thread should terminate
	_quit_signal = CreateEvent(0, FALSE, FALSE, 0);
	if (!_quit_signal) return false;

#else

	_quit_signal = false;

#endif

	return Thread::StartThread(param);
}

bool LoopThread::WaitForThread()
{
	bool success = false;

#if defined(CAT_OS_WINDOWS)

	// Signal termination event
	if (_quit_signal)
		SetEvent(_quit_signal);

#else

	_quit_signal = true;

#endif

	success = Thread::WaitForThread();

#if defined(CAT_OS_WINDOWS)

	if (_quit_signal)
	{
		if (!CloseHandle(_quit_signal))
			success = false;
		_quit_signal = 0;
	}

#endif

	return success;
}

// Returns false if it is time to quit
bool LoopThread::WaitForQuitSignal(int msec)
{
#if defined(CAT_OS_WINDOWS)

	switch (WaitForSingleObject(_quit_signal, msec))
	{
	case WAIT_FAILED:		// Signal object has been destroyed unexpectedly
	case WAIT_ABANDONED:	// LoopThread creator has quit unexpectedly
	case WAIT_OBJECT_0:		// Signaled
	default:				// Unexpected error
		return false;
	case WAIT_TIMEOUT:		// Not signaled
		return true;
	}

#else

	// I would prefer something like WaitForSingleObject()
	if (_quit_signal) return false;
	Clock::sleep(msec);
	return !_quit_signal;

#endif
}
