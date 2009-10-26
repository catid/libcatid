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


Quaternion::Quaternion()
{
}

Quaternion::Quaternion(const Vector4f &u)
	: Vector4f(u)
{
}

Quaternion::Quaternion(const Quaternion &u)
	: Vector4f(u)
{
}

Quaternion::Quaternion(f32 x, f32 y, f32 z, f32 n)
	: Vector4f(x, y, z, n)
{
}

Quaternion::Quaternion(f32 xroll, f32 ypitch, f32 zyaw)
{
	f64 croll = cos(0.5f * xroll);
	f64 cpitch = cos(0.5f * ypitch);
	f64 cyaw = cos(0.5f * zyaw);
	f64 sroll = sin(0.5f * xroll);
	f64 spitch = sin(0.5f * ypitch);
	f64 syaw = sin(0.5f * zyaw);

	f64 cyawcpitch = cyaw * cpitch;
	f64 syawspitch = syaw * spitch;
	f64 cyawspitch = cyaw * spitch;
	f64 syawcpitch = syaw * cpitch;

	x() = (f32)(cyawcpitch * sroll - syawspitch * croll);
	y() = (f32)(cyawspitch * croll + syawcpitch * sroll);
	z() = (f32)(syawcpitch * croll - cyawspitch * sroll);
	n() = (f32)(cyawcpitch * croll + syawspitch * sroll);
}

Quaternion Quaternion::operator~() const
{
	return Quaternion(-x(), -y(), -z(), n());
}

Quaternion cat::operator*(const Quaternion &u, const Quaternion &v)
{
	return Quaternion(u.n()*v.x() + u.x()*v.n() + u.y()*v.z() - u.z()*v.y(),
                      u.n()*v.y() + u.y()*v.n() + u.z()*v.x() - u.x()*v.z(),
                      u.n()*v.z() + u.z()*v.n() + u.x()*v.y() - u.y()*v.x(),
                      u.n()*v.n() - u.x()*v.x() - u.y()*v.y() - u.z()*v.z());
}
Quaternion cat::operator*(const Quaternion &u, const Vector3f &v)
{
	return Quaternion(u.n()*v.x() + u.y()*v.z() - u.z()*v.y(),
                      u.n()*v.y() + u.z()*v.x() - u.x()*v.z(),
                      u.n()*v.z() + u.x()*v.y() - u.y()*v.x(),
                      -u.x()*v.x() - u.y()*v.y() - u.z()*v.z());
}
Quaternion cat::operator*(const Vector3f &u, const Quaternion &v)
{
	return Quaternion(u.x()*v.n() + u.y()*v.z() - u.z()*v.y(),
                      u.y()*v.n() + u.z()*v.x() - u.x()*v.z(),
                      u.z()*v.n() + u.x()*v.y() - u.y()*v.x(),
                      -u.x()*v.x() - u.y()*v.y() - u.z()*v.z());
}

Vector3f Quaternion::getVector()
{
	return Vector3f(x(), y(), z());
}
float Quaternion::getScalar()
{
	return n();
}

float Quaternion::getAngle()
{
	return 2.0f * (f32)acos(n());
}

Vector3f Quaternion::getAxis()
{
	return getVector().normalize();
}

Quaternion cat::rotateQuaternion(const Quaternion &q1, const Quaternion &q2)
{
	return q1 * q2 * ~q1;
}

Vector3f cat::rotateVector(const Quaternion &q, const Vector3f &v)
{
	return ( q * v * (~q) ).getVector();
}

Vector3f Quaternion::getEulerAngles()
{
	f64 q00 = n()*n();
	f64 q11 = x()*x();
	f64 q22 = y()*y();
	f64 q33 = z()*z();

	f64 r11 = q00 + q11 - q22 - q33;
	f64 r21 = 2.0f * (x()*y() + n()*z());
	f64 r31 = 2.0f * (x()*z() - (f64)n()*y());
	f64 r32 = 2.0f * (y()*z() + n()*x());
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

Quaternion cat::nlerp(const Quaternion &q1, const Quaternion &q2, f32 t)
{
	// Linearly interpolate and normalize result
	// This formula is a little more work than "q1 + (q2 - q1) * t"
	// but less likely to lose precision when it matters
	return (q1 * (1.0f - t) + q2 * t).normalize();
}
