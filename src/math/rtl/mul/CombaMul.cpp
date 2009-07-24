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

#include <cat/math/BigPseudoMersenne.hpp>
using namespace cat;

// Comba Multiplication in template meta-programming

template<int Index> class CombaT {};


//// Inner loop

template<int I, int J, int Count> CAT_INLINE void Comba2(const Leg *a, const Leg *b, Leg &c0, Leg &c1, Leg &c2, CombaT<I>, CombaT<J>, CombaT<Count>)
{
	CAT_LEG_COMBA3(a[I], b[J], c0, c1, c2);

	Comba2(a, b, c0, c1, c2, CombaT<I-1>(), CombaT<J+1>(), CombaT<Count-1>());
}

template<int I, int J> CAT_INLINE void Comba2(const Leg *a, const Leg *b, Leg &c0, Leg &c1, Leg &c2, CombaT<I>, CombaT<J>, CombaT<0>)
{
	CAT_LEG_COMBA3(a[I], b[J], c0, c1, c2);
}


//// Outer loop: Second Half

template<int L, int Count> CAT_INLINE void Comba3(const Leg *a, const Leg *b, Leg *out, Leg &c0, Leg &c1, Leg &c2, CombaT<L>, CombaT<Count>)
{
	CAT_LEG_COMBA2(a[L-1], b[L-1 - Count], c0, c1, c2);

	Comba2(a, b, c0, c1, c2, CombaT<L-2>(), CombaT<L - Count>(), CombaT<Count-1>());

	out[2*L - Count - 2] = c0;

	Comba3(a, b, out, c1, c2, c0, CombaT<L>(), CombaT<Count-1>());
}

template<int L> CAT_INLINE void Comba3(const Leg *a, const Leg *b, Leg *out, Leg &c0, Leg &c1, Leg &c2, CombaT<L>, CombaT<0>)
{
	CAT_LEG_COMBA2(a[L-1], b[L-1], c0, c1, c2);

	out[2*L - 2] = c0;
	out[2*L - 1] = c1;
}


//// Outer loop: First Half

template<int L, int Count> CAT_INLINE void Comba1(const Leg *a, const Leg *b, Leg *out, Leg &c0, Leg &c1, Leg &c2, CombaT<L>, CombaT<Count>)
{
	CAT_LEG_COMBA2(a[Count], b[0], c0, c1, c2);

	Comba2(a, b, c0, c1, c2, CombaT<Count-1>(), CombaT<1>(), CombaT<Count-1>());

	out[Count] = c0;

	Comba1(a, b, out, c1, c2, c0, CombaT<L>(), CombaT<Count+1>());
}

template<int L> CAT_INLINE void Comba1(const Leg *a, const Leg *b, Leg *out, Leg &c0, Leg &c1, Leg &c2, CombaT<L>, CombaT<L-1>)
{
	CAT_LEG_COMBA2(a[L-1], b[0], c0, c1, c2);

	Comba2(a, b, c0, c1, c2, CombaT<L-2>(), CombaT<1>(), CombaT<L-2>());

	out[L-1] = c0;

	Comba3(a, b, out, c1, c2, c0, CombaT<L>(), CombaT<L-2>());
}


//// Entrypoint

template<int L> void CombaMul(const Leg *a, const Leg *b, Leg *out)
{
	Leg c0, c1 = 0, c2;

	CAT_LEG_MUL(a[0], b[0], c0, out[0]);

	Comba1(a, b, out, c0, c1, c2, CombaT<L>(), CombaT<1>());
}
