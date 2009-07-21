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

// Fill the X coordinate of the point with a random value
void BigTwistedEdward::PtFillRandomX(IRandom *prng, Leg *out)
{
    // Generate an affine X coordinate point that is unbiased
    do prng->Generate(out+XOFF, RegBytes());
    while (!Less(out+XOFF, GetModulus()));
}

// Generate a random point on the curve that is not part of a small subgroup
void BigTwistedEdward::PtGenerate(IRandom *prng, Leg *out)
{
    // Generate affine (x,y) point on the curve
    do {
        PtFillRandomX(prng, out);
        PtSolveAffineY(out);
    } while (!IsValidAffineXY(out));

    // #E(Fp) = large prime * cofactor h
    // Assumes cofactor h = 4
    // P = hP, to insure it is in the large prime-order subgroup
    PtDoubleZ1(out, out);
    PtEDouble(out, out);
}

// Solve for Y given the X point on a curve
void BigTwistedEdward::PtSolveAffineY(Leg *inout)
{
    // y = sqrt[(1 + x^2) / (1 - d*x^2)]

    // B = x^2
    MrSquare(inout+XOFF, B);

    // A = 1/(1 - d*B)
    MrMultiplyX(B, curve_d, A);
    MrNegate(A, A);
    MrAddX(A, 1);
    MrInvert(A, A);

    // y = sqrt(A*(B+1))
    MrAddX(B, 1);
    MrMultiply(A, B, inout+YOFF);
    MrSquareRoot(inout+YOFF, inout+YOFF);
    MrReduce(inout+YOFF);
}

// Verify that the point (x,y) exists on the given curve
bool BigTwistedEdward::IsValidAffineXY(const Leg *in)
{
    // 0 = 1 + d*x^2*y^2 + x^2 - y^2

    // A = x^2
    MrSquare(in+XOFF, A);

    // B = y^2
    MrSquare(in+YOFF, B);

    // C = A * B * d + 1 + A - B
    MrMultiply(A, B, C);
    MrMultiplyX(C, curve_d, C);
    MrAddX(C, 1);
    MrAdd(C, A, C);
    MrSubtract(C, B, C);
    MrReduce(C);

    // If the result is zero, it is on the curve
    return IsZero(C);
}

// out(x) = X/Z
void BigTwistedEdward::SaveAffineX(const Leg *in, void *out_x)
{
    // If the coordinates are already normalized to affine, then we can just save what is there
    if (EqualX(in+ZOFF, 1))
    {
        Save(in+XOFF, out_x, RegBytes());
    }
    else
    {
        // A = 1 / in.Z
        MrInvert(in+ZOFF, A);

        // B = A * in.X
        MrMultiply(in+XOFF, A, B);
        MrReduce(B);

        Save(B, out_x, RegBytes());
    }
}

// out(x,y) = (X/Z,Y/Z)
void BigTwistedEdward::SaveAffineXY(const Leg *in, void *out_x, void *out_y)
{
    // If the coordinates are already normalized to affine, then we can just save what is there
    if (EqualX(in+ZOFF, 1))
    {
        Save(in+XOFF, out_x, RegBytes());
        Save(in+YOFF, out_y, RegBytes());
    }
    else
    {
        // A = 1 / in.Z
        MrInvert(in+ZOFF, A);

        // B = A * in.X
        MrMultiply(in+XOFF, A, B);
        MrReduce(B);
        Save(B, out_x, RegBytes());

        // C = A * in.Y
        MrMultiply(in+YOFF, A, C);
        MrReduce(C);
        Save(C, out_y, RegBytes());
    }
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

    return IsValidAffineXY(out);
}

// Compute affine coordinates for (X,Y), set Z=1, and compute T = xy
void BigTwistedEdward::PtNormalize(const Leg *in, Leg *out)
{
    // A = 1 / in.Z
    MrInvert(in+ZOFF, A);

    // out.X = A * in.X
    MrMultiply(in+XOFF, A, out+XOFF);
    MrReduce(out+XOFF);

    // out.Y = A * in.Y
    MrMultiply(in+YOFF, A, out+YOFF);
    MrReduce(out+YOFF);

    PtUnpack(out);
}

// Extended Twisted Edwards Negation Formula in 2a
void BigTwistedEdward::PtNegate(const Leg *in, Leg *out)
{
    // -(X : Y : T : Z) = (-X : Y : -T : Z)
    MrNegate(in+XOFF, out+XOFF);
    Copy(in+YOFF, out+YOFF);
    MrNegate(in+TOFF, out+TOFF);
    Copy(in+ZOFF, out+ZOFF);
}

