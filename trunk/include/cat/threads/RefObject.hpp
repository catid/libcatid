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

namespace cat {


// Classes that derive from RefObject have asynchonously managed lifetimes
class RefObject
{
private:
	volatile CAT_ALIGNED(CAT_DEFAULT_CACHE_LINE_SIZE) u32 _ref_count;
	volatile CAT_ALIGNED(CAT_DEFAULT_CACHE_LINE_SIZE) u32 _shutdown;

protected:
	// Called when a shutdown is in progress
	// The object should release any internally held references
	// such as private threads that are working on the object
	// Always called and before OnZeroReferences()
	virtual void OnShutdownRequest() = 0;

	// Called when object has no more references
	// The object should delete itself in response
	// Always called and after OnShutdownRequest()
	virtual void OnZeroReferences() = 0;

public:
	CAT_INLINE RefObject()
	{
		// Initialize shutdown flag
		_shutdown = 0;

		// Initialize to one reference
		_ref_count = 1;
	}

	CAT_INLINE virtual ~RefObject() {}

public:
	CAT_INLINE void RequestShutdown()
	{
		// Raise shutdown flag
		if (Atomic::Set(&_shutdown, 1) == 0)
		{
			// Notify the derived class on the first shutdown request
			OnShutdownRequest();

			// Release the initial reference to allow OnZeroReference()
			ReleaseRef();
		}
	}

	CAT_INLINE bool IsShutdown() { return _shutdown != 0; }

public:
	CAT_INLINE void AddRef()
	{
		// Increment reference count by 1
		Atomic::Add(&_ref_count, 1);
	}

	CAT_INLINE void ReleaseRef()
	{
		// Decrement reference count by 1
		// If all references are gone,
		if (Atomic::Add(&_ref_count, -1) == 1)
		{
			// Request shutdown to make sure shutdown callback is used
			RequestShutdown();

			// Then proceed to call zero references callback
			OnZeroReferences();
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
};


// Auto release for RefObjects
template<class T>
class AutoRef
{
	T *_ref;

public:
	CAT_INLINE AutoRef(T *ref = 0) throw() { _ref = ref; }
	CAT_INLINE ~AutoRef() throw() { RefObject::Release(_ref); }
	CAT_INLINE AutoRef &operator=(T *ref) throw() { Reset(ref); return *this; }

	CAT_INLINE T *Get() throw() { return _ref; }
	CAT_INLINE T *operator->() throw() { return _ref; }
	CAT_INLINE T &operator*() throw() { return *_ref; }
	CAT_INLINE operator T*() { return _ref; }

	CAT_INLINE void Forget() throw() { _ref = 0; }
	CAT_INLINE void Reset(T *ref = 0) throw() { RefObject::Release(_ref); _ref = ref; }
};


} // namespace cat

#endif // CAT_REF_OBJECT_HPP
