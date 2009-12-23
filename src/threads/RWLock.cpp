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

#include <cat/threads/RWLock.hpp>
#include <cat/threads/Atomic.hpp>
using namespace cat;

//// RWLock

RWLock::RWLock()
{
	_rd_request_count = 0;
	_rd_allow = 1;
	_rd_enable_count = 0;
	_wr_request = 0;
	_wr_allow = 1;
	_wr_enabled = 0;

	// Events require ResetEvent() for a reset and are initially reset
	_wr_event = CreateEvent(0, TRUE, FALSE, 0);
	_rd_event = CreateEvent(0, TRUE, FALSE, 0);
}

RWLock::~RWLock()
{
	CloseHandle(_wr_event);
	CloseHandle(_rd_event);
}

void RWLock::ReadLock()
{
	for (;;)
	{
		// If fast path may be available,
		if (_rd_allow)
		{
			bool first = (Atomic::Add(&_rd_enable_count, 1) == 0);

			if (_rd_allow)
			{
				// Fast path success: Established read lock without using kernel objects
				return;
			}
			else
			{
				// Fast path failure: Block on kernel object

				if (Atomic::Add(&_rd_enable_count, -1) == 1)
				{
					// Last failed fast path
				}
				else
				{
					// Not last failed fast path
				}
			}
		}
		else
		{
			// Fast path unavailable: Block on kernel object
		}

		// TODO: Pause
	}
}

void RWLock::ReadUnlock()
{
	// If this is the last read unlock,
	if (Atomic::Add(&_rd_enable_count, -1) == 1)
	{
		// Last failed fast path
	}
	else
	{
		// Not last failed fast path
	}
}

void RWLock::WriteLock()
{
	_wr_lock.Enter();

	_rd_allow = 0;

	CAT_FENCE_COMPILER

	while (_rd_enable_count > 0)
	{
		// TODO: Pause
	}
}

void RWLock::WriteUnlock()
{
	_rd_allow = 1;

	_wr_lock.Leave();
}


//// AutoReadLock

AutoReadLock::AutoReadLock(RWLock &lock)
{
	_lock = &lock;

	lock.ReadLock();
}

AutoReadLock::~AutoReadLock()
{
	if (_lock)
		_lock->ReadUnlock();
}

bool AutoReadLock::Release()
{
	_lock = 0;
	return true;
}


//// AutoWriteLock

AutoWriteLock::AutoWriteLock(RWLock &lock)
{
	_lock = &lock;

	lock.WriteLock();
}

AutoWriteLock::~AutoWriteLock()
{
	if (_lock)
		_lock->WriteUnlock();
}

bool AutoWriteLock::Release()
{
	_lock = 0;
	return true;
}
