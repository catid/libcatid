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

#include <cat/math/BigRTL.hpp>
using namespace cat;

Leg BigRTL::MultiplyX(const Leg *in_a, Leg in_b, Leg *out)
{
    return MultiplyX(library_legs, in_a, in_b, out);
}

Leg BigRTL::MultiplyX(int legs, const Leg *in_a, Leg in_b, Leg *output)
{
    // ICC does a better job than my hand-written version by using SIMD instructions,
    // so I use its optimizer instead.
#if !defined(CAT_COMPILER_ICC) && defined(CAT_ASM_INTEL)

    CAT_ASM_BEGIN
        mov esi, [in_a]        ; esi = in_a
        mov ecx, [output]    ; ecx = output
        mov edi, [in_b]        ; edi = in_b

        ; edx:eax = A[0] * B
        mov eax, [esi]
        mul edi

        mov [ecx], eax        ; output[0] = eax
        sub [legs], 1
        jbe loop_done

loop_head:
            lea esi, [esi + 4]
            mov ebx, edx
            mov eax, [esi]
            mul edi
            lea ecx, [ecx + 4]
            add eax, ebx
            adc edx, 0
            mov [ecx], eax

        sub [legs], 1
        ja loop_head

loop_done:
        mov eax, edx
    CAT_ASM_END

#else

    Leg p_hi;

    CAT_LEG_MUL(in_a[0], in_b, p_hi, output[0]);

    for (int ii = 1; ii < legs; ++ii)
        CAT_LEG_MULADD(in_a[ii], in_b, p_hi, p_hi, output[ii]);

    return p_hi;

#endif
}
