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

// 07/21/09 moved them back
// 06/23/09 began, moved macros from Platform.hpp

#ifndef CAT_BITMATH_HPP
#define CAT_BITMATH_HPP

#include <cat/Platform.hpp>

namespace cat {


namespace BitMath {


// Bit Scan Forward (BSF)
// Scans from bit 0 to MSB
// Undefined when input is zero
extern CAT_INLINE u32 BSF32(u32 x);
extern CAT_INLINE u32 BSF64(u64 x);

// Bit Scan Reverse (BSR)
// Scans from MSB to bit 0
// Undefined when input is zero
extern CAT_INLINE u32 BSR32(u32 x);
extern CAT_INLINE u32 BSR64(u64 x);


} // namespace BitMath


} // namespace cat

#endif // CAT_BITMATH_HPP
