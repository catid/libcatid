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

#include <cat/math/BigPseudoMersenne.hpp>
#include <cat/port/EndianNeutral.hpp>
#include "big_x64_asm.hpp"
using namespace cat;

BigPseudoMersenne::BigPseudoMersenne(int regs, int bits, int C)
    : BigRTL(regs + PM_OVERHEAD, bits)
{
    pm_regs = regs + PM_OVERHEAD;
    modulus_c = C;

    // Reserve a register to contain the full modulus
    CachedModulus = Get(pm_regs - 1);
    CopyModulus(CachedModulus);
}

void BigPseudoMersenne::CopyModulus(Leg *out)
{
    // Set low leg to -C, set all bits on the rest
    out[0] = 0 - modulus_c;
    memset(&out[1], 0xFF, (library_legs-1) * sizeof(Leg));
}

void BigPseudoMersenne::ReduceProductX(Leg overflow, Leg *inout)
{
    // Pseudo-Mersenne reduction
    Leg p_hi, p_lo;
    CAT_LEG_MULADD(overflow, modulus_c, inout[0], p_hi, p_lo);

    inout[0] = p_lo;

    // If the initial sum carried out,
    if ((inout[1] += p_hi) < p_hi)
    {
        // Ripple the carry out as far as needed
        for (int ii = 2; ii < library_legs; ++ii)
            if (++inout[ii]) return;

        // If we get here the sum carried out, so add C to low leg
        if ((inout[0] += modulus_c) < modulus_c)
        {
            // Ripple the carry out as far as needed
            for (int ii = 1; ii < library_legs; ++ii)
                if (++inout[ii]) break;
        }
    }
}

void BigPseudoMersenne::ReduceProduct(const Leg *in_hi, const Leg *in_lo, Leg *out)
{
    // Pseudo-Mersenne reduction
    Leg overflow = MultiplyXAdd(in_hi, modulus_c, in_lo, out);

    ReduceProductX(overflow, out);
}

void BigPseudoMersenne::MrReduce(Leg *inout)
{
    // Subtract the modulus once if the input is greater or equal to it

    for (int ii = 1; ii < library_legs; ++ii)
        if (~inout[ii]) return;

    if (inout[0] >= (0 - modulus_c))
        AddX(inout, modulus_c);
}

void BigPseudoMersenne::MrAdd(const Leg *in_a, const Leg *in_b, Leg *out)
{
#if defined(CAT_USE_LEGS_ASM64)
    if (library_legs == 4)
    {
        bpm_add_4(modulus_c, in_a, in_b, out);
        return;
    }
#endif

    // If the addition overflowed, add C
    if (Add(in_a, in_b, out))
        AddX(out, modulus_c);
}

void BigPseudoMersenne::MrAddX(Leg *inout, Leg x)
{
    // If the addition overflowed, add C
    if (AddX(inout, x))
        AddX(inout, modulus_c);
}

void BigPseudoMersenne::MrSubtract(const Leg *in_a, const Leg *in_b, Leg *out)
{
#if defined(CAT_USE_LEGS_ASM64)
    if (library_legs == 4)
    {
        bpm_sub_4(modulus_c, in_a, in_b, out);
        return;
    }
#endif

    // If the subtraction overflowed, subtract C
    if (Subtract(in_a, in_b, out))
        SubtractX(out, modulus_c);
}

void BigPseudoMersenne::MrSubtractX(Leg *inout, Leg x)
{
    // If the addition overflowed, add C
    if (SubtractX(inout, x))
        SubtractX(inout, modulus_c);
}

void BigPseudoMersenne::MrNegate(const Leg *in, Leg *out)
{
    // It's like SubtractX: out = m - in = ~in-c+1 = ~in - (c-1)
    Leg t = ~in[0];
    Leg x = modulus_c - 1;
    out[0] = t - x;

    int ii = 1;

    // If the initial difference borrowed in,
    if (t < x)
    {
        // Ripple the borrow in as far as needed
        while (ii < library_legs)
        {
            t = ~in[ii];
            out[ii++] = t - 1;
            if (t) break;
        }
    }

    // Invert remaining bits
    for (; ii < library_legs; ++ii)
        out[ii] = ~in[ii];
}

void BigPseudoMersenne::MrDouble(const Leg *in, Leg *out)
{
    // If the doubling overflowed, add C
    if (Double(in, out))
        AddX(out, modulus_c);
}

