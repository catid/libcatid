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
