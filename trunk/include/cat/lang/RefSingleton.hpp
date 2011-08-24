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


/*
	RefSingleton builds on the Singleton class, to add OnFinalize().

	When the order of finalization matters, RefSingleton objects may call the FinalizeBefore<>();
	function inside of their OnInitialize() member.  This function creates a reference counted
	relationship between the two objects.  Circular references will cause this system to break.

	To declare a RefSingleton in the header file:

		#include <cat/lang/RefSingleton.hpp>

		class Settings : public RefSingleton<Settings>
		{
			void OnInitialize();
			void OnFinalize();
			// No ctor or dtor

	To define a Refsingleton in the source file:

		CAT_REF_SINGLETON(Settings);

		void Settings::OnInitialize()
		{
			FinalizeBefore<Clock>(); // Add a reference to Clock RefSingleton so order of finalization is correct.
		}

		void Settings::OnFinalize()
		{
		}

	Just ~10 lines of code to convert an object into a singleton with correct finalization order!
*/


// Internal class
class CAT_EXPORT RefSingletonBase : public SListItem
{
	friend class RefSingletons;

	CAT_NO_COPY(RefSingletonBase);

	virtual u32 *GetRefCount() = 0;
	void ReleaseRefs();

	static const int REFS_PREALLOC = 8;
	u32 _refs_count;
	u32 *_refs_prealloc[REFS_PREALLOC];
	u32 **_refs_extended;

protected:
	RefSingletonBase();

	void AddRefSingletonReference(u32 *ref_counter);

	virtual void OnInitialize() = 0;
	virtual void OnFinalize() = 0;

public:
	CAT_INLINE virtual ~RefSingletonBase() {}
};

// Internal class
class CAT_EXPORT RefSingletonImplBase
{
protected:
	CAT_INLINE void Watch(RefSingletonBase *obj);
};

// Internal class
template<class T>
class RefSingletonImpl : public RefSingletonImplBase
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
		Watch(&_instance);

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

	CAT_INLINE u32 *GetRefCount() { return T::get_refcount_ptr(); }

protected:
	// Called during initialization
	CAT_INLINE virtual void OnInitialize() {}

	// Called during finalization
	CAT_INLINE virtual void OnFinalize() {}

	// Call only from OnInitialize() to declare which other RefSingletons are used
	template<class S>
	CAT_INLINE void FinalizeBefore()
	{
		AddRefSingletonReference(S::get_refcount_ptr());
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
	friend class RefSingletonImplBase;

	SList _active_list;
	typedef SList::Iterator<RefSingletonBase> iter;

	static void OnExit();

	void OnInitialize();
	void OnFinalize();

	template<class T>
	CAT_INLINE void Watch(T *obj)
	{
		_active_list.PushFront(obj);
	}
};

// Internal inline member function definition
CAT_INLINE void RefSingletonImplBase::Watch(RefSingletonBase *obj)
{
	RefSingletons::ref()->Watch(obj);
}


} // namespace cat

#endif // CAT_REF_SINGLETON_HPP
