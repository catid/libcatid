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


// Generic quaternion class for linear algebra
template <typename Scalar, typename Double>
class Quaternion
{
protected:
	Vector<4, Scalar, Double> _v;

public:
	// Short-hand for the current quaternion type
	typedef Quaternion<Scalar, Double> mytype;

	// Short-hand for the vector type
	typedef Vector<3, Scalar, Double> vectype;

	// Uninitialized quaternion is not cleared
	Quaternion() { }

	// Copy constructor
	Quaternion(const mytype &u)
		: _v(u._v)
	{
	}

	// Initialization constructor
	Quaternion(const vectype &v, Scalar w)
		: _v(v)
	{
	}

	// Initialization constructor
	Quaternion(Scalar x, Scalar y, Scalar z, Scalar w)
		: _v(x, y, z, w)
	{
	}

	// Assignment operator
	mytype &operator=(const mytype &u)
	{
		_v.copy(u._v);

		return *this;
	}

	// Euler angle constructor, angles in radians (see Deg2Rad in Scalar.hpp)
	Quaternion(f32 xroll, f32 ypitch, f32 zyaw)
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

		_v.x() = static_cast<Scalar>( cyawcpitch * sroll - syawspitch * croll );
		_v.y() = static_cast<Scalar>( cyawspitch * croll + syawcpitch * sroll );
		_v.z() = static_cast<Scalar>( syawcpitch * croll - cyawspitch * sroll );
		_v.w() = static_cast<Scalar>( cyawcpitch * croll + syawspitch * sroll );
	}

	// Conjugate
	mytype operator~() const
	{
		return mytype(-_v.x(), -_v.y(), -_v.z(), _w);
	}

	// Conjugate in-place
	mytype &conjugate() const
	{
		_v.negate();

		return *this;
	}

	// Multiply by quaternion
	mytype operator*(const mytype &u)
	{
		// Cache each of the elements since each is used 4 times
		Double x1 = _v(0), x2 = u._v(0);
		Double y1 = _v(1), y2 = u._v(1);
		Double z1 = _v(2), z2 = u._v(2);
		Double w1 = _v(3), w2 = u._v(3);

		// Quaternion multiplication formula:
		Scalar x3 = static_cast<Scalar>( w1*x2 + x1*w2 + y1*z2 - z1*y2 );
		Scalar y3 = static_cast<Scalar>( w1*y2 - x1*z2 + y1*w2 + z1*x2 );
		Scalar z3 = static_cast<Scalar>( w1*z2 + x1*y2 - y1*x2 + z1*w2 );
		Scalar w3 = static_cast<Scalar>( w1*w2 - x1*x2 - y1*y2 - z1*z2 );

		return mytype(x3, y3, z3, w3);
	}

	// Multiply by vector
	mytype operator*(const vectype &u)
	{
		// Cache each of the elements since each is used 4 times
		Double x1 = _v(0), x2 = u(0);
		Double y1 = _v(1), y2 = u(1);
		Double z1 = _v(2), z2 = u(2);
		Double w1 = _v(3);

		// Quaternion multiplication formula:
		Scalar x3 = static_cast<Scalar>( w1*x2 + y1*z2 - z1*y2 );
		Scalar y3 = static_cast<Scalar>( w1*y2 - x1*z2 + z1*x2 );
		Scalar z3 = static_cast<Scalar>( w1*z2 + x1*y2 - y1*x2 );
		Scalar w3 = static_cast<Scalar>( -(x1*x2 + y1*y2 + z1*z2) );

		return mytype(x3, y3, z3, w3);
	}

	// Get angle of rotation
	Scalar GetAngle()
	{
		return static_cast<Scalar>( 2 ) * static_cast<Scalar>( acos(_w) );
	}

	// Get vector of direction
	vectype &GetVector()
	{
		return _v;
	}

	// Rotate given quaternion in-place
	void rotate(mytype &u)
	{
		// Implements the simple formula: this * u * ~this

		// Cache each of the elements since each is used 4 times
		Double x1 = _v(0), x2 = u._v(0);
		Double y1 = _v(1), y2 = u._v(1);
		Double z1 = _v(2), z2 = u._v(2);
		Double w1 = _v(3), w2 = u._v(3);

		// Quaternion multiplication formula (q3 = q1 * q2):
		Double x3 = w1*x2 + x1*w2 + y1*z2 - z1*y2;
		Double y3 = w1*y2 - x1*z2 + y1*w2 + z1*x2;
		Double z3 = w1*z2 + x1*y2 - y1*x2 + z1*w2;
		Double w3 = w1*w2 - x1*x2 - y1*y2 - z1*z2;

		// Quaternion multiplication formula (q2' = q3 * ~q1):
		u._v(0) = static_cast<Scalar>( w1*x3 - x1*w3 + y1*z3 - z1*y3 );
		u._v(1) = static_cast<Scalar>( w1*y3 - x1*z3 - y1*w3 + z1*x3 );
		u._v(2) = static_cast<Scalar>( w1*z3 + x1*y3 - y1*x3 - z1*w3 );
		u._v(3) = static_cast<Scalar>( w1*w3 + x1*x3 + y1*y3 + z1*z3 );
	}

	// Rotate given vector in-place
	void rotate(vectype &u)
	{
		// Implements the simple formula: (q1 * u2 * ~q1).GetVector()

		// Cache each of the elements since each is used 4 times
		Double x1 = _v(0), x2 = u(0);
		Double y1 = _v(1), y2 = u(1);
		Double z1 = _v(2), z2 = u(2);
		Double w1 = _v(3);

		// Quaternion-vector multiplication formula (q3 = q1 * u2):
		Double x3 = w1*x2 + y1*z2 - z1*y2;
		Double y3 = w1*y2 - x1*z2 + z1*x2;
		Double z3 = w1*z2 + x1*y2 - y1*x2;
		Double w3 = -(x1*x2 + y1*y2 + z1*z2);

		// Quaternion multiplication formula (q2' = q3 * ~q1):
		u(0) = static_cast<Scalar>( w1*x3 - x1*w3 + y1*z3 - z1*y3 );
		u(1) = static_cast<Scalar>( w1*y3 - x1*z3 - y1*w3 + z1*x3 );
		u(2) = static_cast<Scalar>( w1*z3 + x1*y3 - y1*x3 - z1*w3 );
	}

	// NLERP: Very fast, Non-constant velocity and torque-minimal
	// Precondition: q1, q2 are unit length
	friend void nlerp(const mytype &q1, const mytype &q2, f32 t, mytype &result)
	{
		// Linearly interpolate and normalize result
		// This formula is a little more work than "q1 + (q2 - q1) * t"
		// but less likely to lose precision when it matters

		result = (q1._v * (1.0f - t) + q2._v * t).normalize();
	}

	// SLERP: Constant velocity and torque-minimal
	// Precondition: q1, q2 are unit length
	friend void slerp(const mytype &q1, const mytype &q2, f32 t, mytype &result)
	{
		// Cosine of angle between the two vectors
		Double phi = q1._v.dotProduct(q2._v);

		if (phi > 0.9995)
		{
			// If the inputs are close, fall back to nlerp()
			nlerp(q1, q2, t, result);
		}
		else
		{
			// Stay within the domain of acos()
			Clamp(phi, -1.0f, 1.0f);

			// theta = angle between q1 and result
			Double theta = static_cast<Double>( acos(phi) ) * t;

			result = static<Scalar>( cos(theta) ) * q1._v
				   + static<Scalar>( sin(theta) ) * (q2._v - q1._v * phi).normalize();
		}
	}

	Vector3f getEulerAngles()
	{
	}
};


} // namespace cat

#endif // CAT_QUATERNION_HPP
