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

/*
    Based on code from "Physics for Game Developers", David M. Bourg

    slerp() code adapted from this website:
    http://www.number-none.com/product/Understanding%20Slerp,%20Then%20Not%20Using%20It/index.html
*/

#ifndef CAT_QUATERNION_HPP
#define CAT_QUATERNION_HPP

#include <cat/gfx/Vector.hpp>
#include <cat/gfx/Matrix.hpp>

namespace cat {


class Quaternion : public Vector4f
{
public:
	Quaternion();
	Quaternion(const Vector4f &u);
	Quaternion(const Quaternion &u);
	Quaternion(f32 x, f32 y, f32 z, f32 n);
	Quaternion(f32 xroll, f32 ypitch, f32 zyaw); // angles in radians

	Quaternion operator~() const; // conjugate

	friend Quaternion operator*(const Quaternion &u, const Quaternion &v);
	friend Quaternion operator*(const Quaternion &u, const Vector3f &v);
	friend Quaternion operator*(const Vector3f &u, const Quaternion &v);

	Vector3f getVector(); // get vector part
	f32 getScalar(); // get scalar part

	f32 getAngle(); // get angle of rotation about the axis represented by the vector part
	Vector3f getAxis(); // get unit vector along the axis of rotation represented by vector part

	friend Quaternion rotateQuaternion(const Quaternion &q1, const Quaternion &q2); // rotate q1 by q2
	friend Vector3f rotateVector(const Quaternion &q, const Vector3f &v); // rotate q by v

	// Precondition: q1, q2 are unit length
	friend Quaternion slerp(const Quaternion &q1, const Quaternion &q2, f32 t);

	friend Quaternion nlerp(const Quaternion &q1, const Quaternion &q2, f32 t);

	Vector3f getEulerAngles();
};


} // namespace cat

#endif // CAT_QUATERNION_HPP
