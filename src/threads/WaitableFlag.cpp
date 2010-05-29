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

#include <cat/threads/WaitableFlag.hpp>
#include <cat/time/Clock.hpp>
using namespace cat;

#if !defined(CAT_OS_WINDOWS)
#include <sys/time.h> // gettimeofday
#include <errno.h> // ETIMEDOUT
#endif

WaitableFlag::WaitableFlag()
{
#if defined(CAT_OS_WINDOWS)

	_event = CreateEvent(0, FALSE, FALSE, 0);

#else // CAT_OS_WINDOWS

	_flag = 0;
	_valid = false;
	_valid_cond = false;
	_valid_mutex = false;
# if defined(CAT_NO_ATOMIC_SET)
	_valid_atomic = false;

	_valid_atomic = pthread_mutex_init(&_atomic, 0) == 0;
	if (!_valid_atomic) return;
# endif

	_valid_cond = pthread_cond_init(&_cond, 0) == 0;
	if (!_valid_cond) return;

	_valid_mutex = pthread_mutex_init(&_mutex, 0) == 0;
	if (!_valid_mutex) return;

	_valid = true;

#endif // CAT_OS_WINDOWS
}

void WaitableFlag::Cleanup()
{
#if defined(CAT_OS_WINDOWS)

	if (_event)
	{
		CloseHandle(_event);
		_event = 0;
	}

#else // CAT_OS_WINDOWS

	if (_valid_cond)
	{
		pthread_cond_destroy(&_cond);
		_valid_cond = false;
	}

# if defined(CAT_NO_ATOMIC_SET)
	if (_valid_atomic)
	{
		pthread_mutex_destroy(&_atomic);
		_valid_atomic = false;
	}
# endif

	if (_valid_mutex)
	{
		pthread_mutex_destroy(&_mutex);
		_valid_mutex = false;
	}

#endif // CAT_OS_WINDOWS
}

bool WaitableFlag::Set()
{
#if defined(CAT_OS_WINDOWS)

	if (_event)
	{
		return SetEvent(_event) == TRUE;
	}

#else // CAT_OS_WINDOWS

	if (_valid)
	{
# if defined(CAT_NO_ATOMIC_SET)
		pthread_mutex_lock(&_atomic);

		CAT_FENCE_COMPILER

		_flag = 1;

		CAT_FENCE_COMPILER

		pthread_mutex_unlock(&_atomic);
# else
		Atomic::Set(&_flag, 1);
# endif

		return pthread_cond_broadcast(&_cond) == 0;
	}

#endif // CAT_OS_WINDOWS

	return false;
}

bool WaitableFlag::Wait(int milliseconds)
{
#if defined(CAT_OS_WINDOWS)

	if (_event == 0) return false;

	return WaitForSingleObject(_event, (milliseconds >= 0) ? milliseconds : INFINITE) != WAIT_TIMEOUT;

#else // CAT_OS_WINDOWS

	if (!_valid) return false;

# if defined(CAT_NO_ATOMIC_SET)
	pthread_mutex_lock(&_atomic);

	CAT_FENCE_COMPILER

	u32 flagged = _flag;
	_flag = 0;

	CAT_FENCE_COMPILER

	pthread_mutex_unlock(&_atomic);

	// If flag was already set, return immediately with success
	if (flagged != 0)
		return true;
# else
	// If flag was already set, return immediately with success
	if (Atomic::Set(&_flag, 0) == 1)
		return true;
# endif

	// If polling,
	if (milliseconds == 0)
		return false; // Wait failed since we were unflagged

	// Lock _mutex as required by pthread_cond_*
	pthread_mutex_lock(&_mutex);

	// If waiting forever,
	if (milliseconds < 0)
	{
		return pthread_cond_wait(&_cond, &_mutex) == 0;
	}
	else
	{
		int interval_seconds = milliseconds / 1000; // get interval seconds
		long interval_nanoseconds = (milliseconds % 1000) * 1000000; // get interval nanoseconds

		struct timeval tv;
		if (gettimeofday(&tv, 0) != 0)
			return false;

		long nsec = tv.tv_usec;
		if (nsec < 0) return false;

		long nsec_trigger = nsec + interval_nanoseconds;

		static const long ONE_SECOND_IN_NANOSECONDS = 1000000000;

		if (nsec_trigger < nsec || nsec_trigger >= ONE_SECOND_IN_NANOSECONDS)
		{
			++interval_seconds;
			nsec_trigger -= ONE_SECOND_IN_NANOSECONDS;
		}

		struct timespec ts;
		ts.tv_sec = tv.tv_sec + interval_seconds;
		ts.tv_nsec = nsec_trigger;

		return pthread_cond_timedwait(&_cond, &_mutex, &ts) != ETIMEDOUT;
	}

#endif // CAT_OS_WINDOWS
}
