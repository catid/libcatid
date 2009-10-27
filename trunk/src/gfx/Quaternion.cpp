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

#include <cat/gfx/Quaternion.hpp>
using namespace cat;


Vector3f Quaternion::getEulerAngles()
{
	f64 q00 = w()*w();
	f64 q11 = x()*x();
	f64 q22 = y()*y();
	f64 q33 = z()*z();

	f64 r11 = q00 + q11 - q22 - q33;
	f64 r21 = 2.0f * (x()*y() + w()*z());
	f64 r31 = 2.0f * (x()*z() - (f64)w()*y());
	f64 r32 = 2.0f * (y()*z() + w()*x());
	f64 r33 = q00 - q11 - q22 + q33;

	f64 tmp = fabs(r31);
	if (tmp > 0.999999)
	{
		f64 r12 = 2 * (x()*y() - (f64)n()*z());
		f64 r13 = 2 * (x()*z() + n()*y());

		return Vector3f(0,
						(f32)(-M_PI/2.0f * r31 / tmp),
						(f32)atan2(-r12, -r31*r13));
	}

	return Vector3f((f32)atan2(r32, r33),
					(f32)asin(-r31),
					(f32)atan2(r21, r11));
}

// Precondition: q1, q2 are unit length
Quaternion cat::slerp(const Quaternion &q1, const Quaternion &q2, f32 t)
{
	// Cosine of angle between the two vectors
	float cosang = q1.dot(q2);

	if (cosang > 0.9995)
	{
		// If the inputs are close, fall back to nlerp()
		return nlerp(q1, q2, t);
	}
	else
	{
		clamp(cosang, -1.0f, 1.0f); // stay within the domain of acos()
		float theta = acos(cosang) * t; // theta = angle between q1 and result

		Quaternion q3 = (q2 - q1 * cosang).normalize();
		// { q1, q3 } is now an orthonormal basis

		return q1 * cosf(theta) + q3 * sinf(theta);
	}
}
