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

#ifndef CAT_VECTOR_HPP
#define CAT_VECTOR_HPP

#include <cat/gfx/Scalar.hpp>

#define FOR_EACH_DIMENSION(index) for (int index = 0; index < DIM; ++index)

#define DEFINE_CTORS(Vector, Scalar, DIM) \
	/* Uninitialized vector is not cleared */ \
	Vector() {} \
\
	/* Component-wise initializing constructors */ \
	Vector(Scalar x, Scalar y) \
	{ \
		_elements[0] = x; \
		_elements[1] = y; \
	} \
	Vector(Scalar x, Scalar y, Scalar z) \
	{ \
		_elements[0] = x; \
		_elements[1] = y; \
		_elements[2] = z; \
	} \
	Vector(Scalar x, Scalar y, Scalar z, Scalar w) \
	{ \
		_elements[0] = x; \
		_elements[1] = y; \
		_elements[2] = z; \
		_elements[3] = w; \
	} \
\
	/* Copy constructor */ \
	Vector(const mytype &u) \
	{ \
		for (int ii = 0; ii < DIM; ++ii) \
			_elements[ii] = u(ii); \
	}

namespace cat {


// Generic vector class for linear algebra
template<int DIM, class Scalar> class Vector
{
protected:
	// Protected internal storage of vector components
	Scalar _elements[DIM];

public:
	// Short-hand for the current vector type
	typedef Vector<DIM, Scalar> mytype;

	DEFINE_CTORS(Vector, Scalar, DIM)

	// Assignment operator
	mytype &operator=(const mytype &u)
	{
		memcpy(_elements, u._elements, sizeof(_elements));
	}

	// Magnitude calculation
	Scalar magnitude() const
	{
		Scalar element = _elements[0];
		Scalar sum = element * element;

		for (int ii = 1; ii < DIM; ++ii)
		{
			element = _elements[ii];
			sum += element * element;
		}

		return static_cast<Scalar>( sqrt(sum) );
	}

	// Normalization in-place
	mytype &normalize()
	{
		Scalar m = magnitude();

		FOR_EACH_DIMENSION(ii)
		{
			_elements[ii] = _elements[ii] / m;
		}

		return *this;
	}

	// Zero elements
	void zero()
	{
		OBJCLR(_elements);
	}

	// Is zero?
	bool isZero()
	{
		FOR_EACH_DIMENSION(ii)
		{
			if (_elements[ii] != static_cast<Scalar>( 0 ))
				return false;
		}

		return true;
	}

	// For consistency with Matrix class, use the () operator instead of [] to index it
	inline Scalar &operator()(int ii) { return _elements[ii]; }
	inline Scalar &x() { return _elements[0]; }
	inline Scalar &y() { return _elements[1]; }
	inline Scalar &z() { return _elements[2]; }
	inline Scalar &w() { return _elements[3]; }

	// Const version for accessors
	inline const Scalar &operator()(int ii) const { return _elements[ii]; }
	inline const Scalar &x() const { return _elements[0]; }
	inline const Scalar &y() const { return _elements[1]; }
	inline const Scalar &z() const { return _elements[2]; }
	inline const Scalar &w() const { return _elements[3]; }

	// Negation
	mytype operator-() const
	{
		mytype x;

		FOR_EACH_DIMENSION(ii) x._elements[ii] = -_elements[ii];

		return x;
	}

	// Negation in-place
	mytype &negate()
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] = -_elements[ii];

		return *this;
	}

	// Addition
	mytype operator+(const mytype &u) const
	{
		mytype x;

		FOR_EACH_DIMENSION(ii) x._elements[ii] = _elements[ii] + u._elements[ii];

		return x;
	}

	// Addition in-place
	mytype &operator+=(const mytype &u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] += u._elements[ii];

		return *this;
	}

	// Add a scalar to each element in-place
	mytype &addToEachElement(Scalar u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] += u;

		return *this;
	}

	// Subtraction
	mytype operator-(const mytype &u) const
	{
		mytype x;

		FOR_EACH_DIMENSION(ii) x._elements[ii] = _elements[ii] - u._elements[ii];

		return x;
	}

	// Subtraction in-place
	mytype &operator-=(const mytype &u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] -= u._elements[ii];

		return *this;
	}

	// Subtract a scalar from each element in-place
	mytype &subtractFromEachElement(Scalar u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] -= u;

		return *this;
	}

	// Scalar multiplication
	mytype operator*(Scalar u) const
	{
		mytype x;

		FOR_EACH_DIMENSION(ii) x._elements[ii] = u * _elements[ii];

		return x;
	}

	// Scalar multiplication in-place
	mytype &operator*=(Scalar u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] *= u;

		return *this;
	}

	// Component-wise multiply in-place
	mytype &componentMultiply(const mytype &u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] *= u._elements[ii];

		return *this;
	}

	// Scalar division
	mytype operator/(Scalar u) const
	{
		mytype x;

		FOR_EACH_DIMENSION(ii) x._elements[ii] = _elements[ii] / u;

		return x;
	}

	// Scalar division in-place
	mytype &operator/=(Scalar u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] /= u;

		return *this;
	}

	// Component-wise divide in-place
	mytype &componentDivide(const mytype &u)
	{
		FOR_EACH_DIMENSION(ii) _elements[ii] /= u._elements[ii];

		return *this;
	}

	// Dot product
	Scalar dotProduct(const mytype &u) const
	{
		Scalar sum = _elements[0] * u._elements[0];

		for (int ii = 1; ii < DIM; ++ii)
		{
			sum += _elements[ii] * u._elements[ii];
		}

		return sum;
	}
};


