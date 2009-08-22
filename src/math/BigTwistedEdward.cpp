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

BigTwistedEdward::BigTwistedEdward(int regs, int bits, int modulusC, int paramD, const u8 *Q, const u8 *GenPt)
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
	CurveQ = Get(te_regs - 9);
    TempPt = Get(te_regs - 13);
    Generator = Get(te_regs - 17);

	Load(GenPt, RegBytes(), Generator);
	Load(GenPt + RegBytes(), RegBytes(), Generator + library_legs);
	PtUnpack(Generator);

	Load(Q, RegBytes(), CurveQ);
}

// Unpack an Extended Projective point (X,Y,T,Z) from affine point (x,y)
void BigTwistedEdward::PtUnpack(Leg *inout)
{
    MrMultiply(inout+XOFF, inout+YOFF, inout+TOFF);
    CopyX(1, inout+ZOFF);
}

// Set a point to the identity
void BigTwistedEdward::PtIdentity(Leg *inout)
{
    CopyX(0, inout+XOFF);
    CopyX(1, inout+YOFF);
    CopyX(0, inout+TOFF);
    CopyX(1, inout+ZOFF);
}

// Check if the affine point (x,y) is the additive identity (0,1)
bool BigTwistedEdward::IsAffineIdentity(const Leg *in)
{
	return EqualX(in+XOFF, 0) && EqualX(in+YOFF, 1);
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

// Strangely enough, including these all in the same source file improves performance
// in Visual Studio by almost 50%, which is odd because MSVC was one of the first
// compilers to support "link time optimization."

#include "edward/io/PtFillRandomX.cpp"
#include "edward/io/PtGenerate.cpp"
#include "edward/io/PtNormalize.cpp"
#include "edward/io/PtSolveAffineY.cpp"
#include "edward/io/PtValidAffine.cpp"
#include "edward/io/SaveAffineX.cpp"
#include "edward/io/SaveAffineXY.cpp"
#include "edward/addsub/PtAdd.cpp"
#include "edward/addsub/PtNegate.cpp"
#include "edward/addsub/PtSubtract.cpp"
#include "edward/addsub/PtDouble.cpp"
#include "edward/addsub/PtDoubleZ1.cpp"
#include "edward/mul/PtMultiplyPrecomp.cpp"
#include "edward/mul/PtPrecompAddSub.cpp"
#include "edward/mul/PtMultiply.cpp"
#include "edward/mul/RefMul.cpp"
