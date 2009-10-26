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

#ifndef CAT_SCALAR_HPP
#define CAT_SCALAR_HPP

#include <cat/Platform.hpp>
#include <cmath>

namespace cat {


// PI
#define CAT_TWO_PI_64 6.283185307179586476925286766559
#define CAT_TWO_PI_32 6.28318531f
#define CAT_PI_64 3.1415926535897932384626433832795
#define CAT_PI_32 3.14159265f
#define CAT_HALF_PI_64 1.5707963267948966192313216916398
#define CAT_HALF_PI_32 1.5707963268f
#define CAT_QUARTER_PI_64 0.78539816339744830961566084581988
#define CAT_QUARTER_PI_32 0.7853981634f
#define CAT_INV_PI_64 0.31830988618379067153776752674503
#define CAT_INV_PI_32 0.3183098862f


// Generic clamp() function
template<class Scalar>
void Clamp(Scalar &x, Scalar low, Scalar high)
{
	if (x < low) x = low;
	else if (x > high) x = high;
}


// Fast inverse square root
f32 InvSqrt(f32 x);


} // namespace cat

#endif // CAT_SCALAR_HPP
