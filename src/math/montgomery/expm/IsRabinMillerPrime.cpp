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

bool BigMontgomery::IsRabinMillerPrime(IRandom *prng, const Leg *n, int trials)
{
	Leg *d = Get(mon_regs - 4);
	Leg *a = Get(mon_regs - 5);
	Leg *x = Get(mon_regs - 6);
	Leg *n_1 = Get(mon_regs - 7);

	// Returns false for n = 0 and n = 1
	if (!GreaterX(n, 1))
		return false;

	// Use n as the modulus for Montgomery RNS
	SetModulus(n);

	// n_1 = n-1
	Copy(n, n_1);
	SubtractX(n_1, 1);

	// d = n_1 = n-1
	Copy(n_1, d);

	// s = trailing zeroes eatten from d
	int s = EatTrailingZeroes(d);

	// For each trial,
	while (trials--)
	{
		// Generate a random number [2, n-1]
		do prng->Generate(a, library_legs * sizeof(Leg));
		while (!Less(a, n) || !GreaterX(a, 2));

		// Bring a into the RNS
		MonInput(a, x);

		// a = a^d (mod p) in RNS
		MonExpMod(x, d, a);

		// x = a out of RNS
		MonOutput(a, x);

		// Passes this trial if x = 1 or n-1
		if (EqualX(x, 1)) continue;
		if (!AddX(x, 1) && Equal(x, n)) continue;

		// For s-1 times,
		for (int r = 1; r < s; ++r)
		{
			// a = a*a (mod p) in RNS
			MonSquare(a, a);

			// x = a out of RNS
			MonOutput(a, x);

			// If x = 1, it is composite
			if (EqualX(x, 1)) return false;

			// If x = n-1, it could still be prime
			if (Equal(x, n_1)) goto SKIP_TO_NEXT_TRIAL;
		}

		// If we didn't find any x = n-1, it is composite
		return false;

SKIP_TO_NEXT_TRIAL:; // yea i just went there ..|.. sue me
	}

	// It is probably prime
	return true;
}
