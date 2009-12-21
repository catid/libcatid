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

RWLock::RWLock()
{
	_wr_lock_count = 0;
	_rd_lock_count = 0;

	_wr_unlock_event = CreateEvent(0, FALSE, FALSE, 0);
	_rd_unlock_event = CreateEvent(0, FALSE, FALSE, 0);
}

RWLock::~RWLock()
{
	CloseHandle(_wr_unlock_event);
	CloseHandle(_rd_unlock_event);
}

void RWLock::ReadLock()
{
	Atomic::Add(&_rd_lock_count, 1);

	u32 count = _wr_lock_count;

	CAT_FENCE_COMPILER

	if (count)
	{
		WaitForSingleObject(_wr_unlock_event, INFINITE);
	}
}

void RWLock::ReadUnlock()
{
	if (Atomic::Add(&_rd_lock_count, -1) == 1)
	{
		if (_wr_lock_count)
		{
			SetEvent(_rd_unlock_event);
		}
	}
}

void RWLock::WriteLock()
{
	_wr_lock.Enter();

	_wr_lock_count = 1;

	WaitForSingleObject(_rd_unlock_event, INFINITE);
}

void RWLock::WriteUnlock()
{
	_wr_lock_count = 0;

	CAT_FENCE_COMPILER

	SetEvent(_wr_unlock_event);

	_wr_lock.Leave();
}
