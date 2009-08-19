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

void BigMontgomery::MonAdd(const Leg *in_a, const Leg *in_b, Leg *out)
{
    // If the addition overflowed, subtract modulus
    if (Add(in_a, in_b, out))
        while (Subtract(out, CachedModulus, out));
}

void BigMontgomery::MonDouble(const Leg *in, Leg *out)
{
    // If the doubling overflowed, subtract modulus
    if (Double(in, out))
        while (Subtract(out, CachedModulus, out));
}
