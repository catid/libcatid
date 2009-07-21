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

// 07/20/09 began

#ifndef CAT_ALIGNED_ALLOC_HPP
#define CAT_ALIGNED_ALLOC_HPP

#include <cat/Platform.hpp>

namespace cat {


class Aligned
{
public:
	// Allocates memory aligned to a 16 byte boundary from the heap
	static void *Alloc(int bytes);

	// Allocates an array of a given type on a 16 byte boundary from the heap
	template<typename T> static T *New(int array_size)
	{
		return (T *)Alloc(sizeof(T) * array_size);
	}

	// Frees an aligned pointer
	static void Delete(void *ptr);
};


} // namespace cat

#endif // CAT_ENDIAN_NEUTRAL_HPP
