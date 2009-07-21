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
#include <cat/port/EndianNeutral.hpp>
#include <cat/port/AlignedAlloc.hpp>
using namespace cat;

BigRTL::BigRTL(int regs, int bits)
{
    library_legs = bits / (8 * sizeof(Leg));
    library_regs = regs + BIG_OVERHEAD;

    // Align library memory accesses to a 16-byte boundary
    library_memory = Aligned::New<Leg>(library_legs * library_regs);
}

BigRTL::~BigRTL()
{
    if (library_memory)
    {
        // Clear and free memory for registers
        memset(library_memory, 0, library_legs * library_regs * sizeof(Leg));
        Aligned::Delete(library_memory);
    }
}

Leg *BigRTL::Get(int reg_index)
{
    return &library_memory[library_legs * reg_index];
}

void BigRTL::Load(const void *in, int bytes, Leg *out_leg)
{
    // Prepare to copy
    Leg *in_leg = (Leg*)in;
    int ii, legs = bytes / sizeof(Leg);
    if (legs > library_legs) legs = library_legs;

    // Copy 4 legs at a time
    for (ii = 4; ii <= legs; ii += 4)
    {
        out_leg[ii - 4] = getLE(in_leg[ii - 4]);
        out_leg[ii - 3] = getLE(in_leg[ii - 3]);
        out_leg[ii - 2] = getLE(in_leg[ii - 2]);
        out_leg[ii - 1] = getLE(in_leg[ii - 1]);
    }

    // Copy remaining legs
    switch (legs % 4)
    {
    case 3: out_leg[ii - 1] = getLE(in_leg[ii - 1]);
    case 2: out_leg[ii - 2] = getLE(in_leg[ii - 2]);
    case 1: out_leg[ii - 3] = getLE(in_leg[ii - 3]);
    }

    // Zero remaining buffer bytes
    memset(&out_leg[legs], 0, (library_legs - legs) * sizeof(Leg));
}

void BigRTL::Save(const Leg *in_leg, void *out, int bytes)
{
    // Prepare to copy
    Leg *out_leg = (Leg*)out;
    int ii, legs = bytes / sizeof(Leg);
    if (legs > library_legs) legs = library_legs;

    // Copy 4 legs at a time
    for (ii = 4; ii <= legs; ii += 4)
    {
        out_leg[ii - 4] = getLE(in_leg[ii - 4]);
        out_leg[ii - 3] = getLE(in_leg[ii - 3]);
        out_leg[ii - 2] = getLE(in_leg[ii - 2]);
        out_leg[ii - 1] = getLE(in_leg[ii - 1]);
    }

    // Copy remaining legs
    switch (legs % 4)
    {
    case 3: out_leg[ii - 1] = getLE(in_leg[ii - 1]);
    case 2: out_leg[ii - 2] = getLE(in_leg[ii - 2]);
    case 1: out_leg[ii - 3] = getLE(in_leg[ii - 3]);
    }

    // Zero remaining buffer bytes
    memset(&out_leg[legs], 0, bytes - legs * sizeof(Leg));
}

bool BigRTL::LoadString(const char *in, int base, Leg *out)
{
    char ch;
    CopyX(0, out);

    while ((ch = *in++))
    {
        int mod;

        if (ch >= '0' && ch <= '9') mod = ch - '0';
        else if (ch >= 'A' && ch <= 'Z') mod = ch - 'A' + 10;
        else if (ch >= 'a' && ch <= 'a') mod = ch - 'a' + 10;
        else return false;

        if (mod >= base) return false;

        if (MultiplyX(out, base, out)) return false;

        AddX(out, mod);
    }

    return true;
}

void BigRTL::Copy(const Leg *in, Leg *out)
{
    memcpy(out, in, library_legs * sizeof(Leg));
}

void BigRTL::CopyX(Leg in, Leg *out)
{
    // Set low leg to input, zero the rest
    out[0] = in;
    memset(&out[1], 0, (library_legs-1) * sizeof(Leg));
}

int BigRTL::LegsUsed(const Leg *in)
{
    for (int legs = library_legs - 1; legs >= 0; --legs)
        if (in[legs]) return legs + 1;

    return 0;
}

