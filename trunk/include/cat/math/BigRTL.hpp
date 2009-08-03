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

// 07/12/09 working and fast!
// 06/29/09 began

/*
    Several algorithms based on ideas from the "Handbook of Applied Cryptography"
    http://www.cacr.math.uwaterloo.ca/hac/

    Several algorithms based on ideas from the
    "Handbook of Elliptic and Hyperelliptic Curve Cryptography"
    http://www.hyperelliptic.org/HEHCC/
*/

#ifndef CAT_BIG_RTL_HPP
#define CAT_BIG_RTL_HPP

#include <cat/math/Legs.hpp>

namespace cat {


    // Implements a register transfer language (RTL) for big integer arithmetic
    class BigRTL
    {
        static const int BIG_OVERHEAD = 7; // overhead for ModularInverse()
        int library_regs;

    protected:
        int library_legs;
        Leg *library_memory;

    protected:
        static Leg ShiftRight(int legs, const Leg *in, int shift, Leg *out);
        static Leg ShiftLeft(int legs, const Leg *in, int shift, Leg *out);

    protected:
        static u8 Add(int legs, const Leg *in_a, const Leg *in_b, Leg *out);
        static u8 Add(int legs_a, const Leg *in_a, int legs_b, const Leg *in_b, Leg *out); // legs_b <= legs_a
        static u8 Subtract(int legs, const Leg *in_a, const Leg *in_b, Leg *out);

    protected:
        static Leg MultiplyX(int legs, const Leg *in_a, Leg in_b, Leg *out);
        static Leg MultiplyXAdd(int legs, const Leg *in_a, Leg in_b, const Leg *in_c, Leg *out);
        static Leg DoubleAdd(int legs, const Leg *in_a, const Leg *in_b, Leg *out);

    protected:
        static void DivideCore(int A_used, Leg A_overflow, Leg *A, int B_used, Leg *B, Leg *Q); // A = remainder

    public:
        BigRTL(int regs, int bits);
        ~BigRTL();

    public:
        Leg *Get(int reg_index);
        CAT_INLINE int Legs() { return library_legs; }
        CAT_INLINE int RegBytes() { return library_legs * sizeof(Leg); }

    public:
        void Load(const void *in, int bytes, Leg *out);
        void Save(const Leg *in, void *out, int bytes);

        bool LoadFromString(const char *in, int base, Leg *out);

    public:
        void Copy(const Leg *in, Leg *out);
        void CopyX(Leg in, Leg *out);

    public:
        int LegsUsed(const Leg *in);

    public:
        bool Greater(const Leg *in_a, const Leg *in_b);
        bool Less(const Leg *in_a, const Leg *in_b);
        bool Equal(const Leg *in_a, const Leg *in_b);
        bool EqualX(const Leg *in, Leg x);
        bool IsZero(const Leg *in);

    public:
        Leg ShiftLeft(const Leg *in, int shift, Leg *out);

    public:
        u8 Add(const Leg *in_a, const Leg *in_b, Leg *out);
        u8 AddX(Leg *inout, Leg x);
        u8 Subtract(const Leg *in_a, const Leg *in_b, Leg *out);
        u8 SubtractX(Leg *inout, Leg x);
        void Negate(const Leg *in, Leg *out);

    public:
        u8 Double(const Leg *in, Leg *out);

    public:
        Leg MultiplyX(const Leg *in_a, Leg in_b, Leg *out); // out = a[] * b
        Leg MultiplyXAdd(const Leg *in_a, Leg in_b, const Leg *in_c, Leg *out); // out = a[] * b + c[]
        Leg DoubleAdd(const Leg *in_a, const Leg *in_b, Leg *out); // out = a[] * 2 + b[]

    public:
        void MultiplyLow(const Leg *in_a, const Leg *in_b, Leg *out); // out = a[] * b[], low half

    public:
        // out[] gets the low part of the product, next reg gets high part
        void Multiply(const Leg *in_a, const Leg *in_b, Leg *out); // out+1:out = a[] * b[]
        void Square(const Leg *in, Leg *out); // out+1:out = in[] * in[]

    public:
        Leg DivideX(const Leg *in_a, Leg in_b, Leg *out); // out = a[] / b, returns modulus
        Leg ModulusX(const Leg *in_a, Leg in_b); // returns a[] % b

    public:
        bool Divide(const Leg *in_a, const Leg *in_b, Leg *out_q, Leg *out_r);

    public:
        void ModularInverse(const Leg *x, const Leg *modulus, Leg *inverse);
    };


} // namespace cat

#endif // CAT_BIG_RTL_HPP
