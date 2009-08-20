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
// 06/27/09 began

/*
    Several algorithms based on ideas from the "Handbook of Applied Cryptography"
    http://www.cacr.math.uwaterloo.ca/hac/

    Several algorithms based on ideas from the
    "Handbook of Elliptic and Hyperelliptic Curve Cryptography"
    http://www.hyperelliptic.org/HEHCC/
*/

#ifndef CAT_BIG_PSEUDO_MERSENNE_HPP
#define CAT_BIG_PSEUDO_MERSENNE_HPP

#include <cat/math/BigRTL.hpp>

namespace cat {


// Performs fast arithmetic modulo 2^bits-C, C = 1 (mod 4), C < 16384
class BigPseudoMersenne : public BigRTL
{
    static const int PM_OVERHEAD = 6; // overhead for MrSquareRoot()
    int pm_regs;

protected:
    Leg *CachedModulus;
    Leg modulus_c;

    void MrReduceProductX(Leg overflow, Leg *inout);
    void MrReduceProduct(const Leg *in_hi, const Leg *in_lo, Leg *out);

public:
    BigPseudoMersenne(int regs, int bits, int C);

public:
    const Leg *GetModulus() { return CachedModulus; }
    void CopyModulus(Leg *out);

public:
    // Result may be one modulus too large, so efficiently correct that
    void MrReduce(Leg *inout);

public:
    void MrAdd(const Leg *in_a, const Leg *in_b, Leg *out);
    void MrAddX(Leg *inout, Leg x);
    void MrSubtract(const Leg *in_a, const Leg *in_b, Leg *out);
    void MrSubtractX(Leg *inout, Leg x);
    void MrNegate(const Leg *in, Leg *out);

public:
    void MrDouble(const Leg *in, Leg *out);

public:
    void MrMultiply(const Leg *in_a, const Leg *in_b, Leg *out);
    void MrMultiplyX(const Leg *in_a, Leg in_b, Leg *out);
    void MrSquare(const Leg *in, Leg *out);

public:
    void MrInvert(const Leg *in, Leg *out);

public:
    void MrSquareRoot(const Leg *in, Leg *out);
};


} // namespace cat

#endif // CAT_BIG_PSEUDO_MERSENNE_HPP
