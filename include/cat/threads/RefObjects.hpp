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
#include <cat/threads/Mutex.hpp>
#include <cat/threads/Thread.hpp>

#if defined(CAT_TRACE_REFOBJECT)
#include <cat/io/Logging.hpp>
#endif

#if defined(CAT_NO_ATOMIC_ADD) || defined(CAT_NO_ATOMIC_SET)
#define CAT_NO_ATOMIC_REF_OBJECT
#endif

#include <vector>
#include <list>

#if defined(CAT_TRACE_REFOBJECT)
#define CAT_REFOBJECT_FILE_LINE CAT_FILE_LINE_STRING
#else
#define CAT_REFOBJECT_FILE_LINE 0
#endif

namespace cat {


class RefObject;
class RefObjects;


// Reference to a RefOjbect singleton
template<class T>
class RefObjectSingleton
{
	friend class RefObjects;

	T *_ref;
	bool _valid;

public:
	RefObjectSingleton()
	{
#if defined(CAT_DEBUG)
		_ref = reinterpret_cast<T*>( 1 ); // try to catch errors
#else
		_ref = 0;
#endif
		_valid = false;
	}

	CAT_INLINE operator bool()
	{
		return _valid;
	}

	CAT_INLINE T *operator->()
	{
		return _ref;
	}

	CAT_INLINE T *GetRef()
	{
		return _ref;
	}

	CAT_INLINE RefObjectSingleton<T> &operator=(T *ref)
	{
		_ref = ref;

		CAT_FENCE_COMPILER

		_valid = true;

		return *this;
	}

	CAT_INLINE void Release(const char *file_line)
	{
		if (_valid) _ref->ReleaseRef(file_line);
	}
};


// Mechanism to wait for reference-counted objects to finish shutting down
class CAT_EXPORT RefObjects : Thread
{
	friend class RefObject;

	Mutex _lock;
	RefObject *_active_head, *_dead_head;
	bool _initialized, _shutdown;

	WaitableFlag _shutdown_flag;

	RefObject *FindActiveByGUID(u32 guid);
	bool Watch(const char *file_line, RefObject *obj);	// Will delete object if it fails to initialize
	void Kill(RefObject *obj);

	void LinkToActiveList(RefObject *obj);
	void UnlinkFromActiveList(RefObject *obj);
	void LinkToDeadList(RefObject *obj);
	void BuryDeadites();
	bool ThreadFunction(void *param);

public:
	RefObjects();
	//virtual ~RefObjects();

	static RefObjects *ref();

	// Start the reaper thread
	bool Initialize();

	// Wait for watched objects to finish shutdown, returns false on timeout
	bool Shutdown(s32 milliseconds = -1); // < 0 = wait forever

	template<class T>
	static bool AcquireSingleton(RefObjectSingleton<T> &obj, const char *file_line)
	{
		if (obj)
		{
#if defined(CAT_TRACE_REFOBJECT)
			CAT_INANE("RefObjects") << "AcquireSingleton: " << obj->GetRefObjectName() << "#" << obj.GetRef() << " object already set at " << file_line;
#endif
			obj->AddRef(file_line);
			return true;
		}

		RefObjects *m_refobjects = RefObjects::ref();

		AutoMutex lock(m_refobjects->_lock);

		if (obj)
		{
			lock.Release();

#if defined(CAT_TRACE_REFOBJECT)
			CAT_INANE("RefObjects") << "AcquireSingleton: " << obj->GetRefObjectName() << "#" << obj.GetRef() << " object already set (lost race) at " << file_line;
#endif
			obj->AddRef(file_line);
			return true;
		}

		if (m_refobjects->_shutdown)
		{
#if defined(CAT_TRACE_REFOBJECT)
			CAT_INANE("RefObjects") << "AcquireSingleton: Ignored acquire during shutdown at " << file_line;
#endif
			return false;
		}

		// If the object already has an instance,
		RefObject *existing = m_refobjects->FindActiveByGUID(T::RefObjectGUID);
		if (existing)
		{
			obj = static_cast<T*>( existing );
#if defined(CAT_TRACE_REFOBJECT)
			CAT_INANE("RefObjects") << "AcquireSingleton: " << existing->GetRefObjectName() << "#" << existing << " re-using existing at " << file_line;
#endif
			return true;
		}

		// Create a new object
		T *obj_local = new T;
		if (!obj_local) return false;

		// Get base object
		RefObject *obj_base = obj_local;

		// Initialize shutdown to false and set GUID at the same time
		obj_base->_shutdown_guid = T::RefObjectGUID;

		// If object could not be initialized,
		if (!m_refobjects->Watch(file_line, obj_base))
		{
#if defined(CAT_TRACE_REFOBJECT)
			CAT_INANE("RefObjects") << "AcquireSingleton: " << obj_local->GetRefObjectName() << "#" << obj_local << " initialization failed at " << file_line;
#endif
			return false;
		}

#if defined(CAT_TRACE_REFOBJECT)
		CAT_INANE("RefObjects") << "AcquireSingleton: " << obj_local->GetRefObjectName() << "#" << obj_local << " created at " << file_line;
#endif
		obj = obj_local;
		return true;
	}

