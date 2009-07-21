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
# define CAT_TLS __thread

#elif defined(__GNUC__)
# define CAT_COMPILER_GCC /* GNU C++ Compiler */

# define CAT_PACKED __attribute__((packed)) __attribute__((aligned(4)));
# define CAT_INLINE inline
# define CAT_ASSEMBLY_ATT_SYNTAX
# define CAT_ASSEMBLY_BLOCK asm
# define CAT_TLS __thread
// NOTE: Please define DEBUG externally to signal that the application is under a debugger

#elif defined(__BORLANDC__)
# define CAT_COMPILER_BORLAND /* Borland C++ Compiler */

# define CAT_INLINE __inline
# define CAT_ASSEMBLY_INTEL_SYNTAX
# define CAT_ASSEMBLY_BLOCK _asm
# define CAT_TLS __declspec(thread)

#if !defined(NDEBUG)
# undef DEBUG
# define DEBUG
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
# define CAT_TLS __declspec( thread )

#else
# error "Add your compiler to the list"
#endif

// Pull out MSVC case here since it applies to several compilers that are MSVC-compatible
#if defined(CAT_COMPILER_MSVC)
# define CAT_PACKED
# define CAT_INLINE __inline
# define CAT_ASSEMBLY_INTEL_SYNTAX
# define CAT_ASSEMBLY_BLOCK __asm

# define WIN32_LEAN_AND_MEAN
# if defined(_DEBUG)
#  define DEBUG
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
	defined(_PS3) || defined(__PS3__) || defined(SN_TARGET_PS3)
# define CAT_ENDIAN_BIG
#elif defined(__i386__) || defined(i386) || defined(intel) || defined(_M_IX86) || \
	  defined(__alpha__) || defined(__alpha) || defined(__ia64) || defined(__ia64__) || \
	  defined(_M_ALPHA) || defined(ns32000) || defined(__ns32000__) || defined(sequent) || \
	  defined(MIPSEL) || defined(_MIPSEL) || defined(sun386) || defined(__sun386__) || \
	  defined(__x86_64) || defined(_M_IA64) || defined(_M_X64) || defined(__bfin__)
# define CAT_ENDIAN_LITTLE
#else
# error "Add your architecture to the endianness list"
#endif


//// Architecture 64-bit / 32-bit ////

#if defined(_WIN64)
# define CAT_ARCH_64

// 64-bit MSVC does not support inline assembly
# if defined(CAT_COMPILER_MSVC)
#  undef CAT_ASSEMBLY_INTEL_SYNTAX
#  undef CAT_ASSEMBLY_BLOCK
# endif

#elif defined(_WIN32) // Assuming 32-bit otherwise!
# define CAT_ARCH_32

#else
# error "Add your 64-bit architecture define to the list"
#endif


//// Operating System ////

#if defined(__linux__)
# define CAT_OS_LINUX
#elif defined(_WIN32)
# define CAT_OS_WINDOWS
#else
# error "Add your operating system to the list"
#endif


//// Basic types ////

#if defined(CAT_COMPILER_MSVC)

	typedef unsigned __int8		u8;
	typedef signed __int8		s8;
	typedef unsigned __int16	u16;
	typedef signed __int16		s16;
	typedef unsigned __int32	u32;
	typedef signed __int32		s32;
	typedef unsigned __int64	u64;
	typedef signed __int64		s64;

#elif defined(CAT_COMPILER_GCC)
				  }
# include <inttypes.h>
	namespace cat {

	typedef uint8_t		u8;
	typedef int8_t		s8;
	typedef uint16_t	u16;
	typedef int16_t		s16;
	typedef uint32_t	u32;
	typedef int32_t		s32;
	typedef uint64_t	u64;
	typedef int64_t		s64;
#if defined(CAT_ARCH_64)
	typedef uint128_t	u128;
	typedef int128_t	s128;
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


} // namespace cat

#endif // CAT_PLATFORM_HPP
