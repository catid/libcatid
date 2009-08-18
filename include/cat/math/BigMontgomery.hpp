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

/*
    Several algorithms based on ideas from the "Handbook of Applied Cryptography"
    http://www.cacr.math.uwaterloo.ca/hac/
*/

#ifndef CAT_BIG_MONTGOMERY_HPP
#define CAT_BIG_MONTGOMERY_HPP

#include <cat/math/BigRTL.hpp>

namespace cat {


// Performs fast modular arithmetic in the Montgomery Residue Number System
class BigMontgomery : public BigRTL
{
    static const int MON_OVERHEAD = 3;
    int mon_regs;

protected:
	Leg *TempProduct;
	Leg *TempProductHi;
    Leg *CachedModulus;
	Leg mod_inv;

public:
    BigMontgomery(int regs, int bits);

	// Must call SetModulus() before using this object
	void SetModulus(const Leg *mod);

public:
    const Leg *GetModulus() { return CachedModulus; }
    void CopyModulus(Leg *out);

public:
	// Put the contents of a register into the RNS, stored in out
	void MonInput(const Leg *in, Leg *out);

	// Note: This will clobber the input product!
	// Reduce a double-register product to a single register in the RNS
	void MonReduceProduct(Leg *inout_product, Leg *out);

public:
	// Inputs and outputs must be in the Montgomery RNS
    void MonAdd(const Leg *in_a, const Leg *in_b, Leg *out);
    void MonAddX(Leg *inout, Leg x);
    void MonSubtract(const Leg *in_a, const Leg *in_b, Leg *out);
    void MonSubtractX(Leg *inout, Leg x);
    void MonNegate(const Leg *in, Leg *out);
    void MonDouble(const Leg *in, Leg *out);

public:
	// Inputs and outputs must be in the Montgomery RNS
    void MonMultiply(const Leg *in_a, const Leg *in_b, Leg *out);
    void MonSquare(const Leg *in, Leg *out);
};


} // namespace cat

#endif // CAT_BIG_MONTGOMERY_HPP