// Extended Twisted Edwards Unified Addition Formula (works when both inputs are the same) in 8M 1D 8A
// CAN BE followed by a Pt[E]Add()
void BigTwistedEdward::PtEAdd(const Leg *in_a, const Leg *in_b, Leg *out)
{
    // A = (Y1 - X1) * (Y2 - X2)
    MrSubtract(in_a+YOFF, in_a+XOFF, C);
    MrSubtract(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, A);

    // B = (Y1 + X1) * (Y2 + X2)
    MrAdd(in_a+YOFF, in_a+XOFF, C);
    MrAdd(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, B);

    // C = 2 * d * T1 * T2 (can remove multiplication by d if inputs are known to be different)
    MrMultiply(in_a+TOFF, in_b+TOFF, C);
    MrMultiplyX(C, curve_d * 2, C);

    // D = 2 * Z1 * Z2
    MrMultiply(in_a+ZOFF, in_b+ZOFF, D);
    MrDouble(D, D);

    // E = B - A, F = D - C, G = D + C, H = B + A
    MrSubtract(B, A, E);
    MrSubtract(D, C, F);
    MrAdd(D, C, G);
    MrAdd(B, A, H);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
    MrMultiply(E, H, out+TOFF);
    MrMultiply(F, G, out+ZOFF);
}

// Extended Twisted Edwards Unified Addition Formula (works when both inputs are the same) in 7M 1D 8A
// CAN *NOT* BE followed by a Pt[E]Add()
void BigTwistedEdward::PtAdd(const Leg *in_a, const Leg *in_b, Leg *out)
{
    // A = (Y1 - X1) * (Y2 - X2)
    MrSubtract(in_a+YOFF, in_a+XOFF, C);
    MrSubtract(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, A);

    // B = (Y1 + X1) * (Y2 + X2)
    MrAdd(in_a+YOFF, in_a+XOFF, C);
    MrAdd(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, B);

    // C = 2 * d * T1 * T2 (can remove multiplication by d if inputs are known to be different)
    MrMultiply(in_a+TOFF, in_b+TOFF, C);
    MrMultiplyX(C, curve_d * 2, C);

    // D = 2 * Z1 * Z2
    MrMultiply(in_a+ZOFF, in_b+ZOFF, D);
    MrDouble(D, D);

    // E = B - A, F = D - C, G = D + C, H = B + A
    MrSubtract(B, A, E);
    MrSubtract(D, C, F);
    MrAdd(D, C, G);
    MrAdd(B, A, H);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
    //MrMultiply(E, H, out+TOFF);
    MrMultiply(F, G, out+ZOFF);
}

// Extended Twisted Edwards Unified Subtraction Formula (works when both inputs are the same) in 8M 1D 8A
// CAN BE followed by a Pt[E]Add()
void BigTwistedEdward::PtESubtract(const Leg *in_a, const Leg *in_b, Leg *out)
{
    // Negation: X2 = -X2, T2 = -T2

    // A = (Y1 - X1) * (Y2 + X2)
    MrSubtract(in_a+YOFF, in_a+XOFF, C);
    MrAdd(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, A);

    // B = (Y1 + X1) * (Y2 - X2)
    MrAdd(in_a+YOFF, in_a+XOFF, C);
    MrSubtract(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, B);

    // C = 2 * d * T1 * T2 (can remove multiplication by d if inputs are known to be different)
    MrMultiply(in_a+TOFF, in_b+TOFF, C);
    MrMultiplyX(C, curve_d * 2, C);
    // C = -C

    // D = 2 * Z1 * Z2
    MrMultiply(in_a+ZOFF, in_b+ZOFF, D);
    MrDouble(D, D);

    // E = B - A, F = D + C, G = D - C, H = B + A
    MrSubtract(B, A, E);
    MrAdd(D, C, F);
    MrSubtract(D, C, G);
    MrAdd(B, A, H);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
    MrMultiply(E, H, out+TOFF);
    MrMultiply(F, G, out+ZOFF);
}

// Extended Twisted Edwards Unified Subtraction Formula (works when both inputs are the same) in 7M 1D 8A
// CAN *NOT* BE followed by a Pt[E]Add()
void BigTwistedEdward::PtSubtract(const Leg *in_a, const Leg *in_b, Leg *out)
{
    // Negation: X2 = -X2, T2 = -T2

    // A = (Y1 - X1) * (Y2 + X2)
    MrSubtract(in_a+YOFF, in_a+XOFF, C);
    MrAdd(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, A);

    // B = (Y1 + X1) * (Y2 - X2)
    MrAdd(in_a+YOFF, in_a+XOFF, C);
    MrSubtract(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, B);

    // C = 2 * d * T1 * T2 (can remove multiplication by d if inputs are known to be different)
    MrMultiply(in_a+TOFF, in_b+TOFF, C);
    MrMultiplyX(C, curve_d * 2, C);
    // C = -C

    // D = 2 * Z1 * Z2
    MrMultiply(in_a+ZOFF, in_b+ZOFF, D);
    MrDouble(D, D);

    // E = B - A, F = D + C, G = D - C, H = B + A
    MrSubtract(B, A, E);
    MrAdd(D, C, F);
    MrSubtract(D, C, G);
    MrAdd(B, A, H);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
    //MrMultiply(E, H, out+TOFF);
    MrMultiply(F, G, out+ZOFF);
}

