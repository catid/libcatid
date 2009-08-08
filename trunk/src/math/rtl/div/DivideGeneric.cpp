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
#include <cat/math/BitMath.hpp>
using namespace cat;

#if !defined(CAT_NO_LEGPAIR)

Leg BigRTL::DivideX(const Leg *in_a, Leg in_b, Leg *out)
{
    Leg R = 0;

    for (int leg = library_legs-1; leg >= 0; --leg)
    {
        LegPair N = ((LegPair)R << CAT_LEG_BITS) | in_a[leg];
        out[leg] = (Leg)(N / in_b);
        R = (Leg)(N % in_b);
    }

    return R;
}

Leg BigRTL::ModulusX(const Leg *in_a, Leg in_b)
{
    Leg R = in_a[library_legs-1];
    Leg leg = library_legs;

    if (R >= in_b) R = 0;
    else --leg;

    while (leg--)
        R = (Leg)((((LegPair)R << CAT_LEG_BITS) | in_a[leg]) % in_b);

    return R;
}

void BigRTL::DivideCore(int A_used, Leg A_overflow, Leg *A, int B_used, Leg *B, Leg *Q)
{
    int offset = A_used - B_used;
    Leg B_high = B[B_used-1];
    Leg *A_offset = A + offset;

    // Set q_hi when A >= B
    Leg q_hi;
    if (B_high > A_overflow)
        q_hi = 0;
    else
    {
        q_hi = 1;

        if (B_high == A_overflow)
        for (int ii = B_used-2; ii >= 0; --ii)
        {
            Leg a = A_offset[ii];
            Leg b = B[ii];

            if (b < a) { break; }
            if (b > a) { q_hi = 0; break; }
        }
    }

    Q[offset+1] = q_hi;

    // If q_hi is set, subtract B from A
    if (q_hi)
    {
        u8 c = 0;
        for (int ii = 0; ii < B_used-1; ++ii)
        {
            // Calculate difference
            Leg a = A_offset[ii+1];
            Leg b = B[ii];
            Leg d = a - b - c;

            // Calculate borrow-out
            c = c ? (a < d || b == ~(Leg)0) : (a < b);

            A_offset[ii+1] = d;
        }

        A_overflow -= B_high + c;
    }

    do
    {
        Leg *A_offset = A + offset;

        // Trial divide two highest A legs by highest B leg
        Leg q_hi, q_lo;
        CAT_LEG_DIV(A_overflow, A_offset[B_used - 1], B_high, q_hi, q_lo);

        Leg p_hi = 0, p_lo, B_last = 0;
        u8 c = 0;

        if (q_hi)
        {
            // A -= (1 | q_lo) * B
            for (int ii = 0; ii < B_used; ++ii)
            {
                Leg x = B[ii];
                CAT_LEG_MULADD2(x, q_lo, p_hi, B_last, p_hi, p_lo);
                B_last = x;

                Leg a = A_offset[ii];
                Leg d = a - p_lo - c;

                c = c ? (a < d || p_lo == ~(Leg)0) : (a < p_lo);

                A_offset[ii] = d;
            }

            A_overflow -= B_last;
        }
        else
        {
            // A -= q_lo * B
            for (int ii = 0; ii < B_used; ++ii)
            {
                CAT_LEG_MULADD(B[ii], q_lo, p_hi, p_hi, p_lo);

                Leg a = A_offset[ii];
                Leg d = a - p_lo - c;

                c = c ? (a < d || p_lo == ~(Leg)0) : (a < p_lo);

                A_offset[ii] = d;
            }
        }

        A_overflow -= p_hi + c;

        // Correct trial divide error <= 2
        while (A_overflow)
        {
            --q_lo;

            c = 0;
            for (int ii = 0; ii < B_used; ++ii)
            {
                Leg a = A_offset[ii];
                Leg sum = a + B[ii] + c;

                c = c ? sum <= a : sum < a;

                A_offset[ii] = sum;
            }

            A_overflow += c;
        }

        Q[offset] = q_lo;

        A_overflow = A_offset[B_used - 1];
    } while (offset--);
}

#endif // CAT_ARCH_32
