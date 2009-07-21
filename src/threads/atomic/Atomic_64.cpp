/*
	Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
	published by the Free Software Foundation, either version 3 of
	the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
	License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cat/threads/Atomic.hpp>
using namespace cat;

#if defined(CAT_ARCH_64)

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#endif


//// Compare-and-Swap

bool Atomic::CAS(volatile void *x, const void *expected_old_value, const void *new_value)
{
#if defined(CAT_COMPILER_MSVC)

	__int64 ComparandResult[2] = { ((u64*)expected_old_value)[0],
								   ((u64*)expected_old_value)[1] };

	// Requires MSVC 2008 or newer
	return 1 == _InterlockedCompareExchange128((__int64*)x, ((u64*)new_value)[1],
											   ((u64*)new_value)[0], ComparandResult);

#endif
}


//// Add y to x, returning the previous state of x
u32 Atomic::Add(volatile u32 *x, s32 y)
{
#if defined(CAT_COMPILER_MSVC)

	return _InterlockedAdd((volatile LONG*)x, y) - y;

#endif
}


//// Set x to new value, returning the previous state of x
u32 Atomic::Set(volatile u32 *x, u32 new_value)
{
#if defined(CAT_COMPILER_MSVC)

	return _InterlockedExchange((volatile LONG*)x, new_value);

#endif
}


//// Bit Test and Set (BTS)

bool Atomic::BTS(volatile u32 *x, int bit)
{
#if defined(CAT_COMPILER_MSVC)

	return !!_interlockedbittestandset((volatile LONG*)x, bit);

#endif
}


//// Bit Test and Reset (BTR)

bool Atomic::BTR(volatile u32 *x, int bit)
{
#if defined(CAT_COMPILER_MSVC)

	return !!_interlockedbittestandreset((volatile LONG*)x, bit);

#endif
}


#endif // defined(CAT_ARCH_64)
