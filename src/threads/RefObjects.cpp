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
#include <cat/threads/AutoMutex.hpp>
using namespace cat;

#include <algorithm>


//// RefObjects Singleton

static RefObjects refobject_watcher;

RefObjects *RefObjects::ref()
{
	return &refobject_watcher;
}


//// RefObject

void RefObject::Destroy()
{
#if defined(CAT_TRACE_REFOBJECT)
	CAT_WARN("RefObject") << this << " Destroy";
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

		// Release the initial reference to allow OnZeroReference()
		ReleaseRef(CAT_REFOBJECT_FILE_LINE);
	}
}


//// WatchedRefObject

void WatchedRefObject::ShutdownComplete(bool delete_this)
{
	_lock.Enter();

	for (VectorIterator ii = _watchers.begin(); ii != _watchers.end(); ++ii)
		(*ii)->OnObjectShutdownEnd(this);

	_watchers.clear();

	_lock.Leave();

	if (delete_this) delete this;
}

void WatchedRefObject::RequestShutdown()
{
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

		_lock.Enter();

		for (VectorIterator ii = _watchers.begin(); ii != _watchers.end(); ++ii)
			(*ii)->OnObjectShutdownStart(this);

		_lock.Leave();

		// Release the initial reference to allow OnZeroReference()
		ReleaseRef(CAT_REFOBJECT_FILE_LINE);
	}
}

bool WatchedRefObject::AddWatcher(RefObjects *watcher)
{
	AutoMutex lock(_lock);

	if (!IsShutdown())
	{
		AddRef(CAT_REFOBJECT_FILE_LINE);
		_watchers.push_back(watcher);
		return true;
	}

	return false;
}


//// RefObjects

RefObjects::RefObjects()
{
	_wait_count = 0;
	_shutdown = false;
}

RefObjects::~RefObjects()
{
	WaitForShutdown();
}

bool RefObjects::WaitForShutdown(s32 milliseconds)
{
	AutoMutex lock(_lock);

	bool shutdown = _shutdown;
	_shutdown = true;

	if (_wait_count == 0) return true;

	lock.Release();

	// If first time shutting down,
	if (!shutdown)
	{
		// For each object we haven't seen shutdown start yet,
		for (ListIterator ii = _watched_list.begin(); ii != _watched_list.end(); ++ii)
		{
			RefObject *obj = *ii;
			ListIterator next = ii;

			obj->Destroy();
		}
	}

	return _shutdown_flag.Wait(milliseconds);
}

void RefObjects::Watch(WatchedRefObject *obj)
{
	AutoMutex lock(_lock);

	// Abort if watcher is shutdown already
	if (_shutdown) return;

	// Abort if in race condition with watched object shutdown
	if (!obj->AddWatcher(this))
		return;

	_watched_list.push_back(obj);
	++_wait_count;
}

bool RefObjects::OnObjectShutdownStart(WatchedRefObject *obj)
{
	AutoMutex lock(_lock);

	// If shutting down,
	if (_shutdown)
	{
		// Just release the reference - No longer using this list
		obj->ReleaseRef(CAT_REFOBJECT_FILE_LINE);
		return true;
	}

	// Try to find the object in the watched list
	ListIterator ii = std::find(_watched_list.begin(), _watched_list.end(), obj);

	// If it was found in the list,
	if (ii == _watched_list.end()) return false;

	_watched_list.erase(ii);

	lock.Release();

	// Release object reference, since they still have a reference on us
	// and will call us back when shutdown ends
	obj->ReleaseRef(CAT_REFOBJECT_FILE_LINE);

	// Do not decrement _wait_count until shutdown is complete
	return true;
}

void RefObjects::OnObjectShutdownEnd(WatchedRefObject *obj)
{
	_lock.Enter();

	u32 wait_count = --_wait_count;
	bool shutdown = _shutdown && wait_count <= 0;

	_lock.Leave();

	// If the watcher is shutting down and all wait objects are dead,
	if (shutdown) _shutdown_flag.Set();
}
