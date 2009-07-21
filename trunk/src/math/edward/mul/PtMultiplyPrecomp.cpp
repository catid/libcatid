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
#include <cat/port/AlignedAlloc.hpp>
using namespace cat;

// Allocate a precomputed table of odd multiples of input point
// Free the table with Aligned::Delete()
Leg *BigTwistedEdward::PtMultiplyPrecompAlloc(const Leg *in, int w)
{
    int points = (POINT_STRIDE << (w - 1));

    Leg *out = Aligned::New<Leg>(points * POINT_STRIDE);

    PtMultiplyPrecomp(in, w, out);

    return out;
}

// Precompute odd multiples of input point
void BigTwistedEdward::PtMultiplyPrecomp(const Leg *in, int w, Leg *out)
{
    int neg_offset = POINT_STRIDE << (w - 2);

    // Precompute P and -P
    Leg *pre_a = out;
    PtCopy(in, pre_a);
    PtNegate(in, pre_a+neg_offset);

    // Precompute 2P
    Leg *pre_2 = TempPt;
    PtEDouble(in, pre_2);

    // Precompute 3P and -3P
    Leg *pre_b = pre_a+POINT_STRIDE;
    PtEAdd(pre_a, pre_2, pre_b);
    PtNegate(pre_b, pre_b+neg_offset);

    // Precompute +/- odd multiples of b by iteratively adding 2b
    int pos_point_count = 1 << (w-2);
    for (int table_index = 2; table_index < pos_point_count; table_index += 2)
    {
        pre_a = pre_b+POINT_STRIDE;
        PtEAdd(pre_b, pre_2, pre_a);
        PtNegate(pre_a, pre_a+neg_offset);

        pre_b = pre_a+POINT_STRIDE;
        PtEAdd(pre_a, pre_2, pre_b);
        PtNegate(pre_b, pre_b+neg_offset);
    }
}
