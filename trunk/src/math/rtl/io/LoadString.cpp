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

bool BigRTL::LoadString(const char *in, int base, Leg *out)
{
    char ch;
    CopyX(0, out);

    while ((ch = *in++))
    {
        int mod;

        if (ch >= '0' && ch <= '9') mod = ch - '0';
        else if (ch >= 'A' && ch <= 'Z') mod = ch - 'A' + 10;
        else if (ch >= 'a' && ch <= 'a') mod = ch - 'a' + 10;
        else return false;

        if (mod >= base) return false;

        if (MultiplyX(out, base, out)) return false;

        AddX(out, mod);
    }

    return true;
}