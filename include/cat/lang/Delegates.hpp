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

/*
	Based on The Impossibly Fast C++ Delegates by Sergey Ryazanov from
	http://www.codeproject.com/KB/cpp/ImpossiblyFastCppDelegate.aspx (2005)
*/

/*
	Usage:

	Declare a delegate with a signature of void (int), also known as a
	function that returns void and has one parameter that is an int:
		Delegate1<void, int> d;

	Point the delegate to a member function:
		d.SetMember<A, &A::TestFunctionA>(&a);

	Point the delegate to a const member function:
		d.SetConstMember<C, &C::TestFunctionA>(&c);

	Point the delegate to a free function:
		d.SetFree<&FreeFunctionX>();

	Check if the delegate is unset:
		if (!d) { // unset

	Use double-bang to check if the delegate is set:
		if (!!d) { // is set

	Invoke the function via the delegate:
		d(1000);
*/

#ifndef CAT_DELEGATES_HPP
#define CAT_DELEGATES_HPP

#include <cat/Platform.hpp>

namespace cat {


template <class ret_type>
class Delegate0
{
	typedef ret_type (*StubPointer)(void *);

	void *_object;
	StubPointer _stub;

	template <ret_type (*F)()>
	static CAT_INLINE ret_type FreeStub(void *object)
	{
		return (F)();
	}

	template <class T, ret_type (T::*F)()>
	static CAT_INLINE ret_type MemberStub(void *object)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)();
	}

	template <class T, ret_type (T::*F)() const>
	static CAT_INLINE ret_type ConstMemberStub(void *object)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)();
	}

public:
	CAT_INLINE ret_type operator()() const
	{
		return (*_stub)(_object);
	}

	CAT_INLINE bool operator!() const
	{
		return _stub == 0;
	}

	template <ret_type (*F)()>
	CAT_INLINE void SetFree()
	{
		_object = 0;
		_stub = &FreeStub<F>;
	}

	template <class T, ret_type (T::*F)()>
	CAT_INLINE void SetMember(T *object)
	{
		_object = object;
		_stub = &MemberStub<T, F>;
	}

	template <class T, ret_type (T::*F)() const>
	CAT_INLINE void SetConstMember(T const *object)
	{
		_object = const_cast<T*>( object );
		_stub = &ConstMemberStub<T, F>;
	}
};


template <class ret_type, class arg1_type>
class Delegate1
{
	typedef ret_type (*StubPointer)(void *, arg1_type);

	void *_object;
	StubPointer _stub;

	template <ret_type (*F)(arg1_type)>
	static CAT_INLINE ret_type FreeStub(void *object, arg1_type a1)
	{
		return (F)(a1);
	}

	template <class T, ret_type (T::*F)(arg1_type)>
	static CAT_INLINE ret_type MemberStub(void *object, arg1_type a1)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1);
	}

	template <class T, ret_type (T::*F)(arg1_type) const>
	static CAT_INLINE ret_type ConstMemberStub(void *object, arg1_type a1)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1);
	}

public:
	CAT_INLINE ret_type operator()(arg1_type a1) const
	{
		return (*_stub)(_object, a1);
	}

	CAT_INLINE bool operator!() const
	{
		return _stub == 0;
	}

	template <ret_type (*F)(arg1_type)>
	CAT_INLINE void SetFree()
	{
		_object = 0;
		_stub = &FreeStub<F>;
	}

	template <class T, ret_type (T::*F)(arg1_type)>
	CAT_INLINE void SetMember(T *object)
	{
		_object = object;
		_stub = &MemberStub<T, F>;
	}

	template <class T, ret_type (T::*F)(arg1_type) const>
	CAT_INLINE void SetConstMember(T const *object)
	{
		_object = const_cast<T*>( object );
		_stub = &ConstMemberStub<T, F>;
	}
};


template <class ret_type, class arg1_type, class arg2_type>
class Delegate2
{
	typedef ret_type (*StubPointer)(void *, arg1_type, arg2_type);

	void *_object;
	StubPointer _stub;

	template <ret_type (*F)(arg1_type, arg2_type)>
	static CAT_INLINE ret_type FreeStub(void *object, arg1_type a1, arg2_type a2)
	{
		return (F)(a1, a2);
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type)>
	static CAT_INLINE ret_type MemberStub(void *object, arg1_type a1, arg2_type a2)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1, a2);
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type) const>
	static CAT_INLINE ret_type ConstMemberStub(void *object, arg1_type a1, arg2_type a2)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1, a2);
	}

