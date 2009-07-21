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

// 07/12/09 working and fast!
// 06/27/09 began

#ifndef CAT_LEGS_HPP
#define CAT_LEGS_HPP

#include <cat/Platform.hpp>

namespace cat {


#if defined(CAT_ARCH_64)

#define CAT_LEG_BITS 64
#define CAT_USE_LEGS_ASM64 /* use 64-bit assembly code inner loops */
#define CAT_USED_BITS(x) BitMath::BSR64(x) /* does not work if x = 0 */
	typedef u64 Leg;
#if !defined(CAT_COMPILER_MSVC)
	typedef u128 LegPair;
	typedef s128 LegPairSigned;
#else
#define CAT_NO_LEGPAIR
#endif

#elif defined(CAT_ARCH_32)

#define CAT_LEG_BITS 32
#define CAT_USED_BITS(x) BitMath::BSR32(x) /* does not work if x = 0 */
	typedef u32 Leg;
	typedef u64 LegPair;
	typedef s64 LegPairSigned;

#endif // CAT_ARCH_32

#if defined(CAT_NO_LEGPAIR)

// p(hi:lo) = A * B
#define CAT_LEG_MUL(A, B, p_hi, p_lo)	\
{										\
	p_lo = _umul128(A, B, &p_hi);		\
}

// p(hi:lo) = A * B + C
#define CAT_LEG_MULADD(A, B, C, p_hi, p_lo)	\
{											\
	u64 _C0 = C;							\
	p_lo = _umul128(A, B, &p_hi);			\
	p_hi += ((p_lo += _C0) < _C0);			\
}

// p(hi:lo) = A * B + C + D
#define CAT_LEG_MULADD2(A, B, C, D, p_hi, p_lo)	\
{												\
	u64 _C0 = C, _D0 = D;						\
	p_lo = _umul128(A, B, &p_hi);				\
	p_hi += ((p_lo += _C0) < _C0);				\
	p_hi += ((p_lo += _D0) < _D0);				\
}

#else // CAT_NO_LEGPAIR

// p(hi:lo) = A * B
#define CAT_LEG_MUL(A, B, p_hi, p_lo)		\
{											\
	LegPair _mt = (LegPair)(A) * (Leg)(B);	\
	(p_lo) = (Leg)_mt;						\
	(p_hi) = (Leg)(_mt >> CAT_LEG_BITS);	\
}

// p(hi:lo) = A * B + C
#define CAT_LEG_MULADD(A, B, C, p_hi, p_lo)				\
{														\
	LegPair _mt = (LegPair)(A) * (Leg)(B) + (Leg)(C);	\
	(p_lo) = (Leg)_mt;									\
	(p_hi) = (Leg)(_mt >> CAT_LEG_BITS);				\
}

// p(hi:lo) = A * B + C + D
#define CAT_LEG_MULADD2(A, B, C, D, p_hi, p_lo)						\
{																	\
	LegPair _mt = (LegPair)(A) * (Leg)(B) + (Leg)(C) + (Leg)(D);	\
	(p_lo) = (Leg)_mt;												\
	(p_hi) = (Leg)(_mt >> CAT_LEG_BITS);							\
}

// Q(hi:lo) = A(hi:lo) / B
#define CAT_LEG_DIV(A_hi, A_lo, B, Q_hi, Q_lo)						\
{																	\
	LegPair _A = ((LegPair)(A_hi) << CAT_LEG_BITS) | (Leg)(A_lo);	\
	LegPair _qt = (LegPair)(_A / (B));								\
	(Q_hi) = (Leg)(_qt >> CAT_LEG_BITS);							\
	(Q_lo) = (Leg)_qt;												\
}

#endif // CAT_NO_LEGPAIR

} // namespace cat

#endif // CAT_LEGS_HPP
