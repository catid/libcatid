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

void BigRTL::Square(const Leg *input, Leg *output)
{
    Leg *cross = Get(library_regs - 2);

    // ICC does a better job than my hand-written version by using SIMD instructions,
    // so I use its optimizer instead.
#if !defined(CAT_COMPILER_ICC) && defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    int legs = library_legs;

    CAT_ASSEMBLY_BLOCK // VS.NET, x86, 32-bit words
    {
        mov esi, [input]     ; esi = in
        mov ecx, [output]    ; ecx = output
        mov edi, [legs]      ; edi = leg count

loop_head:
        ; edx:eax = in[0] * in[0]
        mov eax, [esi]
        mul eax
        mov [ecx], eax
        mov [ecx+4], edx
        lea ecx, [ecx + 8]
        lea esi, [esi + 4]

        sub edi, 1
        ja loop_head
    }

#else

    // Calculate square products
    for (int ii = 0; ii < library_legs; ++ii)
        CAT_LEG_MUL(input[ii], input[ii], output[ii*2+1], output[ii*2]);

#endif

    // Calculate cross products
    cross[library_legs] = MultiplyX(library_legs-1, input+1, input[0], cross+1);
    for (int ii = 1; ii < library_legs-1; ++ii)
        cross[library_legs + ii] = MultiplyXAdd(library_legs-1-ii, input+1+ii, input[ii], cross+1+ii*2, cross+1+ii*2);

    // Multiply the cross product by 2 and add it to the square products
    output[library_legs*2-1] += DoubleAdd(library_legs*2-2, cross+1, output+1, output+1);
}