// Specialized for 32-bit floating point elements
template<int DIM>
class Vectorf32 : public Vector<DIM, f32>
{
public:
	DEFINE_CTORS(Vectorf32, f32, DIM)

	// Magnitude calculation
	f32 magnitude() const
	{
		f64 element = _elements[0];
		f64 sum = element * element;

		for (int ii = 1; ii < DIM; ++ii)
		{
			element = _elements[ii];
			sum += element * element;
		}

		return static_cast<f32>( sqrt(sum) );
	}

	// Normalization in-place
	mytype &normalize()
	{
		f32 element = _elements[0];
		f32 sum = element * element;

		for (int ii = 1; ii < DIM; ++ii)
		{
			element = _elements[ii];
			sum += element * element;
		}

		// If sum is not close to 1.0,
		if (sum > 1.005f || sum < 0.995f)
		{
			f32 inv_sqrt = InvSqrt(sum);

			FOR_EACH_DIMENSION(ii)
			{
				_elements[ii] = inv_sqrt * _elements[ii];
			}
		}

		return *this;
	}

	// Scalar division
	mytype operator/(f32 u) const
	{
		mytype x;

		f32 u_inv = 1.0f / u;

		FOR_EACH_DIMENSION(ii) x._elements[ii] = u_inv * _elements[ii];

		return x;
	}

	// Scalar division in-place
	mytype &operator/=(f32 u)
	{
		f32 u_inv = 1.0f / u;

		FOR_EACH_DIMENSION(ii) _elements[ii] *= u_inv;

		return *this;
	}
};


// Specialized for 2D vectors of 32-bit floating point elements
class Vector2f32 : public Vectorf32<2>
{
public:
	DEFINE_CTORS(Vector2f32, f32, 2)

	// Generate a 2D rotation vector in-place
	void generateRotation(f32 angle);

	// Add rotation vector in-place
	mytype &addRotation(const mytype &r);

	// Subtract rotation vector in-place
	mytype &subtractRotation(const mytype &r);

	// Cross product: Result is a scalar
	f32 crossProduct(const mytype &u);
};


// Specialized for 3D vectors with 32-bit floating point elements
class Vector3f32 : public Vectorf32<3>
{
public:
	DEFINE_CTORS(Vector3f32, f32, 3)

	// Cross product: Result is a 3D vector
	mytype crossProduct(const mytype &u);
};


// Specialized for 3D vectors with 32-bit floating point elements
template<class Scalar>
class Vector3 : public Vector<3, Scalar>
{
public:
	DEFINE_CTORS(Vector3, Scalar, 3)

	// Cross product: Result is a 3D vector
	mytype crossProduct(const mytype &u)
	{
		mytype result;

		result.x() = y() * u.z() - z() * u.y();
		result.y() = z() * u.x() - x() * u.z();
		result.z() = x() * u.y() - y() * u.x();

		return result;
	}
};


// Short-hand for common usages:

typedef Vector<2, u32> Vector2u;
typedef Vector3<u32>   Vector3u;
typedef Vector<4, u32> Vector4u;

typedef Vector<2, s32> Vector2s;
typedef Vector3<s32>   Vector3s;
typedef Vector<4, s32> Vector4s;

typedef Vector2f32   Vector2f;
typedef Vector3f32   Vector3f;
typedef Vectorf32<4> Vector4f;

typedef Vector<2, f64> Vector2d;
typedef Vector3<f64>   Vector3d;
typedef Vector<4, f64> Vector4d;


} // namespace cat

#undef DEFINE_CTORS
#undef FOR_EACH_DIMENSION

#endif // CAT_VECTOR_HPP