// Extended Twisted Edwards Dedicated Doubling Formula in 4M 4S 5a
// CAN BE followed by a Pt[E]Add()
void BigTwistedEdward::PtEDouble(const Leg *in, Leg *out)
{
    // A = X1^2, B = Y1^2, C = 2 * Z1^2
    MrSquare(in+XOFF, A);
    MrSquare(in+YOFF, B);
    MrSquare(in+ZOFF, C);
    MrDouble(C, C);

    // G = -A + B, F = G - C, H = -A - B
    MrNegate(A, A);
    MrAdd(A, B, G);
    MrSubtract(G, C, F);
    MrSubtract(A, B, H);

    // E = (X1 + Y1)^2 + H
    MrAdd(in+XOFF, in+YOFF, E);
    MrSquare(E, E);
    MrAdd(E, H, E);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
    MrMultiply(E, H, out+TOFF);
    MrMultiply(F, G, out+ZOFF);
}

// Extended Twisted Edwards Dedicated Doubling Formula in 3M 4S 5a
// CAN *NOT* BE followed by a Pt[E]Add()
void BigTwistedEdward::PtDouble(const Leg *in, Leg *out)
{
    // A = X1^2, B = Y1^2, C = 2 * Z1^2
    MrSquare(in+XOFF, A);
    MrSquare(in+YOFF, B);
    MrSquare(in+ZOFF, C);
    MrDouble(C, C);

    // G = -A + B, F = G - C, H = -A - B
    MrNegate(A, A);
    MrAdd(A, B, G);
    MrSubtract(G, C, F);
    MrSubtract(A, B, H);

    // E = (X1 + Y1)^2 + H
    MrAdd(in+XOFF, in+YOFF, E);
    MrSquare(E, E);
    MrAdd(E, H, E);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
    //MrMultiply(E, H, out+TOFF);
    MrMultiply(F, G, out+ZOFF);
}

// Extended Twisted Edwards Dedicated Doubling Formula in 3M 3S 4a
// Assumes Z=1, CAN *NOT* BE followed by a Pt[E]Add()
void BigTwistedEdward::PtDoubleZ1(const Leg *in, Leg *out)
{
    // A = X1^2, B = Y1^2, C = 2 * Z1^2 = 2
    MrSquare(in+XOFF, A);
    MrSquare(in+YOFF, B);
    //MrSquare(in+ZOFF, C);
    //MrDouble(C, C);

    // G = -A + B, F = G - C = G - 2, H = -A - B
    MrNegate(A, A);
    MrAdd(A, B, G);
    //MrSubtract(G, C, F);
    Copy(G, F);
    MrSubtractX(F, 2); // C = 2
    MrSubtract(A, B, H);

    // E = (X1 + Y1)^2 + H
    MrAdd(in+XOFF, in+YOFF, E);
    MrSquare(E, E);
    MrAdd(E, H, E);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, out+XOFF);
    MrMultiply(G, H, out+YOFF);
    //MrMultiply(E, H, out+TOFF);
    MrMultiply(F, G, out+ZOFF);
}

// Simultaneous Add and Subtract for efficient precomputation (A +/- B) in 14M 1D 11a (versus 16M 2D 16a)
void BigTwistedEdward::PtPrecompAddSub(const Leg *in_a, const Leg *in_b, Leg *sum, Leg *diff, int neg_offset)
{
    // A = (Y1 - X1) * (Y2 - X2)
    MrSubtract(in_a+YOFF, in_a+XOFF, C);
    MrSubtract(in_b+YOFF, in_b+XOFF, D);
    MrMultiply(C, D, F);

    // B = (Y1 + X1) * (Y2 + X2)
    MrAdd(in_a+YOFF, in_a+XOFF, E);
    MrAdd(in_b+YOFF, in_b+XOFF, H);
    MrMultiply(E, H, G);

    // I = (Y1 - X1) * (Y2 + X2)
    MrMultiply(C, H, A);

    // J = (Y1 + X1) * (Y2 - X2)
    MrMultiply(E, D, B);

    // C = 2 * d * T1 * T2 (can remove multiplication by d if inputs are known to be different)
    MrMultiply(in_a+TOFF, in_b+TOFF, C);
    MrMultiplyX(C, curve_d * 2, C);

    // D = 2 * Z1 * Z2
    MrMultiply(in_a+ZOFF, in_b+ZOFF, D);
    MrDouble(D, D);

    // E = B - A, F = D - C, G = D + C, H = B + A
    MrSubtract(G, F, E);
    MrAdd(G, F, H);
    MrSubtract(D, C, F);
    MrAdd(D, C, G);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, F, sum+XOFF);
    MrMultiply(G, H, sum+YOFF);
    MrMultiply(E, H, sum+TOFF);
    MrMultiply(F, G, sum+ZOFF);
    PtNegate(sum, sum + neg_offset);

    // E = J - I, F <-> G, H = J + I
    MrSubtract(B, A, E);
    MrAdd(B, A, H);

    // X3 = E * F, Y3 = G * H, T3 = E * H, Z3 = F * G
    MrMultiply(E, G, diff+XOFF);
    MrMultiply(F, H, diff+YOFF);
    MrMultiply(E, H, diff+TOFF);
    MrMultiply(G, F, diff+ZOFF);
    PtNegate(diff, diff + neg_offset);
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