public:
	CAT_INLINE ret_type operator()(arg1_type a1, arg1_type a2) const
	{
		return (*_stub)(_object, a1, a2);
	}

	CAT_INLINE bool operator!() const
	{
		return _stub == 0;
	}

	template <ret_type (*F)(arg1_type, arg2_type)>
	CAT_INLINE void SetFree()
	{
		_object = 0;
		_stub = &FreeStub<F>;
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type)>
	CAT_INLINE void SetMember(T *object)
	{
		_object = object;
		_stub = &MemberStub<T, F>;
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type) const>
	CAT_INLINE void SetConstMember(T const *object)
	{
		_object = const_cast<T*>( object );
		_stub = &ConstMemberStub<T, F>;
	}
};


template <class ret_type, class arg1_type, class arg2_type, class arg3_type>
class Delegate3
{
	typedef ret_type (*StubPointer)(void *, arg1_type, arg2_type, arg3_type);

	void *_object;
	StubPointer _stub;

	template <ret_type (*F)(arg1_type, arg2_type, arg3_type)>
	static CAT_INLINE ret_type FreeStub(void *object, arg1_type a1, arg2_type a2, arg3_type a3)
	{
		return (F)(a1, a2, a3);
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type)>
	static CAT_INLINE ret_type MemberStub(void *object, arg1_type a1, arg2_type a2, arg3_type a3)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1, a2, a3);
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type) const>
	static CAT_INLINE ret_type ConstMemberStub(void *object, arg1_type a1, arg2_type a2, arg3_type a3)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1, a2, a3);
	}

public:
	CAT_INLINE ret_type operator()(arg1_type a1, arg1_type a2, arg3_type a3) const
	{
		return (*_stub)(_object, a1, a2, a3);
	}

	CAT_INLINE bool operator!() const
	{
		return _stub == 0;
	}

	template <ret_type (*F)(arg1_type, arg2_type, arg3_type)>
	CAT_INLINE void SetFree()
	{
		_object = 0;
		_stub = &FreeStub<F>;
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type)>
	CAT_INLINE void SetMember(T *object)
	{
		_object = object;
		_stub = &MemberStub<T, F>;
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type) const>
	CAT_INLINE void SetConstMember(T const *object)
	{
		_object = const_cast<T*>( object );
		_stub = &ConstMemberStub<T, F>;
	}
};


template <class ret_type, class arg1_type, class arg2_type, class arg3_type, class arg4_type>
class Delegate4
{
	typedef ret_type (*StubPointer)(void *, arg1_type, arg2_type, arg3_type, arg4_type);

	void *_object;
	StubPointer _stub;

	template <ret_type (*F)(arg1_type, arg2_type, arg3_type, arg4_type)>
	static CAT_INLINE ret_type FreeStub(void *object, arg1_type a1, arg2_type a2, arg3_type a3, arg4_type a4)
	{
		return (F)(a1, a2, a3, a4);
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type, arg4_type)>
	static CAT_INLINE ret_type MemberStub(void *object, arg1_type a1, arg2_type a2, arg3_type a3, arg4_type a4)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1, a2, a3, a4);
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type, arg4_type) const>
	static CAT_INLINE ret_type ConstMemberStub(void *object, arg1_type a1, arg2_type a2, arg3_type a3, arg4_type a4)
	{
		T *p = static_cast<T*>(object);
		return (p->*F)(a1, a2, a3, a4);
	}

public:
	CAT_INLINE ret_type operator()(arg1_type a1, arg1_type a2, arg3_type a3, arg4_type a4) const
	{
		return (*_stub)(_object, a1, a2, a3, a4);
	}

	CAT_INLINE bool operator!() const
	{
		return _stub == 0;
	}

	template <ret_type (*F)(arg1_type, arg2_type, arg3_type, arg4_type)>
	CAT_INLINE void SetFree()
	{
		_object = 0;
		_stub = &FreeStub<F>;
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type, arg4_type)>
	CAT_INLINE void SetMember(T *object)
	{
		_object = object;
		_stub = &MemberStub<T, F>;
	}

	template <class T, ret_type (T::*F)(arg1_type, arg2_type, arg3_type, arg4_type) const>
	CAT_INLINE void SetConstMember(T const *object)
	{
		_object = const_cast<T*>( object );
		_stub = &ConstMemberStub<T, F>;
	}
};


} // namespace cat

#endif // CAT_DELEGATES_HPP
