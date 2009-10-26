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

#ifndef CAT_MATRIX_HPP
#define CAT_MATRIX_HPP

#include <cat/gfx/Vector.hpp>

namespace cat {

/*
	4x4 matrix elements arranged column major (row, column):

		m[0]  m[4]  m[8]  m[12]
		m[1]  m[5]  m[9]  m[13]
		m[2]  m[6]  m[10] m[14]
		m[3]  m[7]  m[11] m[15]

	Matrix element order corresponds to OpenGL matrix ordering s.t. in a
	4x4 matrix the elements define the following new coordinate system:

		new x-axis vector: { m[0]  m[1]  m[2]  }
		new y-axis vector: { m[4]  m[5]  m[6]  }
		new z-axis vector: { m[7]  m[9]  m[10] }
		new origin:        { m[12] m[13] m[14] }
*/

template<int ROWS, int COLS, class Scalar> class Matrix
{
protected:
	Scalar _elements[ROWS * COLS];

public:
	typedef Matrix<ROWS, COLS, Scalar> this_type;

	Matrix();

	this_type &operator+=(const this_type &u);
	this_type &operator-=(const this_type &u);
	this_type &operator*=(Scalar u);
	this_type &operator/=(Scalar u);

	inline Scalar &operator()(int ii) { return _array[ii]; }
	inline const Scalar &operator()(int ii) const { return _array[ii]; }

	inline Scalar &operator()(int row, int col) { return _array[col * ROWS + row]; }
	inline const Scalar &operator()(int row, int col) const { return _array[col * ROWS + row]; }
};


// Short-hand for common usages:

typedef Matrix<2, 2, u32> Matrix2x2u;
typedef Matrix<3, 3, u32> Matrix3x3u;
typedef Matrix<4, 4, u32> Matrix4x4u;

typedef Matrix<2, 2, s32> Matrix2x2i;
typedef Matrix<3, 3, s32> Matrix3x3i;
typedef Matrix<4, 4, s32> Matrix4x4i;

typedef Matrix<2, 2, f32> Matrix2x2f;
typedef Matrix<3, 3, f32> Matrix3x3f;
typedef Matrix<4, 4, f32> Matrix4x4f;


} // namespace cat

#endif // CAT_MATRIX_HPP
