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

#ifndef CAT_I_RANDOM_HPP
#define CAT_I_RANDOM_HPP

#include <cat/Platform.hpp>

namespace cat {


// Pseudo-random number generators will derive from IRandom and implement its public methods
// WARNING: Not seeded by default.  Be sure to call Initialize() before Generate()
class IRandom
{
public:
	virtual ~IRandom() {}

	// Generate a 32-bit random number
	virtual u32 Generate() = 0;

	// Generate a variable number of random bytes
	virtual void Generate(void *buffer, int bytes) = 0;

public:
	// Generate a 32-bit random number in the range [low..high] inclusive
	u32 GenerateUnbiased(u32 low, u32 high)
	{
		u32 range = high - low;

		// Round range up to the next pow(2)-1 using a Stanford Bit Twiddling Hack
		u32 v = range - 1;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;

		// Generate an unbiased random number in the inclusive range [0..(high-low)]
		u32 x;
		do x = Generate() & v;
		while (x > range);

		return low + x;
	}
};


} // namespace cat

#endif // CAT_I_RANDOM_HPP
