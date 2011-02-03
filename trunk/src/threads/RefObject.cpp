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

#include <algorithm>


//// RefObject

void RefObject::ShutdownComplete(bool delete_this)
{
	if (delete_this) delete this;
}

void RefObject::RequestShutdown()
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
		OnShutdownRequest();

		// Release the initial reference to allow OnZeroReference()
		ReleaseRef();
	}
}


//// WatchedRefObject

void WatchedRefObject::ShutdownComplete(bool delete_this)
{
	_lock.Enter();

	for (std::vector<RefObjectWatcher*>::iterator ii = _watchers.begin(); ii != _watchers.end(); ++ii)
	{
		RefObjectWatcher *watcher = *ii;

		watcher->OnObjectShutdownEnd(this);
	}

	_watchers.clear();

	_lock.Leave();

	if (delete_this) delete this;
}


//// RefObjectWatcher

RefObjectWatcher::RefObjectWatcher()
{
	_wait_count = 0;
}

RefObjectWatcher::~RefObjectWatcher()
{
	WaitForShutdown();
}

bool RefObjectWatcher::WaitForShutdown(s32 milliseconds, bool request_shutdown)
{
	AutoMutex lock(_lock);

	if (_wait_count == 0) return true;

	// If user wants to shutdown watched objects,
	if (request_shutdown)
	{
		// For each object we haven't seen shutdown start yet,
		for (std::list<RefObject*>::iterator ii = _watched_list.begin(); ii != _watched_list.end(); ++ii)
		{
			RefObject *obj = *ii;

			obj->RequestShutdown();
		}
	}

	lock.Release();

	return _shutdown_flag.Wait(milliseconds);
}

void RefObjectWatcher::Watch(RefObject *obj)
{
	AutoMutex lock(_lock);

	// If object is already watched, abort
	if (find(_watched_list.begin(), _watched_list.end(), obj) != _watched_list.end()) return;

	_watched_list.push_back(obj);
	++_wait_count;

	lock.Release();

	obj->AddRef();
}

bool RefObjectWatcher::OnObjectShutdownStart(RefObject *obj)
{
	AutoMutex lock(_lock);

	// Try to find the object in the watched list
	std::list<RefObject*>::iterator ii = find(_watched_list.begin(), _watched_list.end(), obj);

	// If it was found in the list,
	if (ii != _watched_list.end())
	{
		_watched_list.erase(ii);

		lock.Release();

		// Release object reference, since they still have a reference on us
		// and will call us back when shutdown ends
		obj->ReleaseRef();

		// Do not decrement _wait_count until shutdown is complete
		return true;
	}

	return false;
}

void RefObjectWatcher::OnObjectShutdownEnd(RefObject *obj)
{
	_lock.Enter();
	u32 wait_count = --_wait_count;
	_lock.Leave();

	if (wait_count == 0)
		_shutdown_flag.Set();
}