bool BigRTL::Greater(const Leg *in_a, const Leg *in_b)
{
    int legs = library_legs;

    while (legs-- > 0)
    {
        Leg a = in_a[legs];
        Leg b = in_b[legs];
        if (a < b) return false;
        if (a > b) return true;
    }

    return false;
}

bool BigRTL::Less(const Leg *in_a, const Leg *in_b)
{
    int legs = library_legs;

    while (legs-- > 0)
    {
        Leg a = in_a[legs];
        Leg b = in_b[legs];
        if (a > b) return false;
        if (a < b) return true;
    }

    return false;
}

bool BigRTL::Equal(const Leg *in_a, const Leg *in_b)
{
    return 0 == memcmp(in_a, in_b, library_legs * sizeof(Leg));
}

bool BigRTL::EqualX(const Leg *in, Leg x)
{
    if (in[0] != x) return false;

    for (int ii = 1; ii < library_legs; ++ii)
        if (in[ii]) return false;

    return true;
}

bool BigRTL::IsZero(const Leg *in)
{
    for (int ii = 0; ii < library_legs; ++ii)
        if (in[ii]) return false;

    return true;
}

Leg BigRTL::ShiftLeft(const Leg *in, int shift, Leg *out)
{
    return ShiftLeft(library_legs, in, shift, out);
}

Leg BigRTL::ShiftLeft(int legs, const Leg *in, int shift, Leg *out)
{
    if (!shift)
    {
        memcpy(out, in, legs * sizeof(Leg));
        return 0;
    }

    Leg carry = in[0];

    out[0] = carry << shift;

    for (int ii = 1; ii < legs; ++ii)
    {
        Leg x = in[ii];
        out[ii] = (x << shift) | (carry >> (CAT_LEG_BITS - shift));
        carry = x;
    }

    return carry >> (CAT_LEG_BITS - shift);
}

Leg BigRTL::ShiftRight(int legs, const Leg *in, int shift, Leg *out)
{
    if (!shift)
    {
        memcpy(out, in, legs * sizeof(Leg));
        return 0;
    }

    Leg carry = in[legs-1];

    out[legs-1] = carry >> shift;

    for (int ii = legs-2; ii >= 0; --ii)
    {
        Leg x = in[ii];
        out[ii] = (x >> shift) | (carry << (CAT_LEG_BITS - shift));
        carry = x;
    }

    return carry << (CAT_LEG_BITS - shift);
}

u8 BigRTL::Add(const Leg *in_a, const Leg *in_b, Leg *out)
{
    return Add(library_legs, in_a, in_b, out);
}

u8 BigRTL::Add(int legs, const Leg *in_a, const Leg *in_b, Leg *out)
{
#if !defined(CAT_NO_LEGPAIR)

    // Add first two legs without a carry-in
    LegPair sum = (LegPair)in_a[0] + in_b[0];
    out[0] = (Leg)sum;

    // Add remaining legs
    for (int ii = 1; ii < legs; ++ii)
    {
        sum = ((sum >> CAT_LEG_BITS) + in_a[ii]) + in_b[ii];
        out[ii] = (Leg)sum;
    }

    return (u8)(sum >> CAT_LEG_BITS);

#else

    // Add first two legs without a carry-in
    Leg t = in_a[0];
    Leg s = t + in_b[0];
    u8 c = s < t;

    out[0] = s;

    // Add remaining legs
    for (int ii = 1; ii < legs; ++ii)
    {
        // Calculate sum
        Leg a = in_a[ii];
        Leg b = in_b[ii];
        Leg sum = a + b + c;

        // Calculate carry
        c = c ? sum <= a : sum < a;

        out[ii] = sum;
    }

    return c;

#endif
}

