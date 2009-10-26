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

#include <cat/gfx/Vector.hpp>
using namespace cat;


// Generate a 2D rotation vector in-place
void Vector2f32::generateRotation(f32 angle)
{
	x() = cosf(angle);
	y() = sinf(angle);
}

// Add rotation vector in-place
mytype &Vector2f32::addRotation(const mytype &r)
{
	f32 ax = x(), ay = y();
	f32 nx = r.x(), ny = r.y();

	x() = ax*nx - ay*ny; // cos(a+n) = cos(a)*cos(n) - sin(a)*sin(n)
	y() = ay*nx + ax*ny; // sin(a+n) = sin(a)*cos(n) + cos(a)*sin(n)

	return *this;
}

// Subtract rotation vector in-place
mytype &Vector2f32::subtractRotation(const mytype &r)
{
	f32 ax = x(), ay = y();
	f32 nx = r.x(), ny = r.y();

	x() = ax*nx + ay*ny; // cos(a-n) = cos(a)*cos(n) + sin(a)*sin(n)
	y() = ay*nx - ax*ny; // sin(a-n) = sin(a)*cos(n) - cos(a)*sin(n)

	return *this;
}

// Cross product: Result is a scalar
f32 Vector2f32::crossProduct(const mytype &u)
{
	return x() * u.y() - y() * u.x();
}


// Cross product: Result is a 3D vector
mytype Vector3f32::crossProduct(const mytype &u)
{
	mytype result;

	result.x() = y() * u.z() - z() * u.y();
	result.y() = z() * u.x() - x() * u.z();
	result.z() = x() * u.y() - y() * u.x();

	return result;
}
