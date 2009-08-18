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
using namespace cat;

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

// Divide the product of two registers (a+1:a) by single register (b)
// Resulting quotient is two registers (q+1:q) and remainder is one register (r)
bool BigRTL::DivideProduct(const Leg *in_a, const Leg *in_b, Leg *out_q, Leg *out_r)
{
	int B_used = LegsUsed(in_b);
    if (!B_used) return false;

	const Leg *in_a_hi = in_a + library_legs;
	Leg *out_q_hi = out_q + library_legs;

    int A_used = LegsUsed(in_a_hi);
	if (A_used) A_used += library_legs;
	else
	{
		// If a < b, avoid division
		if (Less(in_a, in_b))
		{
			Copy(in_a, out_r);
			CopyX(0, out_q);
			CopyX(0, out_q_hi);
			return true;
		}

		A_used = LegsUsed(in_a);
	}

	// TODO: implement from here on!!

    // {q, r} = a / b
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
