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

#include <cat/threads/RefObject.hpp>
using namespace cat;

static Mutex m_refobjects_lock;


//// RefObject

RefObject::RefObject()
{
	// Initialize to one reference
	_ref_count = 1;
	_shutdown = 0;
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
	if (Atomic::Set(&_shutdown, 1) != 1)
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

	RefObjects::ref()->Kill(this);
}


//// RefObjects

CAT_REF_SINGLETON(RefObjects);

void RefObjects::OnInitialize()
{
	_shutdown = false;

	if (!Thread::StartThread())
	{
		CAT_FATAL("RefObjects") << "Unable to start reaper thread";
		_initialized = false;
		return;
	}

	_initialized = true;
}

Mutex &RefObjects::GetGlobalLock()
{
	return m_refobjects_lock;
}

bool RefObjects::Watch(const char *file_line, RefObject *&obj)
{
	if (!obj) return false;

	if (!_initialized || _shutdown)
	{
		delete obj;
		obj = 0;

#if defined(CAT_TRACE_REFOBJECT)
		CAT_INANE("RefObjects") << obj->GetRefObjectName() << "#" << obj << " refused to watch during bad state at " << file_line;
#endif
		return false;
	}

	AutoMutex lock(m_refobjects_lock);

	if (_shutdown)
	{
		lock.Release();

		delete obj;
		obj = 0;

#if defined(CAT_TRACE_REFOBJECT)
		CAT_INANE("RefObjects") << obj->GetRefObjectName() << "#" << obj << " refused to watch during shutdown at " << file_line;
#endif
		return false;
	}

	if (!obj->OnRefObjectInitialize())
	{
#if defined(CAT_TRACE_REFOBJECT)
		CAT_WARN("RefObjects") << obj->GetRefObjectName() << "#" << obj << " failed to initialize at " << file_line;
#endif

		obj->Destroy(file_line);

		_dead_list.PushFront(obj);

		obj = 0;

		return false;
	}

#if defined(CAT_TRACE_REFOBJECT)
	CAT_WARN("RefObjects") << obj->GetRefObjectName() << "#" << obj << " active and watched at " << file_line;
#endif

	_active_list.PushFront(obj);

	return true;
}

void RefObjects::Kill(RefObject *obj)
{
	AutoMutex lock(m_refobjects_lock);

	// Skip if shutdown
	if (!_shutdown)
	{
		_active_list.Erase(obj);

		_dead_list.PushFront(obj);
	}
}

void RefObjects::OnFinalize()
{
	_shutdown_flag.Set();

	static const int REFOBJECTS_REAPER_WAIT = 15000; // ms

	Thread::WaitForThread(REFOBJECTS_REAPER_WAIT);
}

void RefObjects::BuryDeadites()
{
	if (!_dead_list.empty()) return;

	// Copy dead list
	DListForward dead_list;

	m_refobjects_lock.Enter();
	dead_list.Steal(_dead_list);
	m_refobjects_lock.Leave();

	for (DListForward::Iterator<RefObject> next = 0, ii = dead_list.head(); ii; ii = next)
	{
		next = ii.GetNext();

		if (ii->OnRefObjectFinalize())
			delete ii;
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

	m_refobjects_lock.Enter();

	_shutdown = true;

	// Now the shutdown flag is set everywhere synchronously.
	// The lists may only be modified from this function.

	m_refobjects_lock.Leave();

	CAT_INANE("RefObjects") << "Reaper destroying remaining active objects...";

	// For each remaining active object,
	for (DListForward::Iterator<RefObject> ii = _active_list.head(); ii; ++ii)
	{
		ii->Destroy(CAT_REFOBJECT_FILE_LINE);
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
		for (DListForward::Iterator<RefObject> next = 0, ii = _active_list.head(); ii; ii = next)
		{
			next = ii.GetNext();

			// If reference count hits zero,
			if (ii->_ref_count == 0)
			{
#if defined(CAT_TRACE_REFOBJECT)
				CAT_INANE("RefObjects") << ii->GetRefObjectName() << "#" << ii.GetRef() << " finalizing";
#endif

				_active_list.Erase(ii);

				// If object finalizing requests memory freed,
				if (ii->OnRefObjectFinalize())
				{
#if defined(CAT_TRACE_REFOBJECT)
					CAT_INANE("RefObjects") << ii->GetRefObjectName() << "#" << ii.GetRef() << " freeing memory";
#endif

					delete ii;
				}

				// Reset hang counter
				hang_counter = 0;
			}
		}

		// Quit when active list is empty
		if (_active_list.empty()) break;

		// If active list is not empty and hang count exceeded threshold,
		if (++hang_counter < HANG_THRESHOLD)
			Clock::sleep(SLEEP_TIME);
		else
		{
			// Find smallest ref count object
			DListForward::Iterator<RefObject> smallest_obj = _active_list.head();
			u32 smallest_ref_count = smallest_obj->_ref_count;

			for (DListForward::Iterator<RefObject> next = 0, ii = smallest_obj.GetNext(); ii; ii = next)
			{
				next = ii.GetNext();

				if (ii->_ref_count < smallest_ref_count)
				{
					smallest_ref_count = ii->_ref_count;
					smallest_obj = ii;
				}
			}

			CAT_FATAL("RefObjects") << smallest_obj->GetRefObjectName() << "#" << smallest_obj.GetRef() << " finalizing FORCED with " << smallest_ref_count << " dangling references (smallest found)";

			_active_list.Erase(smallest_obj);

			// If object finalizing requests memory freed,
			if (smallest_obj->OnRefObjectFinalize())
			{
				CAT_FATAL("RefObjects") << smallest_obj->GetRefObjectName() << "#" << smallest_obj.GetRef() << " freeing memory for forced finalize";

				delete smallest_obj;
			}

			// Reset hang counter
			hang_counter = 0;
		}
	}

	CAT_INANE("RefObjects") << "...Reaper going to sleep in a quiet field of dead objects";

	_initialized = false;
	return true;
}
