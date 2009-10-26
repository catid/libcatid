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

#include <cat/gfx/Scalar.hpp>
using namespace cat;

#if defined(CAT_ISA_X86)

/*
	Fast inverse square root, improving on the accuracy of the
	Quake III algorithm, with error in the range -0.00065 ... 0.00065
	from http://pizer.wordpress.com/2008/10/12/fast-inverse-square-root/
*/
f32 InvSqrt(f32 x)
{
    f32 x1 = 0.714158168f * x;

	// Generate a close approximation to the square root:
	u32 i = 0x5F1F1412 - (*(u32*)&x >> 1);
	f32 approx = *(f32*)&i;

	// One iteration of Newton's method converging towards the square root:
	return approx * (1.69000231f - x1 * approx * approx);
}

#else

f32 InvSqrt(f32 x)
{
	return (f32)(1.0 / sqrt(x));
}

#endif
