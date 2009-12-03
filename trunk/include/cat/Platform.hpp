/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_PLATFORM_HPP
#define CAT_PLATFORM_HPP

namespace cat {


//// Compiler ////

// Apple/iPhone compiler flags
#ifdef __APPLE__
# include "TargetConditionals.h" // iPhone target flags
#endif

//-----------------------------------------------------------------------------
// Intel C++ Compiler : Interoperates with MSVC and GCC
#if defined(__INTEL_COMPILER) || defined(__ICL) || defined(__ICC) || defined(__ECC)
# define CAT_COMPILER_ICC
#endif

//-----------------------------------------------------------------------------
// Borland C++ Compiler : Compatible with MSVC syntax
#if defined(__BORLANDC__)
# define CAT_COMPILER_BORLAND
# define CAT_COMPILER_COMPAT_MSVC
# define CAT_INLINE __inline
# define CAT_ASM_EMIT __emit__

//-----------------------------------------------------------------------------
// Digital Mars C++ Compiler (previously known as Symantec C++)
#elif defined(__DMC__) || defined(__SC__) || defined(__SYMANTECC__)
# define CAT_COMPILER_DMARS
# define CAT_COMPILER_COMPAT_MSVC
# define CAT_INLINE __inline
# define CAT_ASM_EMIT __emit__

//-----------------------------------------------------------------------------
// Codeplay VectorC C++ Compiler : Compatible with GCC and MSVC syntax, prefer GCC
#elif defined(__VECTORC__)
# define CAT_COMPILER_CODEPLAY
# define CAT_COMPILER_COMPAT_GCC

//-----------------------------------------------------------------------------
// Pathscale C++ Compiler : Compatible with GCC syntax
#elif defined(__PATHSCALE__)
# define CAT_COMPILER_PATHSCALE
# define CAT_COMPILER_COMPAT_GCC

//-----------------------------------------------------------------------------
// Watcom C++ Compiler : Compatible with GCC and MSVC syntax, prefer GCC
#elif defined(__WATCOMC__)
# define CAT_COMPILER_WATCOM
# define CAT_COMPILER_COMPAT_GCC

//-----------------------------------------------------------------------------
// SUN C++ Compiler : Compatible with GCC syntax
#elif defined(__SUNPRO_CC)
# define CAT_COMPILER_SUN
# define CAT_COMPILER_COMPAT_GCC

//-----------------------------------------------------------------------------
// Metrowerks C++ Compiler : Compatible with MSVC syntax
#elif defined(__MWERKS__)
# define CAT_COMPILER_MWERKS
# define CAT_COMPILER_COMPAT_MSVC
# define CAT_INLINE inline
# define CAT_ASM_BEGIN _asm {
# define CAT_ASM_EMIT __emit__

//-----------------------------------------------------------------------------
// GNU C++ Compiler
// SN Systems ProDG C++ Compiler : Compatible with GCC
#elif defined(__GNUC__) || defined(__APPLE_CC__) || defined(__SNC__)
# define CAT_COMPILER_GCC
# define CAT_COMPILER_COMPAT_GCC

//-----------------------------------------------------------------------------
// Microsoft Visual Studio C++ Compiler
#elif defined(_MSC_VER)
# define CAT_COMPILER_MSVC
# define CAT_COMPILER_COMPAT_MSVC

} // namespace cat
# include <cstdlib> // Intrinsics
# include <intrin.h> // Intrinsics
namespace cat {

//-----------------------------------------------------------------------------
// Otherwise unknown compiler
#else
# define CAT_COMPILER_UNKNOWN
# define CAT_ALIGNED(n) /* no way to detect alignment syntax */
# define CAT_PACKED /* no way to detect packing syntax */
# define CAT_INLINE inline
// No way to support inline assembly code here
# define CAT_RESTRICT

#endif

/*
	A lot of compilers have similar syntax to MSVC or GCC,
	so for simplicity I have those two defined below, and
	any deviations are implemented with overrides above.
*/

// MSVC-compatible compilers
#if defined(CAT_COMPILER_COMPAT_MSVC)

#if !defined(CAT_ALIGNED)
# define CAT_ALIGNED(n) __declspec(align(n))
#endif
#if !defined(CAT_PACKED)
# define CAT_PACKED
# define CAT_PRAGMA_PACK
#endif
#if !defined(CAT_INLINE)
# define CAT_INLINE __forceinline
#endif
#if !defined(CAT_ASM_INTEL)
# define CAT_ASM_INTEL
#endif
#if !defined(CAT_ASM_BEGIN)
# define CAT_ASM_BEGIN __asm {
#endif
#if !defined(CAT_ASM_EMIT)
# define CAT_ASM_EMIT _emit
#endif
#if !defined(CAT_ASM_END)
# define CAT_ASM_END }
#endif
#if !defined(CAT_TLS)
# define CAT_TLS __declspec( thread )
#endif
#if !defined(CAT_RESTRICT)
# define CAT_RESTRICT __restrict
#endif

// GCC-compatible compilers
#elif defined(CAT_COMPILER_COMPAT_GCC)

#if !defined(CAT_ALIGNED)
# define CAT_ALIGNED(n) __attribute__ ((aligned (n)))
#endif
#if !defined(CAT_PACKED)
# define CAT_PACKED __attribute__ ((packed))
#endif
#if !defined(CAT_INLINE)
# define CAT_INLINE inline /* __inline__ __attribute__((always_inline)) */
#endif
#if !defined(CAT_ASM_ATT)
# define CAT_ASM_ATT
#endif
#if !defined(CAT_ASM_BEGIN)
# define CAT_ASM_BEGIN __asm__ __volatile__ (
#endif
#if !defined(CAT_ASM_EMIT)
# define CAT_ASM_EMIT .byte
#endif
#if !defined(CAT_ASM_END)
# define CAT_ASM_END );
#endif
#if !defined(CAT_TLS)
# define CAT_TLS __thread
#endif
#if !defined(CAT_RESTRICT)
# define CAT_RESTRICT __restrict__
#endif

#endif // CAT_COMPILER_COMPAT_*


//// Debug Flag ////

#if defined(CAT_COMPILER_MSVC)

# if defined(_DEBUG)
#  define CAT_DEBUG
# endif

#else

# if !defined(NDEBUG)
#  define CAT_DEBUG
# endif

#endif


//// Instruction Set Architecture ////

#if defined(__powerpc__) || defined(__ppc__) || defined(_POWER) || defined(_M_PPC) || \
	defined(_M_MPPC) || defined(__POWERPC) || defined(powerpc) || defined(__ppc64__) || \
	defined(_PS3) || defined(__PS3__) || defined(SN_TARGET_PS3) || defined(__POWERPC__)
# define CAT_ISA_PPC

#elif defined(__i386__) || defined(i386) || defined(intel) || defined(_M_IX86) || \
      defined(__ia64) || defined(__ia64__) || defined(__x86_64) || defined(_M_IA64) || \
	  defined(_M_X64)
# define CAT_ISA_X86

#elif defined(TARGET_CPU_ARM)
# define CAT_ISA_ARM

#elif defined(__mips__)
# define CAT_ISA_MIPS

#elif defined(__ALPHA__)
# define CAT_ISA_ALPHA

#else
# define CAT_ISA_UNKNOWN
#endif


//// Endianness ////

// Okay -- Technically IA64 and PPC can switch endianness with an MSR bit
// flip, but come on no one does that!  ...Right?
// If it's not right, make sure that one of the first two flags are defined.
#if defined(__LITTLE_ENDIAN__)
# define CAT_ENDIAN_LITTLE
#elif defined(__BIG_ENDIAN__)
# define CAT_ENDIAN_BIG
#elif defined(CAT_ISA_X86)
# define CAT_ENDIAN_LITTLE
#elif defined(CAT_ISA_PPC)
# define CAT_ENDIAN_BIG
#else
# define CAT_ENDIAN_UNKNOWN /* Must be detected at runtime */
#endif


//// Word Size ////

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || \
	defined(_WIN64) || defined(_M_X64) || defined(__ia64) || \
	defined(__ia64__) || defined(__x86_64) || defined(_M_IA64) || \
	defined(__mips64)

# define CAT_WORD_64

// 64-bit MSVC does not support inline assembly
# if defined(CAT_COMPILER_MSVC)
#  undef CAT_ASM_INTEL
# endif

#else // Assuming 32-bit otherwise!

# define CAT_WORD_32

#endif


//// Operating System ////

#if defined(__APPLE__) && defined(TARGET_OS_IPHONE)
# define CAT_OS_IPHONE

#elif defined(__APPLE__) && (defined(__MACH__) || defined(__DARWIN__))
# define CAT_OS_OSX

#elif defined(__linux__) || defined(__unix__)
# define CAT_OS_LINUX

#elif defined(_WIN32_WCE)
# define CAT_OS_WINDOWS_CE

#elif defined(_WIN32)
# define CAT_OS_WINDOWS

#elif defined(_XBOX) || defined(_X360)
# define CAT_OS_XBOX

#elif defined(_PS3) || defined(__PS3__) || defined(SN_TARGET_PS3)
# define CAT_OS_PS3

#elif defined(__OS2__)
# define CAT_OS_OS2

#else
# define CAT_OS_UNKNOWN
#endif


//// Basic types ////

#if defined(CAT_COMPILER_MSVC)