u8 BigRTL::Add(int legs_a, const Leg *in_a, int legs_b, const Leg *in_b, Leg *out)
{
#if !defined(CAT_NO_LEGPAIR)

    // Add first two legs without a carry-in
    LegPair sum = (LegPair)in_a[0] + in_b[0];
    out[0] = (Leg)sum;

    // Add remaining legs
    int ii;
    for (ii = 1; ii < legs_b; ++ii)
    {
        sum = ((sum >> CAT_LEG_BITS) + in_a[ii]) + in_b[ii];
        out[ii] = (Leg)sum;
    }

    for (; ii < legs_a; ++ii)
    {
        sum = (sum >> CAT_LEG_BITS) + in_a[ii];
        out[ii] = (Leg)sum;
    }

    return (u8)(sum >> CAT_LEG_BITS);

#else

    // Add first two legs without a carry-in
    Leg t = in_a[0];
    Leg s = t + in_b[0];
    u8 c = s < t;

    out[0] = s;

    // Add remaining legs
    int ii;
    for (ii = 1; ii < legs_b; ++ii)
    {
        // Calculate sum
        Leg a = in_a[ii];
        Leg b = in_b[ii];
        Leg sum = a + b + c;

        // Calculate carry
        c = c ? sum <= a : sum < a;

        out[ii] = sum;
    }

    for (; ii < legs_a; ++ii)
    {
        // Calculate sum
        Leg a = in_a[ii];
        Leg sum = a + c;

        // Calculate carry
        c = c ? sum <= a : sum < a;

        out[ii] = sum;
    }

    return c;

#endif
}

u8 BigRTL::AddX(Leg *inout, Leg x)
{
    // If the initial sum did not carry out, return 0
    if ((inout[0] += x) >= x)
        return 0;

    // Ripple the carry out as far as needed
    for (int ii = 1; ii < library_legs; ++ii)
        if (++inout[ii]) return 0;

    return 1;
}

u8 BigRTL::Subtract(const Leg *in_a, const Leg *in_b, Leg *out)
{
    return Subtract(library_legs, in_a, in_b, out);
}

u8 BigRTL::Subtract(int legs, const Leg *in_a, const Leg *in_b, Leg *out)
{
#if !defined(CAT_NO_LEGPAIR)

    // Subtract first two legs without a borrow-in
    LegPairSigned diff = (LegPairSigned)in_a[0] - in_b[0];
    out[0] = (Leg)diff;

    // Subtract remaining legs
    for (int ii = 1; ii < legs; ++ii)
    {
        diff = ((diff >> CAT_LEG_BITS) + in_a[ii]) - in_b[ii];
        out[ii] = (Leg)diff;
    }

    return (u8)(diff >> CAT_LEG_BITS) & 1;

#else

    // Subtract first two legs without a borrow-in
    Leg t = in_a[0];
    Leg s = in_b[0];
    Leg w = t - s;
    u8 c = t < s;

    out[0] = w;

    // Subtract remaining legs
    for (int ii = 1; ii < legs; ++ii)
    {
        // Calculate difference
        Leg a = in_a[ii];
        Leg b = in_b[ii];
        Leg d = a - b - c;

        // Calculate borrow-out
        c = c ? (a < d || b == ~(Leg)0) : (a < b);

        out[ii] = d;
    }

    return c;

#endif
}

u8 BigRTL::SubtractX(Leg *inout, Leg x)
{
    Leg t = inout[0];
    inout[0] = t - x;

    // If the initial difference did not borrow in, return 0
    if (t >= x) return 0;

    // Ripple the borrow in as far as needed
    for (int ii = 1; ii < library_legs; ++ii)
        if (inout[ii]--) return 0;

    return 1;
}

void BigRTL::Negate(const Leg *in, Leg *out)
{
    int ii;

    // Ripple the borrow in as far as needed
    for (ii = 0; ii < library_legs; ++ii)
        if ((out[ii] = ~in[ii] + 1))
            break;

    // Invert remaining bits
    for (; ii < library_legs; ++ii)
        out[ii] = ~in[ii];
}

u8 BigRTL::Double(const Leg *in, Leg *out)
{
    // Double low leg first
    Leg last = in[0];
    out[0] = last << 1;

    // Shift up the rest by 1 bit; actually pretty fast this way!
    for (int ii = 1; ii < library_legs; ++ii)
    {
        Leg next = in[ii];
        out[ii] = (next << 1) | (last >> (CAT_LEG_BITS-1));
        last = next;
    }

    return (u8)(last >> (CAT_LEG_BITS-1));
}

Leg BigRTL::MultiplyX(const Leg *in_a, Leg in_b, Leg *out)
{
    return MultiplyX(library_legs, in_a, in_b, out);
}

Leg BigRTL::MultiplyXAdd(const Leg *in_a, Leg in_b, const Leg *in_c, Leg *out)
{
    return MultiplyXAdd(library_legs, in_a, in_b, in_c, out);
}

