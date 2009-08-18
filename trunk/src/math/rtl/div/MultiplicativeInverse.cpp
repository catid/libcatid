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
using namespace cat;

Leg BigRTL::MultiplicativeInverseX(Leg n)
{
	// {u1, g1} = 2^bits / n
	Leg hb = (~(n - 1) >> (CAT_LEG_BITS-1));
	Leg u1 = -(LegSigned)(CAT_LEG_LARGEST / n + hb);
	Leg g1 = ((-(LegSigned)hb) & (CAT_LEG_LARGEST % n + 1)) - n;

	if (!g1) return n != 1 ? 0 : 1;

	Leg q, u = 1, g = n;

	for (;;)
	{
		q = g / g1;
		g %= g1;

		if (!g) return g1 != 1 ? 0 : u1;

		u -= q*u1;
		q = g1 / g;
		g1 %= g;

		if (!g1) return g != 1 ? 0 : u;

		u1 -= q*u;
	}
}