	// MSVC does not ship with stdint.h (C99 standard...)
    typedef unsigned __int8  u8;
    typedef signed __int8    s8;
    typedef unsigned __int16 u16;
    typedef signed __int16   s16;
    typedef unsigned __int32 u32;
    typedef signed __int32   s32;
    typedef unsigned __int64 u64;
    typedef signed __int64   s64;

#else

} // namespace cat
#include <stdint.h>
namespace cat {

	// All other compilers use this
	typedef uint8_t  u8;
	typedef int8_t   s8;
	typedef uint16_t u16;
	typedef int16_t  s16;
	typedef uint32_t u32;
	typedef int32_t  s32;
	typedef uint64_t u64;
	typedef int64_t  s64;

#endif

#if defined(CAT_COMPILER_GCC) && defined(CAT_WORD_64)

	// GCC also adds 128-bit types :D
    typedef __uint128_t u128;
    typedef __int128_t  s128;

#endif

typedef float f32;
typedef double f64;

union Float32 {
    float f;
    u32 i;

    Float32(float n) { f = n; }
    Float32(u32 n) { i = n; }
};


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

// Variable-length data trailing a struct
template<typename T> u8 *GetTrailingBytes(T *t) { return reinterpret_cast<u8*>( t ) + sizeof(T); }


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

// Safely take the average of two numbers without possibility of overflow
#define CAT_SAFE_AVERAGE(A, B) (((A) & (B)) + (((A) ^ (B)) >> 1))

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

#if defined(CAT_OS_WINDOWS_CE)

#pragma intrinsic(_lrotl)
#pragma intrinsic(_lrotr)

#undef CAT_ROL32
#undef CAT_ROR32

#define CAT_ROL32(n, r) _lrotl(n, r)
#define CAT_ROR32(n, r) _lrotr(n, r)

#elif defined(CAT_COMPILER_MSVC)

#pragma intrinsic(_rotl)
#pragma intrinsic(_rotr)
#pragma intrinsic(_rotl64)
#pragma intrinsic(_rotr64)
#pragma intrinsic(_byteswap_ushort)
#pragma intrinsic(_byteswap_ulong)
#pragma intrinsic(_byteswap_uint64)
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(__emulu)

#if defined(CAT_WORD_64)
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