Leg BigRTL::DoubleAdd(const Leg *in_a, const Leg *in_b, Leg *out)
{
    return DoubleAdd(library_legs, in_a, in_b, out);
}

Leg BigRTL::MultiplyX(int legs, const Leg *in_a, Leg in_b, Leg *output)
{
    // ICC does a better job than my hand-written version by using SIMD instructions,
    // so I use its optimizer instead.
#if !defined(CAT_COMPILER_ICC) && defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK // VS.NET, x86, 32-bit words
    {
        mov esi, [in_a]        ; esi = in_a
        mov ecx, [output]    ; ecx = output
        mov edi, [in_b]        ; edi = in_b

        ; edx:eax = A[0] * B
        mov eax, [esi]
        mul edi

        mov [ecx], eax        ; output[0] = eax
        sub [legs], 1
        jbe loop_done

loop_head:
            lea esi, [esi + 4]
            mov ebx, edx
            mov eax, [esi]
            mul edi
            lea ecx, [ecx + 4]
            add eax, ebx
            adc edx, 0
            mov [ecx], eax

        sub [legs], 1
        ja loop_head

loop_done:
        mov eax, edx
    }

#else

    Leg p_hi;

    CAT_LEG_MUL(in_a[0], in_b, p_hi, output[0]);

    for (int ii = 1; ii < legs; ++ii)
        CAT_LEG_MULADD(in_a[ii], in_b, p_hi, p_hi, output[ii]);

    return p_hi;

#endif
}

// out = A * B + C
Leg BigRTL::MultiplyXAdd(int legs, const Leg *in_a, Leg in_b, const Leg *in_c, Leg *output)
{
    // ICC does a better job than my hand-written version by using SIMD instructions,
    // so I use its optimizer instead.
#if !defined(CAT_COMPILER_ICC) && defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    CAT_ASSEMBLY_BLOCK // VS.NET, x86, 32-bit words
    {
        mov esi, [in_a]        ; esi = in_a
        mov edi, [in_c]        ; edi = in_c
        mov ecx, [output]    ; ecx = output

        ; edx:eax = A[0] * B + C
        mov eax, [esi]
        mul [in_b]
        add eax, [edi]
        adc edx, 0

        mov [ecx], eax        ; output[0] = eax
        sub [legs], 1
        jbe loop_done

loop_head:
            lea esi, [esi + 4]
            mov ebx, edx
            mov eax, [esi]
            lea edi, [edi + 4]
            mul [in_b]
            add eax, [edi]
            adc edx, 0
            lea ecx, [ecx + 4]
            add eax, ebx
            adc edx, 0
            mov [ecx], eax

        sub [legs], 1
        ja loop_head

loop_done:
        mov eax, edx
    }

#else

    Leg p_hi;

    CAT_LEG_MULADD(in_a[0], in_b, in_c[0], p_hi, output[0]);

    for (int ii = 1; ii < legs; ++ii)
        CAT_LEG_MULADD2(in_a[ii], in_b, in_c[ii], p_hi, p_hi, output[ii]);

    return p_hi;

#endif
}

// out = in_a[] * 2 + in_b[]
Leg BigRTL::DoubleAdd(int legs, const Leg *in_a, const Leg *in_b, Leg *out)
{
#if !defined(CAT_NO_LEGPAIR)

    LegPair x = ((LegPair)in_a[0] << 1) + in_b[0];
    out[0] = (Leg)x;

    for (int ii = 1; ii < legs; ++ii)
    {
        x = (x >> CAT_LEG_BITS) + ((LegPair)in_a[ii] << 1) + in_b[ii];
        out[ii] = (Leg)x;
    }

    return x >> CAT_LEG_BITS;

#else

    Leg p_hi;

    CAT_LEG_MULADD(in_a[0], 2, in_b[0], p_hi, out[0]);

    for (int ii = 1; ii < legs; ++ii)
        CAT_LEG_MULADD2(in_a[ii], 2, in_b[ii], p_hi, p_hi, out[ii]);

    return p_hi;

#endif
}

void BigRTL::Multiply(const Leg *in_a, const Leg *in_b, Leg *out)
{
    out[library_legs] = MultiplyX(in_a, in_b[0], out);

    for (int ii = 1; ii < library_legs; ++ii)
        out[library_legs + ii] = MultiplyXAdd(in_a, in_b[ii], out + ii, out + ii);
}