void BigPseudoMersenne::MrMultiply(const Leg *in_a, const Leg *in_b, Leg *out)
{
#if defined(CAT_USE_LEGS_ASM64)
    if (library_legs == 4)
    {
        bpm_mul_4(modulus_c, in_a, in_b, out);
        return;
    }
#endif

    Leg *T_hi = Get(pm_regs - 2);
    Leg *T_lo = Get(pm_regs - 3);

    Multiply(in_a, in_b, T_lo);

    ReduceProduct(T_hi, T_lo, out);
}

void BigPseudoMersenne::MrMultiplyX(const Leg *in_a, Leg in_b, Leg *out)
{
#if defined(CAT_USE_LEGS_ASM64)
    if (library_legs == 4)
    {
        bpm_mulx_4(modulus_c, in_a, in_b, out);
        return;
    }
#endif

    Leg overflow = MultiplyX(in_a, in_b, out);

    ReduceProductX(overflow, out);
}

void BigPseudoMersenne::MrSquare(const Leg *in, Leg *out)
{
#if defined(CAT_USE_LEGS_ASM64)
    if (library_legs == 4)
    {
        bpm_sqr_4(modulus_c, in, out);
        return;
    }
#endif

    Leg *T_hi = Get(pm_regs - 2);
    Leg *T_lo = Get(pm_regs - 3);

    Square(in, T_lo);

    ReduceProduct(T_hi, T_lo, out);
}

void BigPseudoMersenne::MrInvert(const Leg *in, Leg *out)
{
    // Modular inverse with prime modulus:
    // out = in^-1 = in ^ (m - 2) [using Euler's totient function]

    Leg *T = Get(pm_regs - 4);
    Leg *S = Get(pm_regs - 5);

    // Optimal window size is sqrt(bits-16)
    const int w = 16; // Constant window size, optimal for 256-bit modulus

    // Perform exponentiation for the first w bits
    Copy(in, S);
    int ctr = w - 1;
    while (ctr--)
    {
        MrSquare(S, S);
        MrMultiply(S, in, S);
    }

    // Store result in a temporary register
    Copy(S, T);

    // NOTE: This assumes that modulus_c < 65534 = (2^w - 2)
    int one_frames = (RegBytes()*8 - w*2) / w;
    while (one_frames--)
    {
        // Just multiply once re-using the first result, every 16 bits
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrMultiply(S, T, S);
    }

    // For the final leg just do bitwise exponentiation
    // NOTE: Makes use of the fact that the window size is a power of two
    Leg m_low = 0 - (modulus_c + 2);
    for (Leg bit = (Leg)1 << (w - 1); bit; bit >>= 1)
    {
        MrSquare(S, S);

        if (m_low & bit)
            MrMultiply(S, in, S);
    }

    Copy(S, out);
}

void BigPseudoMersenne::MrSquareRoot(const Leg *in, Leg *out)
{
    // Square root for specially formed modulus:
    // out = in ^ (m + 1)/4

    // Same algorithm from MrInvert()
    Leg *T = Get(pm_regs - 4);
    Leg *S = Get(pm_regs - 5);

    // Optimal window size is sqrt(bits-16)
    const int w = 16; // Constant window size, optimal for 256-bit modulus

    // Perform exponentiation for the first w bits
    Copy(in, S);
    int ctr = w - 1;
    while (ctr--)
    {
        MrSquare(S, S);
        MrMultiply(S, in, S);
    }

    // Store result in a temporary register
    Copy(S, T);

    // NOTE: This assumes that modulus_c < 16384 = 2^(w-2)
    int one_frames = (RegBytes()*8 - w*2) / w;
    while (one_frames--)
    {
        // Just multiply once re-using the first result, every 16 bits
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrSquare(S, S); MrSquare(S, S); MrSquare(S, S); MrSquare(S, S);
        MrMultiply(S, T, S);
    }

    // For the final leg just do bitwise exponentiation
    // NOTE: Makes use of the fact that the window size is a power of two
    Leg m_low = 1 - modulus_c;
    for (Leg bit = (Leg)1 << (w - 1); bit >= 4; bit >>= 1)
    {
        MrSquare(S, S);

        if (m_low & bit)
            MrMultiply(S, in, S);
    }

    Copy(S, out);
}
