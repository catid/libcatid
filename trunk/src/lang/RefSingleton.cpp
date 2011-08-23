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

#include <cat/lang/RefSingleton.hpp>
using namespace cat;


//// Mutex

static Mutex m_ref_singleton_mutex;

Mutex &cat::GetRefSingletonMutex()
{
	return m_ref_singleton_mutex;
}


//// RefSingletonBase

RefSingletonBase::RefSingletonBase()
{
	_refs_count = 0;
}

void RefSingletonBase::AddRefSingletonReference(u32 *ref_counter)
{
	u32 ii = _refs_count++;

	if (ii < REFS_PREALLOC)
	{
		_refs_prealloc[ii] = ref_counter;
	}
	else
	{
		ii -= REFS_PREALLOC;

		// Reallocate every time ii has under 2 bits set
		if (ii < REFS_PREALLOC)
		{
			if (ii == 0)
			{
				_refs_extended = new u32*[REFS_PREALLOC];
				if (!_refs_extended) { --_refs_count; return; }
			}
		}
		else if (!CAT_AT_LEAST_2_BITS(ii))
		{
			u32 **new_extended = new u32*[ii + 1];
			if (!new_extended) { --_refs_count; return; }

			// If old data exists,
			if (_refs_extended)
			{
				memcpy(new_extended, _refs_extended, sizeof(u32*) * ii);
				delete []_refs_extended;
			}

			_refs_extended = new_extended;
		}

		_refs_extended[ii] = ref_counter;
	}
}

void RefSingletonBase::ReleaseRefs()
{
	int count = _refs_count;

	for (int ii = 0; ii < REFS_PREALLOC && ii < count; ++ii)
	{
		u32 *cnt = _refs_prealloc[ii];

		*cnt = *cnt - 1;
	}

	int extended_count = count - REFS_PREALLOC;

	if (extended_count > 0 && _refs_extended)
	{
		for (int ii = 0; ii < extended_count; ++ii)
		{
			u32 *cnt = _refs_extended[ii];

			*cnt = *cnt - 1;
		}

		delete []_refs_extended;
	}
}


//// RefSingletons

void RefSingletons::OnExit()
{
	RefSingletons::ref()->OnFinalize();
}

CAT_SINGLETON(RefSingletons);

void RefSingletons::OnInitialize()
{
	// Register shutdown callback
	atexit(&RefSingletons::OnExit);
}

void RefSingletons::OnFinalize()
{
	// While there are still active singletons,
	while (!_active_list.empty())
	{
		bool locked = true;

		// For each active singleton,
		for (iter ii = _active_list; ii; ++ii)
		{
			u32 *ref_count = ii->GetRefCount();

			// If reference count reaches zero,
			if (*ref_count == 0)
			{
				ii->OnFinalize();
				ii->ReleaseRefs();

				_active_list.Erase(ii);

				locked = false;
			}
		}

		// If locked up due to dangling references,
		if (locked)
		{
			CAT_FATAL("RefSingleton") << "Unable to gracefully finalize all the RefSingletons due to dangling references.  Forcing the rest...";

			for (iter ii = _active_list; ii; ++ii)
				ii->OnFinalize();

			break;
		}
	}
}
