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

BigTwistedEdward::BigTwistedEdward(int regs, int bits, int modulusC, int paramD)
    : BigPseudoMersenne(regs + TE_OVERHEAD, bits, modulusC)
{
    te_regs = regs + TE_OVERHEAD;
    curve_d = paramD;

    YOFF = library_legs;
    TOFF = library_legs * 2;
    ZOFF = library_legs * 3;
    POINT_STRIDE = library_legs * POINT_REGS;

    A = Get(te_regs - 1);
    B = Get(te_regs - 2);
    C = Get(te_regs - 3);
    D = Get(te_regs - 4);
    E = Get(te_regs - 5);
    F = Get(te_regs - 6);
    G = Get(te_regs - 7);
    H = Get(te_regs - 8);
    TempPt = Get(te_regs - 12);
}

// Unpack an EdPoint from affine point (x,y)
void BigTwistedEdward::PtUnpack(Leg *inout)
{
    CopyX(1, inout+ZOFF);
    MrMultiply(inout+XOFF, inout+YOFF, inout+TOFF);
}

void BigTwistedEdward::PtCopy(const Leg *in, Leg *out)
{
    Copy(in+XOFF, out+XOFF);
    Copy(in+YOFF, out+YOFF);
    Copy(in+TOFF, out+TOFF);
    Copy(in+ZOFF, out+ZOFF);
}

// out(X,Y) = (X,Y) without attempting to convert to affine from projective
void BigTwistedEdward::SaveProjectiveXY(const Leg *in, void *out_x, void *out_y)
{
    Save(in+XOFF, out_x, RegBytes());
    Save(in+YOFF, out_y, RegBytes());
}

// out(X,Y,Z,T) = (in_x,in_y), returns false if the coordinates are invalid
bool BigTwistedEdward::LoadVerifyAffineXY(const void *in_x, const void *in_y, Leg *out)
{
    Load(in_x, RegBytes(), out+XOFF);
    Load(in_y, RegBytes(), out+YOFF);

    return PtValidAffine(out);
}
