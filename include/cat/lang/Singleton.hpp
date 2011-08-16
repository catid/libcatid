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

#ifndef CAT_SINGLETON_HPP
#define CAT_SINGLETON_HPP

/*
	This implementation of singletons is motivated by a few real requirements:

	+ Objects that are global,
		but have initialization that must be done once in a thread-safe manner.
	+ Use is initialization, so the client does not need to explicitly initialize.
	+ Pre-allocate the objects in the data section, and initialize at runtime.
	+ Cannot create or copy the singleton object in normal ways.
	+ Easier to type than coding it in a broken way.
	+ Access the object across DLLs without memory allocation issues.

	Usage:

		To declare a singleton class:

			class MyClass
			{
				CAT_SINGLETON(MyClass);
				...

		To define a singleton class:

			CAT_ON_SINGLETON_STARTUP(SystemInfo)
			{
				...
			}

		To access a member of the singleton instance:

			MyClass::ref()->blah

	Some things it won't do and work-arounds:

	- You cannot specify a constructor for the object.
		-> Implement the provided startup callback routine.
	- You cannot specify a deconstructor for the object.
		-> Use RefObjects for singletons that need cleanup.
*/

#include <cat/threads/RefObjects.hpp>

namespace cat {


// In the H file for the object, use this macro:
#define CAT_SINGLETON(T)		\
	CAT_NO_COPY(T);				\
public:							\
	static T *ref();			\
private:						\
	CAT_INLINE T() {}			\
	CAT_INLINE virtual ~T() {}	\
	friend class Singleton<T>;	\
	void OnSingletonStartup();


// In the C file for the object, use this macro:
#define CAT_ON_SINGLETON_STARTUP(T)		\
static cat::Singleton<T> m_T_ss;		\
T *T::ref() { return m_T_ss.GetRef(); }	\
void T::OnSingletonStartup()


// Internal class
template<class T>
class Singleton
{
	T _instance;
	bool _init;
	Mutex _lock;

public:
	CAT_INLINE T *GetRef()
	{
		if (_init) return &_instance;

		AutoMutex lock(_lock);

		if (_init) return &_instance;

		_instance.OnSingletonStartup();

		CAT_FENCE_COMPILER;

		_init = true;

		return &_instance;
	}
};


} // namespace cat

#endif // CAT_SINGLETON_HPP
