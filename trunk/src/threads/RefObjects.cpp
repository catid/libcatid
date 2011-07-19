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

#include <cat/threads/RefObjects.hpp>
#include <cat/threads/AutoMutex.hpp>
using namespace cat;


//// RefObjects Singleton

static RefObjects refobject_reaper;

RefObjects *RefObjects::ref()
{
	return &refobject_reaper;
}


//// RefObject

RefObject::RefObject()
{
	// Initialize shutdown flag
	_shutdown = 0;

	// Initialize to one reference
	_ref_count = 1;
}

void RefObject::Destroy(const char *file_line)
{
#if defined(CAT_TRACE_REFOBJECT)
	CAT_WARN("RefObject") << this << " destroyed at " << file_line;
#endif

	// Raise shutdown flag
#if defined(CAT_NO_ATOMIC_REF_OBJECT)
	u32 shutdown;

	_lock.Enter();
	shutdown = _shutdown;
	_shutdown = 1;
	_lock.Leave();

	if (shutdown == 0)
#else
	if (Atomic::Set(&_shutdown, 1) == 0)
#endif
	{
		// Notify the derived class on the first shutdown request
		OnDestroy();

		// Release the initial reference to allow Finalize()
		ReleaseRef(file_line);
	}
}

void RefObject::OnZeroReferences(const char *file_line)
{
#if defined(CAT_TRACE_REFOBJECT)
	CAT_WARN("RefObject") << this << " zero refs at " << file_line;
#endif

	refobject_reaper.Kill(this);
}


//// RefObjects

RefObjects::RefObjects()
{
	_active_head = _dead_head = 0;

	_shutdown = false;
}

bool RefObjects::Watch(const char *file_line, RefObject *obj)
{
	if (!obj) return false;

#if defined(CAT_TRACE_REFOBJECT)
	CAT_WARN("RefObjects") << obj << " acquired at " << file_line;
#endif

	AutoMutex lock(_lock);

	if (_shutdown)
	{
		lock.Release();

		delete obj;

		return false;
	}

	// Link to active list
	RefObject *old_head = _active_head;

	obj->_prev = 0;
	obj->_next = old_head;

	if (old_head) old_head->_prev = obj;

	_active_head = obj;

	return true;
}

void RefObjects::UnlinkFromActiveList(RefObject *obj)
{
	RefObject *next = obj->_next, *prev = obj->_prev;
	if (prev) prev->_next = obj;
	else _active_head = next;
	if (next) next->_prev = obj;
}

void RefObjects::LinkToDeadList(RefObject *obj)
{
	RefObject *old_head = _dead_head;

	obj->_prev = 0;
	obj->_next = old_head;

	if (old_head) old_head->_prev = obj;

	_dead_head = obj;
}

void RefObjects::Kill(RefObject *obj)
{
	AutoMutex lock(_lock);

	// Skip if shutdown
	if (_shutdown) return;

	UnlinkFromActiveList(obj);

	LinkToDeadList(obj);
}

bool RefObjects::Initialize()
{
	if (!Thread::StartThread())
	{
		CAT_FATAL("RefObjects") << "Unable to start reaper thread";
		return false;
	}

	return true;
}

bool RefObjects::Shutdown(s32 milliseconds)
{
	_shutdown_flag.Set();

	return Thread::WaitForThread(milliseconds);
}

void RefObjects::BuryDeadites()
{
	if (!_dead_head) return;

	RefObject *dead_head;

	// Copy dead list
	_lock.Enter();
	dead_head = _dead_head;
	_dead_head = 0;
	_lock.Leave();

	for (RefObject *next, *obj = dead_head; obj; obj = next)
	{
		next = obj->_next;

		if (obj->OnFinalize())
			delete obj;
	}
}

bool RefObjects::ThreadFunction(void *param)
{
	while (!_shutdown_flag.Wait(513))
	{
		BuryDeadites();
	}

	_lock.Enter();

	// Set shutdown flag
	_shutdown = true;

	// Now the shutdown flag is set everywhere synchronously.
	// The lists may only be modified from this function.

	_lock.Leave();

	// For each remaining active object,
	for (RefObject *obj = _active_head; obj; obj = obj->_next)
	{
		obj->Destroy(CAT_REFOBJECT_FILE_LINE);
	}

	// Bury any easy dead
	BuryDeadites();

	// While active objects still exist,
	RefObject *head = _active_head;
	while (head)
	{
		if (head->_ref_count == 0)
		{
			RefObject *next = head->_next;

			if (head->OnFinalize())
				delete head;

			head = next;
		}
		else
		{
			// Give it some time
			Clock::sleep(10);
		}
	}

	return true;
}
