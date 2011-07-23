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
	CAT_WARN("RefObject") << GetRefObjectName() << "#" << this << " destroyed at " << file_line;
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
		OnRefObjectDestroy();

		// Release the initial reference to allow Finalize()
		ReleaseRef(file_line);
	}
}

void RefObject::OnZeroReferences(const char *file_line)
{
#if defined(CAT_TRACE_REFOBJECT)
	CAT_WARN("RefObject") << GetRefObjectName() << "#" << this << " zero refs at " << file_line;
#endif

	refobject_reaper.Kill(this);
}


//// RefObjects

RefObjects::RefObjects()
{
	_active_head = _dead_head = 0;

	_shutdown = _initialized = false;
}

bool RefObjects::Watch(const char *file_line, RefObject *obj)
{
	if (!obj) return false;

	if (!obj->OnRefObjectInitialize())
	{
		delete obj;

		return false;
	}

#if defined(CAT_TRACE_REFOBJECT)
	CAT_WARN("RefObjects") << obj->GetRefObjectName() << "#" << obj << " acquired at " << file_line;
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

static RefObjects refobject_reaper;

RefObjects *RefObjects::ref()
{
	return &refobject_reaper;
}

void RefObjectsAtExit()
{
	refobject_reaper.Shutdown();
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

		if (obj->OnRefObjectFinalize())
			delete obj;
	}
}

bool RefObjects::ThreadFunction(void *param)
{
	CAT_INANE("RefObjects") << "Reaper starting...";

	while (!_shutdown_flag.Wait(513))
	{
		BuryDeadites();
	}

	CAT_INANE("RefObjects") << "Reaper caught shutdown signal, setting asynchronous shutdown flag...";

	_lock.Enter();

	_shutdown = true;

	// Now the shutdown flag is set everywhere synchronously.
	// The lists may only be modified from this function.

	_lock.Leave();

	CAT_INANE("RefObjects") << "Reaper destroying remaining active objects...";

	// For each remaining active object,
	for (RefObject *obj = _active_head; obj; obj = obj->_next)
	{
		obj->Destroy(CAT_REFOBJECT_FILE_LINE);
	}

	CAT_INANE("RefObjects") << "Reaper burying any easy dead...";

	BuryDeadites();

	CAT_INANE("RefObjects") << "Reaper spinning and finalizing the remaining active objects...";

	static const u32 HANG_THRESHOLD = 3000; // ms
	static const u32 SLEEP_TIME = 10; // ms
	u32 hang_counter = 0;

	// While active objects still exist,
	CAT_FOREVER
	{
		// Troll for zero reference counts
		for (RefObject *next, *node = _active_head; node; node = next)
		{
			next = node->_next;

			// If reference count hits zero,
			if (node->_ref_count == 0)
			{
				CAT_INANE("RefObjects") << node->GetRefObjectName() << "#" << node << " finalizing";

				UnlinkFromActiveList(node);

				// If object finalizing requests memory freed,
				if (node->OnRefObjectFinalize())
				{
					CAT_INANE("RefObjects") << node->GetRefObjectName() << "#" << node << " freeing memory";

					delete node;
				}

				// Reset hang counter
				hang_counter = 0;
			}
		}

		// Quit when active list is empty
		if (!_active_head) break;

		// If active list is not empty and hang count exceeded threshold,
		if (++hang_counter < HANG_THRESHOLD)
			Clock::sleep(SLEEP_TIME);
		else
		{
			// Find smallest ref count object
			RefObject *smallest_obj = _active_head;
			u32 smallest_ref_count = smallest_obj->_ref_count;

			for (RefObject *next, *node = smallest_obj->_next; node; node = next)
			{
				next = node->_next;

				if (node->_ref_count < smallest_ref_count)
				{
					smallest_ref_count = node->_ref_count;
					smallest_obj = node;
				}
			}

			CAT_FATAL("RefObjects") << smallest_obj->GetRefObjectName() << "#" << smallest_obj << " finalizing FORCED with " << smallest_ref_count << " dangling references (smallest found)";

			UnlinkFromActiveList(smallest_obj);

			// If object finalizing requests memory freed,
			if (smallest_obj->OnRefObjectFinalize())
			{
				CAT_FATAL("RefObjects") << smallest_obj->GetRefObjectName() << "#" << smallest_obj << " freeing memory for forced finalize";

				delete smallest_obj;
			}

			// Reset hang counter
			hang_counter = 0;
		}
	}

	CAT_INANE("RefObjects") << "...Reaper going to sleep in a quiet field of dead objects";

	return true;
}
