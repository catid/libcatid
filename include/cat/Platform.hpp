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

// 06/11/09 added 64-bit stuff; part of libcat-1.0
// 10/08/08 add detection for Intel compiler (it rocks!)

#ifndef CAT_PLATFORM_HPP
#define CAT_PLATFORM_HPP

namespace cat {


//// Compiler ////

// Structure packing syntax
#if defined(__INTEL_COMPILER) || defined(__ICL) || defined(__ICC) || defined(__ECC)
# define CAT_COMPILER_ICC /* Intel C++ Compiler */
# define CAT_COMPILER_MSVC /* Compatible with MSVC */

#elif defined(__GNUC__) || defined(__APPLE_CC__)
# define CAT_COMPILER_GCC /* GNU C++ Compiler */

# define CAT_PACKED __attribute__((packed)) __attribute__((aligned(4)));
# define CAT_INLINE inline
# define CAT_ASM_ATT
# define CAT_ASM __asm__
# define CAT_TLS __thread

#if defined(DEBUG)
# define CAT_DEBUG
#endif

#elif defined(__BORLANDC__)
# define CAT_COMPILER_BORLAND /* Borland C++ Compiler */

# define CAT_INLINE __inline
# define CAT_ASM_INTEL
# define CAT_ASM _asm
# define CAT_TLS __declspec(thread)

#if !defined(NDEBUG)
# define CAT_DEBUG
#endif

#elif defined(__DMC__)
# define CAT_COMPILER_DMARS
# define CAT_TLS __declspec(thread)
# error "Fill in the holes"

#elif defined(__MWERKS__)
# define CAT_COMPILER_MWERKS
# error "Fill in the holes"

#elif defined(__SUNPRO_CC)
# define CAT_COMPILER_SUN
# define CAT_TLS __thread
# error "Fill in the holes"

#elif defined(_MSC_VER)
# define CAT_COMPILER_MSVC /* Microsoft Visual Studio C++ Compiler */

#else
# error "Add your compiler to the list"
#endif

// Pull out MSVC case here since it applies to several compilers that are MSVC-compatible
#if defined(CAT_COMPILER_MSVC)
# define CAT_PACKED
# define CAT_INLINE __forceinline
# define CAT_ASM_INTEL
# define CAT_ASM __asm
# define CAT_TLS __declspec( thread )

# define WIN32_LEAN_AND_MEAN
# if defined(_DEBUG)
#  define CAT_DEBUG
# endif
                  }
# include <cstdlib> // Intrinsics
# include <intrin.h> // Intrinsics
    namespace cat {
#endif

//// Architecture Endianness ////

#if defined(__sparc) || defined(__sparc__) || defined(__powerpc__) || \
    defined(__ppc__) || defined(__hppa) || defined(_MIPSEB) || defined(_POWER) || \
    defined(_M_PPC) || defined(_M_MPPC) || defined(_M_MRX000) || \
    defined(__POWERPC) || defined(m68k) || defined(powerpc) || \
    defined(sel) || defined(pyr) || defined(mc68000) || defined(is68k) || \
    defined(tahoe) || defined(ibm032) || defined(ibm370) || defined(MIPSEB) || \
    defined(__convex__) || defined(DGUX) || defined(hppa) || defined(apollo) || \
    defined(_CRAY) || defined(__hp9000) || defined(__hp9000s300) || defined(_AIX) || \
    defined(__AIX) || defined(__pyr__) || defined(hp9000s700) || defined(_IBMR2) || \
    defined(_PS3) || defined(__PS3__) || defined(SN_TARGET_PS3) || defined(__ppc64__) || \
	defined(__BIG_ENDIAN__)
# define CAT_ENDIAN_BIG
#elif defined(__i386__) || defined(i386) || defined(intel) || defined(_M_IX86) || \
      defined(__alpha__) || defined(__alpha) || defined(__ia64) || defined(__ia64__) || \
      defined(_M_ALPHA) || defined(ns32000) || defined(__ns32000__) || defined(sequent) || \
      defined(MIPSEL) || defined(_MIPSEL) || defined(sun386) || defined(__sun386__) || \
      defined(__x86_64) || defined(_M_IA64) || defined(_M_X64) || defined(__bfin__) || \
	  defined(__LITTLE_ENDIAN__)
# define CAT_ENDIAN_LITTLE
#else
# error "Add your architecture to the endianness list"
#endif


//// Target Word Size ////

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)

# define CAT_ARCH_64

// 64-bit MSVC does not support inline assembly
# if defined(CAT_COMPILER_MSVC)
#  undef CAT_ASM_INTEL
#  undef CAT_ASM
# endif

#else // Assuming 32-bit otherwise!

# define CAT_ARCH_32

#endif


//// Operating System ////

#if defined(__APPLE__)
# define CAT_OS_APPLE
#elif defined(__linux__)
# define CAT_OS_LINUX
#elif defined(_WIN32)
# define CAT_OS_WINDOWS
#else
# error "Add your operating system to the list"
#endif


//// Basic types ////

#if defined(CAT_COMPILER_MSVC)

    typedef unsigned __int8  u8;
    typedef signed __int8    s8;
    typedef unsigned __int16 u16;
    typedef signed __int16   s16;
    typedef unsigned __int32 u32;
    typedef signed __int32   s32;
    typedef unsigned __int64 u64;
    typedef signed __int64   s64;

#elif defined(CAT_COMPILER_GCC)
                  }
# include <inttypes.h>
    namespace cat {

    typedef uint8_t   u8;
    typedef int8_t    s8;
    typedef uint16_t  u16;
    typedef int16_t   s16;
    typedef uint32_t  u32;
    typedef int32_t   s32;
    typedef uint64_t  u64;
    typedef int64_t   s64;
#if defined(CAT_ARCH_64)
    typedef uint128_t u128;
    typedef int128_t  s128;
#endif

#else
# error "Add your compiler's basic types"
#endif

union Float32 {
    float f;
    u32 i;

    Float32(float n) { f = n; }
    Float32(u32 n) { i = n; }
};

/*
//// Coordinate system ////

// Order of vertices in any quad
enum QuadCoords
{
    QUAD_UL, // upper left  (0,0)
    QUAD_LL, // lower left  (0,1)
    QUAD_LR, // lower right (1,1)
    QUAD_UR, // upper right (1,0)
};
*/


//// String and buffer macros ////

// Same as strncpy() in all ways except that the result is guaranteed to
// be a nul-terminated C string
#if defined(CAT_COMPILER_MSVC)
# define CAT_STRNCPY(dest, src, size) { strncpy_s(dest, size, src, size); (dest)[(size)-1] = '\0'; }
#else
# define CAT_STRNCPY(dest, src, size) { strncpy(dest, src, size); (dest)[(size)-1] = '\0'; }
#endif

// Because memory clearing is a frequent operation
#define CAT_MEMCLR(dest, size) memset(dest, 0, size)

// Works for arrays, also
#define CAT_OBJCLR(object) memset(&(object), 0, sizeof(object))

// Stringize
#define CAT_STRINGIZE(X) DO_CAT_STRINGIZE(X)
#define DO_CAT_STRINGIZE(X) #X


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
#pragma intrinsic(__rdtsc)
#pragma intrinsic(_umul128)
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)
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


} // namespace cat

#endif // CAT_PLATFORM_HPP
