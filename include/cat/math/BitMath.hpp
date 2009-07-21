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


extern bool UnitTest();


} // namespace BitMath


} // namespace cat


//// Miscellaneous bitwise macros ////

#define CAT_BITCLRHI8(reg, count) ((u8)((u8)(reg) << (count)) >> (count)) /* sets to zero a number of high bits in a byte */
#define CAT_BITCLRLO8(reg, count) ((u8)((u8)(reg) >> (count)) << (count)) /* sets to zero a number of low bits in a byte */
#define CAT_BITCLRHI16(reg, count) ((u16)((u16)(reg) << (count)) >> (count)) /* sets to zero a number of high bits in a 16-bit word */
#define CAT_BITCLRLO16(reg, count) ((u16)((u16)(reg) >> (count)) << (count)) /* sets to zero a number of low bits in a 16-bit word */
#define CAT_BITCLRHI32(reg, count) ((u32)((u32)(reg) << (count)) >> (count)) /* sets to zero a number of high bits in a 32-bit word */
#define CAT_BITCLRLO32(reg, count) ((u32)((u32)(reg) >> (count)) << (count)) /* sets to zero a number of low bits in a 32-bit word */


//// Integer macros ////

#define CAT_AT_LEAST_2_BITS(n) ( (n) & ((n) - 1) )
#define CAT_LEAST_SIGNIFICANT_BIT(n) ( (n) & (u32)(-(s32)(n)) ) /* 0 -> 0 */
#define CAT_IS_POWER_OF_2(n) ( n && !CAT_AT_LEAST_2_BITS(n) )

// Bump 'n' to the next unit of 'width'
// 0=CAT_CEIL_UNIT(0, 16), 1=CAT_CEIL_UNIT(1, 16), 1=CAT_CEIL_UNIT(16, 16), 2=CAT_CEIL_UNIT(17, 16)
#define CAT_CEIL_UNIT(n, width) ( ( (n) + (width) - 1 ) / (width) )
// 0=CAT_CEIL(0, 16), 16=CAT_CEIL(1, 16), 16=CAT_CEIL(16, 16), 32=CAT_CEIL(17, 16)
#define CAT_CEIL(n, width) ( CAT_CEIL_UNIT(n, width) * (width) )


//// Rotation macros ////

#define CAT_ROL8(n, r)  ( ((u8)(n) << (r)) | ((u8)(n) >> ( 8 - (r))) ) /* only works for u8 */
#define CAT_ROR8(n, r)  ( ((u8)(n) >> (r)) | ((u8)(n) << ( 8 - (r))) ) /* only works for u8 */
#define CAT_ROL16(n, r) ( ((u16)(n) << (r)) | ((u16)(n) >> (16 - (r))) ) /* only works for u16 */
#define CAT_ROR16(n, r) ( ((u16)(n) >> (r)) | ((u16)(n) << (16 - (r))) ) /* only works for u16 */
#define CAT_ROL32(n, r) ( ((u32)(n) << (r)) | ((u32)(n) >> (32 - (r))) ) /* only works for u32 */
#define CAT_ROR32(n, r) ( ((u32)(n) >> (r)) | ((u32)(n) << (32 - (r))) ) /* only works for u32 */
#define CAT_ROL64(n, r) ( ((u64)(n) << (r)) | ((u64)(n) >> (64 - (r))) ) /* only works for u64 */
#define CAT_ROR64(n, r) ( ((u64)(n) >> (r)) | ((u64)(n) << (64 - (r))) ) /* only works for u64 */


//// Byte-order swapping ////

#define CAT_BOSWAP16(n) CAT_ROL16(n, 8)
#define CAT_BOSWAP32(n) ( (CAT_ROL32(n, 8) & 0x00ff00ff) | (CAT_ROL32(n, 24) & 0xff00ff00) )
#define CAT_BOSWAP64(n) ( ((u64)CAT_BOSWAP32((u32)n) << 32) | CAT_BOSWAP32((u32)(n >> 32)) )


//// Intrinsics ////

#if defined(CAT_COMPILER_MSVC)

#pragma intrinsic(_rotl)
#pragma intrinsic(_rotr)
#pragma intrinsic(_rotl64)
#pragma intrinsic(_rotr64)
#pragma intrinsic(_byteswap_ushort)
#pragma intrinsic(_byteswap_ulong)
#pragma intrinsic(_byteswap_uint64)
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)

#if defined(CAT_ARCH_64)
# pragma intrinsic(__rdtsc)
# pragma intrinsic(_mul128)
# pragma intrinsic(_BitScanForward64)
# pragma intrinsic(_BitScanReverse64)
#endif

#undef CAT_ROL32
#undef CAT_ROR32
#undef CAT_ROL64
#undef CAT_ROR64
#undef CAT_BOSWAP16
#undef CAT_BOSWAP32
#undef CAT_BOSWAP64

#define CAT_ROL32(n, r) _rotl(n, r)
#define CAT_ROR32(n, r) _rotr(n, r)
#define CAT_ROL64(n, r) _rotl64(n, r)
#define CAT_ROR64(n, r) _rotr64(n, r)
#define CAT_BOSWAP16(n) _byteswap_ushort(n)
#define CAT_BOSWAP32(n) _byteswap_ulong(n)
#define CAT_BOSWAP64(n) _byteswap_uint64(n)

#endif


#endif // CAT_BITMATH_HPP
