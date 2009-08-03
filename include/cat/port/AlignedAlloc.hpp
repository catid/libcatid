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
	Aligned() {}

	// Acquires memory aligned to a CPU cache-line byte boundary from the heap
    static void *Acquire(int bytes);

    // Release an aligned pointer
    static void Release(void *ptr);

    template<class T>
    static inline void Delete(T *ptr)
    {
        ptr->~T();
        Release(ptr);
    }

	static Aligned ii;
};


} // namespace cat

#include <cstddef>

// Provide placement new constructor and delete pair to allow for
// an easy syntax to create objects from the RegionAllocator:
//   T *a = new (Aligned()) T();
// The object can be freed with:
//   Aligned::Delete(a);
// Which insures that the destructor is called before freeing memory
inline void *operator new[](std::size_t bytes, cat::Aligned &) throw()
{
	return cat::Aligned::Acquire((int)bytes);
}

inline void operator delete(void *ptr, cat::Aligned &) throw()
{
	cat::Aligned::Release(ptr);
}

#endif // CAT_ENDIAN_NEUTRAL_HPP
