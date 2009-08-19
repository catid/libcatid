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

void BigMontgomery::MonOutput(const Leg *in, Leg *out)
{
	// out = in * R^-1 (mod p)
	Copy(in, TempProduct);
	CopyX(0, TempProductHi);
	MonReduceProduct(TempProduct, out);

	// Result after reduction may still be too large by one modulus
	if (!Less(out, CachedModulus))
		Subtract(out, CachedModulus, out);
}
