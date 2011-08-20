/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_REF_SINGLETON_HPP
#define CAT_REF_SINGLETON_HPP

#include <cat/lang/Singleton.hpp>
#include <cat/lang/LinkedLists.hpp>

namespace cat {


// Internal class
class CAT_EXPORT RefSingletonBase : public SListItem
{
	CAT_NO_COPY(RefSingletonBase);
	CAT_INLINE RefSingletonBase() {}

	u32 _ref_count;

protected:
	void AddRef(RefSingletonBase *instance);
	void ReleaseRefs();

	virtual void OnInitialize() = 0;
	virtual void OnFinalize() = 0;

public:
	CAT_INLINE virtual ~RefSingletonBase() {}
};

// Internal class
template<class T>
class RefSingletonImpl
{
	T _instance;
	bool _init;

public:
	CAT_INLINE T *GetRef()
	{
		if (_init) return &_instance;

		AutoMutex lock(GetRefSingletonMutex());

		if (_init) return &_instance;

		RefSingleton<T> *ptr = &_instance;
		ptr->OnInitialize();

		CAT_FENCE_COMPILER;

		_init = true;

		return &_instance;
	}
};


// In the H file for the object, derive from this class:
template<class T>
class CAT_EXPORT RefSingleton : public RefSingletonBase
{
	friend class RefSingletonImpl<T>;

protected:
	// Called during initialization
	CAT_INLINE virtual void OnInitialize() {}

	// Called during finalization
	CAT_INLINE virtual void OnFinalize() {}

	CAT_INLINE u32 *GetRefCount() { return T::get_refcount_ptr(); }

	// Call only from OnInitialize() to declare which other RefSingletons are used
	template<class S>
	CAT_INLINE void Use()
	{
		u32 *refcount_ptr = S::get_refcount_ptr();
	}

public:
	CAT_INLINE virtual ~RefSingleton<T>() {}

	static T *ref();
	static u32 *get_refcount_ptr();
};


// In the C file for the object, use this macro:
#define CAT_REF_SINGLETON(T)										\
static cat::RefSingletonImpl<T> m_T_rss;							\
template<> T *RefSingleton<T>::ref() { return m_T_rss.GetRef(); }	\
static u32 m_T_rcnt = 0;											\
template<> u32 *RefSingleton<T>::get_refcount_ptr() { return &m_T_rcnt; }


// Internal free function
Mutex CAT_EXPORT &GetRefSingletonMutex();

// Internal class
class CAT_EXPORT RefSingletons : public Singleton<RefSingletons>
{
	SList _active_list;
	typedef SList::Iterator<RefSingletonBase> iter;

	template<class T>
	CAT_INLINE void Watch(T *obj)
	{
		AutoMutex lock(GetRefSingletonMutex());

		_active_list.PushFront(obj);
	}

	static void OnExit();

	void OnInitialize();
	void OnFinalize();
};


} // namespace cat

#endif // CAT_REF_SINGLETON_HPP