void BigRTL::Square(const Leg *in, Leg *output)
{
    Leg *cross = Get(library_regs - 2);

    // ICC does a better job than my hand-written version by using SIMD instructions,
    // so I use its optimizer instead.
#if !defined(CAT_COMPILER_ICC) && defined(CAT_ASSEMBLY_INTEL_SYNTAX)

    int legs = library_legs;

    CAT_ASSEMBLY_BLOCK // VS.NET, x86, 32-bit words
    {
        mov esi, [in]        ; esi = in
        mov ecx, [output]    ; ecx = output
        mov edi, [legs]        ; edi = leg count

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
        CAT_LEG_MUL(in[ii], in[ii], output[ii*2+1], output[ii*2]);

#endif

    // Calculate cross products
    cross[library_legs] = MultiplyX(library_legs-1, in+1, in[0], cross+1);
    for (int ii = 1; ii < library_legs-1; ++ii)
        cross[library_legs + ii] = MultiplyXAdd(library_legs-1-ii, in+1+ii, in[ii], cross+1+ii*2, cross+1+ii*2);

    // Multiply the cross product by 2 and add it to the square products
    output[library_legs*2-1] += DoubleAdd(library_legs*2-2, cross+1, output+1, output+1);
}

void BigRTL::MultiplyLow(const Leg *in_a, const Leg *in_b, Leg *out)
{
    MultiplyX(in_a, in_b[0], out);

    for (int ii = 1; ii < library_legs; ++ii)
        MultiplyXAdd(library_legs - ii, in_a, in_b[ii], out + ii, out + ii);
}

bool BigRTL::Divide(const Leg *in_a, const Leg *in_b, Leg *out_q, Leg *out_r)
{
    // If a < b, avoid division
    if (Less(in_a, in_b))
    {
        Copy(in_a, out_r);
        CopyX(0, out_q);
        return true;
    }

    // {q, r} = a / b

    int B_used = LegsUsed(in_b);
    if (!B_used) return false;
    int A_used = LegsUsed(in_a);

    // If b is just one leg, use faster DivideX code
    if (B_used == 1)
    {
        Leg R = DivideX(in_a, in_b[0], out_q);
        CopyX(R, out_r);
        return true;
    }

    Leg *A = Get(library_regs - 1); // shifted numerator
    Leg *B = Get(library_regs - 2); // shifted denominator

    // Determine shift required to set high bit of highest leg in b
    int shift = CAT_LEG_BITS - CAT_USED_BITS(in_b[B_used-1]) - 1;

    // Shift a and b by these bits, probably making A one leg larger
    Leg A_overflow = ShiftLeft(A_used, in_a, shift, A);
    ShiftLeft(B_used, in_b, shift, B);

    DivideCore(A_used, A_overflow, A, B_used, B, out_q);

    // Zero the unused legs of the quotient
    int offset = A_used - B_used + 1;
    memset(out_q + offset, 0, (library_legs - offset) * sizeof(Leg));

    // Fix remainder shift and zero its unused legs
    memset(out_r + B_used, 0, (library_legs - B_used) * sizeof(Leg));
    ShiftRight(B_used, A, shift, out_r);

    return true;
}

void BigRTL::ModularInverse(const Leg *x, const Leg *modulus, Leg *inverse)
{
    if (EqualX(x, 1))
    {
        CopyX(1, inverse);
        return;
    }

    Leg *t1 = inverse;
    Leg *t0 = Get(library_regs - 3);
    Leg *b = Get(library_regs - 4);
    Leg *c = Get(library_regs - 5);
    Leg *q = Get(library_regs - 6);
    Leg *p = Get(library_regs - 7);

    Copy(x, b);
    Divide(modulus, b, t0, c);
    CopyX(1, t1);

    while (!EqualX(c, 1))
    {
        Divide(b, c, q, b);
        MultiplyLow(q, t0, p);
        Add(t1, p, t1);

        if (EqualX(b, 1))
            return;

        Divide(c, b, q, c);
        MultiplyLow(q, t1, p);
        Add(t0, p, t0);
    }

    Subtract(modulus, t0, inverse);
}
