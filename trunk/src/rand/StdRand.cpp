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

#include <cat/rand/StdRand.hpp>
using namespace cat;

static const s32 STDLIB_RAND_MULTIPLIER = 214013;
static const s32 STDLIB_RAND_ADDEND = 2531011;
static const s32 STDLIB_RANDU_MULTIPLIER = 65539;

u16 StandardRand::rand()
{
	seed = STDLIB_RAND_MULTIPLIER * seed + STDLIB_RAND_ADDEND;
	return (u16)((seed >> 16) & 0x7fff);
}

u16 StandardRand::randu()
{
	seed *= STDLIB_RANDU_MULTIPLIER;
	return (u16)((seed >> 16) & 0x7fff);
}

u32 cat::NLCRand32(int x, int y, u32 seed)
{
	// Map 2D->1D
	// To avoid repetitive output, adjust pitch constant (271) higher if needed
    u32 n = x + y * 271 + seed;

    // Mixing step
    n = (n << 13) ^ n;

    // Non-linear congruential mixing step
    n *= (n * n * 15731 + 789221) + 1376312589;

    return n;
}


float cat::NLCRandNorm(int x, int y, u32 seed)
{
	u32 n = NLCRand32(x, y, seed);

    // Clamp to -1..1 (mode preferred by my Perlin noise code)
    return 1.0f - n / 2147483647.5f;
}
