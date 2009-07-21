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

#include <cat/math/BigTwistedEdward.hpp>
using namespace cat;

// Extended Twisted Edwards Scalar Multiplication k*p
// CAN *NOT* BE followed by a Pt[E]Add()
void BigTwistedEdward::PtMultiply(const Leg *in_p, const Leg *in_k, u8 k_msb, Leg *out)
{
    Leg *DefaultPrecomp = Get(te_regs - TE_OVERHEAD);

    PtMultiplyPrecomp(in_p, WINDOW_BITS, DefaultPrecomp);
    PtMultiply(DefaultPrecomp, WINDOW_BITS, in_k, k_msb, out);
}

// w-MOF lookup table for PtMultiply()
struct {
    u8 add_index; // nth odd number to add: 0=0,1=1,2=3,3=5,4=7,...
    u8 doubles_after; // number of doubles to perform after add
} static const MOF_LUT[128] = {
    {0,0},{0,1},{1,0},{0,2},{2,0},{1,1},{3,0},{0,3},
    {4,0},{2,1},{5,0},{1,2},{6,0},{3,1},{7,0},{0,4},
    {8,0},{4,1},{9,0},{2,2},{10,0},{5,1},{11,0},{1,3},
    {12,0},{6,1},{13,0},{3,2},{14,0},{7,1},{15,0},{0,5},
    {16,0},{8,1},{17,0},{4,2},{18,0},{9,1},{19,0},{2,3},
    {20,0},{10,1},{21,0},{5,2},{22,0},{11,1},{23,0},{1,4},
    {24,0},{12,1},{25,0},{6,2},{26,0},{13,1},{27,0},{3,3},
    {28,0},{14,1},{29,0},{7,2},{30,0},{15,1},{31,0},{0,6},
    {32,0},{16,1},{33,0},{8,2},{34,0},{17,1},{35,0},{4,3},
    {36,0},{18,1},{37,0},{9,2},{38,0},{19,1},{39,0},{2,4},
    {40,0},{20,1},{41,0},{10,2},{42,0},{21,1},{43,0},{5,3},
    {44,0},{22,1},{45,0},{11,2},{46,0},{23,1},{47,0},{1,5},
    {48,0},{24,1},{49,0},{12,2},{50,0},{25,1},{51,0},{6,3},
    {52,0},{26,1},{53,0},{13,2},{54,0},{27,1},{55,0},{3,4},
    {56,0},{28,1},{57,0},{14,2},{58,0},{29,1},{59,0},{7,3},
    {60,0},{30,1},{61,0},{15,2},{62,0},{31,1},{63,0},{0,7}
};

// Extended Twisted Edwards Scalar Multiplication k*p
// CAN *NOT* BE followed by a Pt[E]Add()
void BigTwistedEdward::PtMultiply(const Leg *in_precomp, int w, const Leg *in_k, u8 k_msb, Leg *out)
{
    // Begin multiplication loop
    bool seen_high_bit;
    int leg = library_legs - 1;
    Leg bits, last_leg;
    int offset, doubles_before = 0, doubles_skip = 0;

    if (k_msb)
    {
        last_leg = k_msb;
        offset = CAT_LEG_BITS + w;
        seen_high_bit = true;
        PtCopy(in_precomp, out);
    }
    else
    {
        last_leg = in_k[leg--];
        offset = w;
        seen_high_bit = false;
    }

    for (;;)
    {
        // If still processing bits from current leg of k,
        if (offset <= CAT_LEG_BITS)
        {
            // Select next bits from current leg of k
            bits = last_leg >> (CAT_LEG_BITS - offset);
        }
        else if (leg >= 0)
        {
            // Next bits straddle the previous and next legs of k
            Leg new_leg = in_k[leg--];
            offset -= CAT_LEG_BITS;
            bits = (last_leg << offset) | (new_leg >> (CAT_LEG_BITS - offset));
            last_leg = new_leg;
        }
        else if (offset <= CAT_LEG_BITS + w)
        {
            // Pad zeroes on the right
            bits = last_leg << (offset - CAT_LEG_BITS);

            // Skip padding - 1 doubles after leaving this loop
            doubles_skip = offset - CAT_LEG_BITS - 1;
        }
        else break;

        // Invert low bits if negative, and mask out high bit
        Leg z = (bits ^ (0 - ((bits >> w) & 1))) & ((1 << w) - 1);

        if (!z)
        {
            doubles_before += w;

            // Timing attack protection
            PtAdd(out, in_precomp, TempPt);
        }
        else
        {
            // Extract the operation for this table entry
            z = (z - 1) >> 1;
            int neg_mask = (bits & ((Leg)1 << w)) >> 2;
            const Leg *precomp = in_precomp + (MOF_LUT[z].add_index + neg_mask) * POINT_STRIDE;
            int doubles_after = MOF_LUT[z].doubles_after;

            // Perform doubles before addition
            doubles_before += w - doubles_after;

            // There will always be at least one doubling to perform here
            while (--doubles_before)
                PtDouble(out, out);
            PtEDouble(out, out);

            // If we have seen the high bit yet,
            if (seen_high_bit)
            {
                // Perform addition or subtraction from the precomputed table
                PtAdd(out, precomp, out);
            }
            else
            {
                // On the first seen bit, product = precomputed point
                PtCopy(precomp, out);
                seen_high_bit = true;
            }

            // Accumulate doubles after addition
            doubles_before = doubles_after;
        }

        // set up offset for next time around
        offset += w;
    }

    // Skip some doubles at the end due to window underrun
    if (doubles_before > doubles_skip)
    {
        doubles_before -= doubles_skip;

        // Perform trailing doubles
        while (doubles_before--)
            PtDouble(out, out);
    }
}
