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

#ifndef CAT_LOOP_THREAD_HPP
#define CAT_LOOP_THREAD_HPP

#include <cat/threads/Thread.hpp>


namespace cat {


/*
	A thread that spins in a loop, waiting for a signal to exit.

	Derive from this class and implement ThreadFunction().

	ThreadFunction() should call WaitForQuitSignal() in the loop.
*/
class LoopThread : public Thread
{
#if defined(CAT_OS_WINDOWS)
	HANDLE _quit_signal;
#else
	volatile bool _quit_signal;
#endif

public:
	bool StartThread(void *param = 0);
	bool WaitForThread();

protected:
	// Returns false if it is time to quit, pass msec=0 to poll without waiting
	bool WaitForQuitSignal(int msec = 0);

	virtual bool ThreadFunction(void *param) = 0;

public:
    LoopThread();
    virtual ~LoopThread();
};


} // namespace cat

#endif // CAT_LOOP_THREAD_HPP
