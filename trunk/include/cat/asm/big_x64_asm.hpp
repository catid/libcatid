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

// 06/27/09 began

// Forward declaration for assembly routines in big_x64_*.asm

#ifndef CAT_BIG_X64_ASM_HPP
#define CAT_BIG_X64_ASM_HPP

#include <cat/math/Legs.hpp>

#if defined(CAT_WORD_64)

namespace cat {


extern "C" void bpm_add_4(Leg modulus_c, const Leg *in_a, const Leg *in_b, Leg *out);
extern "C" void bpm_sub_4(Leg modulus_c, const Leg *in_a, const Leg *in_b, Leg *out);
extern "C" void bpm_mul_4(Leg modulus_c, const Leg *in_a, const Leg *in_b, Leg *out);
extern "C" void bpm_mulx_4(Leg modulus_c, const Leg *in_a, Leg in_b, Leg *out);
extern "C" void bpm_sqr_4(Leg modulus_c, const Leg *in, Leg *out);

extern "C" Leg divide64_x(Leg legs, const Leg *in_a, const Leg in_b, Leg *out_q);
extern "C" Leg modulus64_x(Leg legs, const Leg *in_a, const Leg in_b);
extern "C" void divide64_core(Leg A_used, Leg A_overflow, const Leg *A, Leg B_used, const Leg *B, Leg *Q);


} // namespace cat

#endif // CAT_WORD_64

#endif // CAT_BIG_X64_ASM_HPP