	template<class T>
	static bool Acquire(T *&obj, const char *file_line)
	{
		RefObjects *m_refobjects = RefObjects::ref();

		obj = new T;
		if (!obj) return false;

		// Get base object
		RefObject *obj_base = obj;

		// Initialize shutdown to false and set GUID at the same time
		obj_base->_shutdown_guid = T::RefObjectGUID;

		AutoMutex lock(m_refobjects->_lock);

		if (m_refobjects->_shutdown)
		{
			lock.Release();
			delete obj;
#if defined(CAT_TRACE_REFOBJECT)
			CAT_INANE("RefObjects") << "Acquire: " << obj->GetRefObjectName() << "#" << obj << " ignored during shutdown at " << file_line;
#endif
			return false;
		}

		// If object could not be initialized,
		if (!m_refobjects->Watch(file_line, obj_base))
		{
#if defined(CAT_TRACE_REFOBJECT)
			CAT_INANE("RefObjects") << "Acquire: " << obj->GetRefObjectName() << "#" << obj << " initialization failed at " << file_line;
#endif
			obj = 0;
			return false;
		}

		lock.Release();

#if defined(CAT_TRACE_REFOBJECT)
		CAT_INANE("RefObjects") << "Acquire: " << obj->GetRefObjectName() << "#" << obj << " created at " << file_line;
#endif

		return true;
	}
};


// Classes that derive from RefObject have asynchronously managed lifetimes
// Never delete a RefObject directly.  Use the Destroy() member instead
class CAT_EXPORT RefObject
{
	friend class RefObjects;

	CAT_NO_COPY(RefObject);

#if defined(CAT_NO_ATOMIC_REF_OBJECT)
	Mutex _lock;
#endif

	volatile u32 _ref_count;
	volatile u32 _shutdown_guid;
	RefObject *_prev, *_next;

	void OnZeroReferences(const char *file_line);

public:
	RefObject();
	CAT_INLINE virtual ~RefObject() {}

	void Destroy(const char *file_line);

	CAT_INLINE bool IsShutdown() { return _shutdown_guid != ILLEGAL_GUID; }

	CAT_INLINE void AddRef(const char *file_line, s32 times = 1)
	{
#if defined(CAT_TRACE_REFOBJECT)
		CAT_WARN("RefObject") << GetRefObjectName() << "#" << this << " add " << times << " at " << file_line;
#endif

#if defined(CAT_NO_ATOMIC_REF_OBJECT)
		_lock.Enter();
		_ref_count += times;
		_lock.Leave();
#else
		// Increment reference count by # of times
		Atomic::Add(&_ref_count, times);
#endif
	}

	CAT_INLINE void ReleaseRef(const char *file_line, s32 times = 1)
	{
#if defined(CAT_TRACE_REFOBJECT)
		CAT_WARN("RefObject") << GetRefObjectName() << "#" << this << " release " << times << " at " << file_line;
#endif

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
			OnZeroReferences(file_line);
		}
	}

	// Safe release -- If not null, then releases and sets to null
	template<class T>
	static CAT_INLINE void Release(T * &object)
	{
		if (object)
		{
			object->ReleaseRef(CAT_REFOBJECT_FILE_LINE);
			object = 0;
		}
	}

public:
	// Override the GUID to allow the objects to be treated as singletons.
	static const u32 ILLEGAL_GUID = ~(u32)0;
	static const u32 RefObjectGUID = 0;

	// Return a C-string naming the derived RefObject uniquely.
	// For debug output; it can be used to report which object is locking up.
	virtual const char *GetRefObjectName() = 0;

protected:
	// Called when an object is constructed.
	// Allows the object to reference itself on instantiation and report startup
	// errors without putting it in the constructor where it doesn't belong.
	// Return false to delete the object immediately.
	// Especially handy for using RefObjects as a plugin system.
	CAT_INLINE virtual bool OnRefObjectInitialize() { return true; }

	// Called when a shutdown is in progress.
	// The object should release any internally held references.
	// such as private threads that are working on the object.
	// Always called and before OnRefObjectFinalize().
	// Proper implementation of derived classes should call the parent version.
	CAT_INLINE virtual void OnRefObjectDestroy() {}

	// Called when object has no more references.
	// Return true to delete the object.
	// Always called and after OnRefObjectDestroy().
	CAT_INLINE virtual bool OnRefObjectFinalize() { return true; }
};


// Auto release for RefObjects
template<class T>
class AutoRelease
{
	T *_ref;

public:
	CAT_INLINE AutoRelease(T *t = 0) throw() { _ref = t; }
	CAT_INLINE ~AutoRelease() throw() { if (_ref) _ref->ReleaseRef(CAT_REFOBJECT_FILE_LINE); }
	CAT_INLINE AutoRelease &operator=(T *t) throw() { Reset(t); return *this; }

	CAT_INLINE T *Get() throw() { return _ref; }
	CAT_INLINE T *operator->() throw() { return _ref; }
	CAT_INLINE T &operator*() throw() { return *_ref; }
	CAT_INLINE operator T*() { return _ref; }

	CAT_INLINE void Forget() throw() { _ref = 0; }
	CAT_INLINE void Reset(T *t = 0) throw() { _ref = t; }
};


// Auto shutdown for RefObjects
template<class T>
class AutoDestroy
{
	T *_ref;

public:
	CAT_INLINE AutoDestroy(T *t = 0) throw() { _ref = t; }
	CAT_INLINE ~AutoDestroy() throw() { if (_ref) _ref->Destroy(CAT_REFOBJECT_FILE_LINE); }
	CAT_INLINE AutoDestroy &operator=(T *t) throw() { Reset(t); return *this; }

	CAT_INLINE T *Get() throw() { return _ref; }
	CAT_INLINE T *operator->() throw() { return _ref; }
	CAT_INLINE T &operator*() throw() { return *_ref; }
	CAT_INLINE operator T*() { return _ref; }

	CAT_INLINE void Forget() throw() { _ref = 0; }
	CAT_INLINE void Reset(T *t = 0) throw() { _ref = t; }
};


} // namespace cat

#endif // CAT_REF_OBJECT_HPP
