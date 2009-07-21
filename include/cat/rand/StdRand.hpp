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

// 06/15/09 split from MersenneTwister.hpp

#ifndef CAT_STDRAND_HPP
#define CAT_STDRAND_HPP

#include <cat/Platform.hpp>

namespace cat {


// Microsoft VC++ 7.0 stdlib srand(), rand(), old RANDU()
class StandardRand
{
protected:
    s32 seed;

public:
    StandardRand(u32 ns = 0) { seed = ns; }

    inline void srand32(u32 ns) { seed = ns; } // 32-bit version
    inline void srand16(u16 ns) { seed = ns; } // 16-bit version (yup)

    u16 rand();    // Linear Congruential Generator: X = X * M + A (mod N)
    u16 randu(); // RANDU LCG: X = X * M (mod N)
};


// Non-linear congruential 32-bit random mixing function for given x, y and seed
// This function is used in every example of Perlin noise I found online for some reason!
u32 NLCRand32(int x, int y, u32 seed);

// Uses NLCRand32 as a front-end, and then maps the output to a number between -1 and 1
float NLCRandNorm(int x, int y, u32 seed);



} // namespace cat

#endif // CAT_STDRAND_HPP
