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

#ifndef CAT_REF_OBJECT_HPP
#define CAT_REF_OBJECT_HPP

#include <cat/threads/Atomic.hpp>
#include <cat/threads/WaitableFlag.hpp>
//#include <cat/io/Logging.hpp>

#if defined(CAT_NO_ATOMIC_ADD) || defined(CAT_NO_ATOMIC_SET)
#include <cat/threads/Mutex.hpp>
#define CAT_NO_ATOMIC_REF_OBJECT
#endif

#include <vector>
#include <list>

namespace cat {


class RefObject;
class WatchedRefObject;
class RefObjectWatcher;


// Classes that derive from RefObject have asynchronously managed lifetimes
// Never delete a RefObject directly.  Use the RequestShutdown() member instead
class RefObject
{
	friend class WatchedRefObject;

#if defined(CAT_NO_ATOMIC_REF_OBJECT)
	Mutex _lock;
#endif

	volatile u32 _ref_count;
	u8 _packing[CAT_DEFAULT_CACHE_LINE_SIZE];
	volatile u32 _shutdown;

	virtual void ShutdownComplete(bool delete_this);

public:
	CAT_INLINE RefObject()
	{
		// Initialize shutdown flag
		_shutdown = 0;

		// Initialize to one reference
		_ref_count = 1;
	}

	CAT_INLINE virtual ~RefObject() {}

	virtual void RequestShutdown();

	CAT_INLINE bool IsShutdown() { return _shutdown != 0; }

	CAT_INLINE void AddRef(s32 times = 1)
	{
		//WARN("RefObject") << this << " add " << times;

#if defined(CAT_NO_ATOMIC_REF_OBJECT)
		_lock.Enter();
		_ref_count += times;
		_lock.Leave();
#else
		// Increment reference count by # of times
		Atomic::Add(&_ref_count, times);
#endif
	}

	CAT_INLINE void ReleaseRef(s32 times = 1)
	{
		//WARN("RefObject") << this << " release " << times;

		// Decrement reference count by # of times
		// If all references are gone,
#if defined(CAT_NO_ATOMIC_REF_OBJECT)
		u32 ref_count;

		_lock.Enter();
		ref_count = _ref_count;
		_ref_count -= times;
		_lock.Leave();

		if (ref_count == times)
#else
		if (Atomic::Add(&_ref_count, -times) == times)
#endif
		{
			// Request shutdown to make sure shutdown callback is used
			RequestShutdown();

			// Then proceed to call zero references callback
			ShutdownComplete(OnZeroReferences());
		}
	}

	// Safe release -- If not null, then releases and sets to null
	template<class T>
	static CAT_INLINE void Release(T * &object)
	{
		if (object)
		{
			object->ReleaseRef();
			object = 0;
		}
	}

protected:
	// Called when a shutdown is in progress
	// The object should release any internally held references
	// such as private threads that are working on the object
	// Always called and before OnZeroReferences()
	// Proper implementation of derived classes should call the parent version
	virtual void OnShutdownRequest() = 0;

	// Called when object has no more references
	// Return true to delete the object
	// Always called and after OnShutdownRequest()
	virtual bool OnZeroReferences() = 0;
};


// This is a RefObject that can be watched using a RefObjectWatcher
// I separated these out so that the basic RefObject can be very lightweight
class WatchedRefObject : public RefObject
{
	friend class RefObjectWatcher;

#if !defined(CAT_NO_ATOMIC_REF_OBJECT)
	Mutex _lock;
#endif
	std::vector<RefObjectWatcher*> _watchers;
	typedef std::vector<RefObjectWatcher*>::iterator VectorIterator;

	virtual void ShutdownComplete(bool delete_this);
	bool AddWatcher(RefObjectWatcher *watcher);

public:
	CAT_INLINE virtual ~WatchedRefObject() {}

	virtual void RequestShutdown();
};


// Mechanism to wait for reference-counted objects to finish shutting down
class RefObjectWatcher
{
	friend class WatchedRefObject;

	Mutex _lock;
	std::list<WatchedRefObject*> _watched_list;
	typedef std::list<WatchedRefObject*>::iterator ListIterator;
	u32 _wait_count;
	bool _shutdown;

	WaitableFlag _shutdown_flag;

	bool OnObjectShutdownStart(WatchedRefObject *obj);
	void OnObjectShutdownEnd(WatchedRefObject *obj);

public:
	RefObjectWatcher();
	virtual ~RefObjectWatcher();

	// Wait for watched objects to finish shutdown, returns false on timeout
	bool WaitForShutdown(s32 milliseconds = -1); // < 0 = wait forever

	void Watch(WatchedRefObject *obj);
};


// Auto release for RefObjects
template<class T>
class AutoRelease
{
	T *_ref;

public:
	CAT_INLINE AutoRelease(T *ref = 0) throw() { _ref = ref; }
	CAT_INLINE ~AutoRelease() throw() { if (_ref) _ref->ReleaseRef(); }
	CAT_INLINE AutoRelease &operator=(T *ref) throw() { Reset(ref); return *this; }

	CAT_INLINE T *Get() throw() { return _ref; }
	CAT_INLINE T *operator->() throw() { return _ref; }
	CAT_INLINE T &operator*() throw() { return *_ref; }
	CAT_INLINE operator T*() { return _ref; }

	CAT_INLINE void Forget() throw() { _ref = 0; }
	CAT_INLINE void Reset(T *ref = 0) throw() { _ref = ref; }
};


// Auto shutdown for RefObjects
template<class T>
class AutoShutdown
{
	T *_ref;

public:
	CAT_INLINE AutoShutdown(T *ref = 0) throw() { _ref = ref; }
	CAT_INLINE ~AutoShutdown() throw() { if (_ref) _ref->RequestShutdown(); }
	CAT_INLINE AutoShutdown &operator=(T *ref) throw() { Reset(ref); return *this; }

	CAT_INLINE T *Get() throw() { return _ref; }
	CAT_INLINE T *operator->() throw() { return _ref; }
	CAT_INLINE T &operator*() throw() { return *_ref; }
	CAT_INLINE operator T*() { return _ref; }

	CAT_INLINE void Forget() throw() { _ref = 0; }
	CAT_INLINE void Reset(T *ref = 0) throw() { _ref = ref; }
};


} // namespace cat

#endif // CAT_REF_OBJECT_HPP
