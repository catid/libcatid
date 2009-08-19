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

#include <cat/math/BigMontgomery.hpp>
using namespace cat;

// Base must be in the Montgomery RNS.  in_base != out
void BigMontgomery::MonExpMod(const Leg *in_base, const Leg *in_exp, Leg *out)
{
	bool seen_high_bit = false;

	// Left-to-right square and multiply method
	for (int ii = library_legs - 1; ii >= 0; --ii)
	{
		Leg e_i = in_exp[ii];

		for (Leg mask = CAT_LEG_MSB; mask; mask >>= 1)
		{
			if (seen_high_bit)
			{
				// out = out*out (mod p)
				MonSquare(out, out);

				if (e_i & mask)
				{
					// out *= base (mod p)
					MonMultiply(out, in_base, out);
				}
			}
			else
			{
				if (e_i & mask)
				{
					// out = base
					Copy(in_base, out);
					seen_high_bit = true;
				}
			}
		}
	}

	// 0^e = 0
	if (!seen_high_bit)
		CopyX(0, out);
}