// Allocate a precomputed table of odd multiples of input point
// Free the table with Aligned::Delete()
Leg *BigTwistedEdward::PtMultiplyPrecompAlloc(const Leg *in, int w)
{
    int points = (POINT_STRIDE << (w - 1));

    Leg *out = Aligned::New<Leg>(points * POINT_STRIDE);

    PtMultiplyPrecomp(in, w, out);

    return out;
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

// Extended Twisted Edwards Scalar Multiplication k*p
// CAN *NOT* BE followed by a Pt[E]Add()
void BigTwistedEdward::PtMultiply(const Leg *in_p, const Leg *in_k, u8 k_msb, Leg *out)
{
    const int w = WINDOW_BITS;

    Leg *DefaultPrecomp = Get(te_regs - TE_OVERHEAD);

#if defined(CAT_USE_W6_CONJUGATE_ADDITION)

    // More efficient than naive approach by +4S -6M -5D -20a
    // Inspired by Longa-Gebotys 2008, but it is an original algorithm

    int neg_offset = POINT_STRIDE << (w - 2);

    // Precompute P and -P
    Leg *pre_a = DefaultPrecomp;
    PtCopy(in_p, pre_a);
    PtNegate(in_p, pre_a+neg_offset);

    Leg *P1 = pre_a;

    // Precompute 2P
    Leg *pre_2 = TempPt;
    PtEDouble(in_p, pre_2);

    // Precompute 3P and -3P
    Leg *pre_b = pre_a+POINT_STRIDE;
    PtEAdd(pre_a, pre_2, pre_b);
    PtNegate(pre_b, pre_b+neg_offset);

    Leg *P3 = pre_b;

    // Precompute 5P and -5P
    pre_a = pre_b+POINT_STRIDE;
    PtEAdd(pre_b, pre_2, pre_a);
    PtNegate(pre_a, pre_a+neg_offset);

    Leg *P5 = pre_a;

    // Precompute 7P and -7P
    pre_b = pre_a+POINT_STRIDE;
    PtEAdd(pre_a, pre_2, pre_b);
    PtNegate(pre_b, pre_b+neg_offset);

    Leg *P7 = pre_b;

    // Precompute 9P and -9P
    pre_a = pre_b+POINT_STRIDE;
    PtEAdd(pre_b, pre_2, pre_a);
    PtNegate(pre_a, pre_a+neg_offset);

    Leg *P9 = pre_a;

    // Precompute 11P and -11P
    pre_b = pre_a+POINT_STRIDE;
    PtEAdd(pre_a, pre_2, pre_b);
    PtNegate(pre_b, pre_b+neg_offset);

    // Precompute 22P
    PtEDouble(pre_b, pre_2);

    pre_b += POINT_STRIDE*5;
    pre_a = pre_b + POINT_STRIDE;

    PtPrecompAddSub(pre_2, P1, pre_a, pre_b, neg_offset);
    pre_b -= POINT_STRIDE;
    pre_a += POINT_STRIDE;
    PtPrecompAddSub(pre_2, P3, pre_a, pre_b, neg_offset);
    pre_b -= POINT_STRIDE;
    pre_a += POINT_STRIDE;
    PtPrecompAddSub(pre_2, P5, pre_a, pre_b, neg_offset);
    pre_b -= POINT_STRIDE;
    pre_a += POINT_STRIDE;
    PtPrecompAddSub(pre_2, P7, pre_a, pre_b, neg_offset);
    pre_b -= POINT_STRIDE;
    pre_a += POINT_STRIDE;
    PtPrecompAddSub(pre_2, P9, pre_a, pre_b, neg_offset);

#else

    PtMultiplyPrecomp(in_p, w, DefaultPrecomp);

#endif // CAT_USE_W6_CONJUGATE_ADDITION

    PtMultiply(DefaultPrecomp, w, in_k, k_msb, out);
}

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
