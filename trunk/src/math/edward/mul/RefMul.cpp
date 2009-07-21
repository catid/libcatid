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

// A reference multiplier to verify that PtMultiply() is functionally the same
void BigTwistedEdward::RefMul(const Leg *in_p, const Leg *in_k, u8 k_msb, Leg *out)
{
    Leg *one = Get(te_regs - TE_OVERHEAD);

    PtCopy(in_p, one);

    bool seen = false;

    if (k_msb)
    {
        seen = true;
        PtCopy(one, out);
    }

    for (int ii = library_legs - 1; ii >= 0; --ii)
    {
        for (Leg jj = (Leg)1 << (CAT_LEG_BITS-1); jj; jj >>= 1)
        {
            PtEDouble(out, out);
            if (in_k[ii] & jj)
            {
                if (seen)
                    PtEAdd(one, out, out);
                else
                {
                    seen = true;
                    PtCopy(one, out);
                }
            }
        }
    }
}
