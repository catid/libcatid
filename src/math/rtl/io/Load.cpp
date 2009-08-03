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
#include <cat/port/EndianNeutral.hpp>
using namespace cat;

void BigRTL::Load(const void *in, int bytes, Leg *out_leg)
{
    // Prepare to copy
    Leg *in_leg = (Leg*)in;
    int ii, legs = bytes / sizeof(Leg);
    if (legs > library_legs) legs = library_legs;

    // Copy 4 legs at a time
    for (ii = 4; ii <= legs; ii += 4)
    {
        out_leg[ii - 4] = getLE(in_leg[ii - 4]);
        out_leg[ii - 3] = getLE(in_leg[ii - 3]);
        out_leg[ii - 2] = getLE(in_leg[ii - 2]);
        out_leg[ii - 1] = getLE(in_leg[ii - 1]);
    }

    // Copy remaining legs
    switch (legs % 4)
    {
    case 3: out_leg[legs - 3] = getLE(in_leg[legs - 3]);
    case 2: out_leg[legs - 2] = getLE(in_leg[legs - 2]);
    case 1: out_leg[legs - 1] = getLE(in_leg[legs - 1]);
    }

    // Zero remaining buffer bytes
    memset(&out_leg[legs], 0, (library_legs - legs) * sizeof(Leg));
}
