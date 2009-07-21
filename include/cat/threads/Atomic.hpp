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

// 06/23/09 began

#ifndef CAT_ATOMIC_HPP
#define CAT_ATOMIC_HPP

#include <cat/Platform.hpp>

namespace cat {


namespace Atomic {


// Compare-and-Swap (CAS)
// On 32-bit architectures, the arguments point to 64-bit values
// On 64-bit architectures, the arguments point to 128-bit values
// Returns true if the old value was equal to the expected value
extern CAT_INLINE bool CAS(volatile void *x, const void *expected_old_value, const void *new_value);

// Add y to x, returning the previous state of x
extern CAT_INLINE u32 Add(volatile u32 *x, s32 y);

// Set x to new value, returning the previous state of x
extern CAT_INLINE u32 Set(volatile u32 *x, u32 new_value);

// Bit Test and Set (BTS)
// Returns true if the bit was 1 and is still 1, otherwise false
extern CAT_INLINE bool BTS(volatile u32 *x, int bit);

// Bit Test and Reset (BTR)
// Returns true if the bit was 1 and is now 0, otherwise false
extern CAT_INLINE bool BTR(volatile u32 *x, int bit);


extern bool UnitTest();


} // namespace Atomic


} // namespace cat

#endif // CAT_ATOMIC_HPP
