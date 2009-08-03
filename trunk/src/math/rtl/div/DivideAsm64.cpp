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

#if defined(CAT_WORD_64)

#include <cat/asm/big_x64_asm.hpp>

Leg BigRTL::DivideX(const Leg *in_a, Leg in_b, Leg *out)
{
    return divide64_x(library_legs, in_a, in_b, out);
}

Leg BigRTL::ModulusX(const Leg *in_a, Leg in_b)
{
    return modulus64_x(library_legs, in_a, in_b);
}

void BigRTL::DivideCore(int A_used, Leg A_overflow, Leg *A, int B_used, Leg *B, Leg *Q)
{
    divide64_core(A_used, A_overflow, A, B_used, B, Q);
}

#endif // CAT_WORD_64
