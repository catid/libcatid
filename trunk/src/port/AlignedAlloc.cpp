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

#include <cat/port/AlignedAlloc.hpp>
using namespace cat;

// Returns a pointer aligned to a 16 byte boundary
void *Aligned::Alloc(int bytes)
{
    u8 *buffer = new u8[16 + bytes];
    if (!buffer) return 0;

#if defined(CAT_ARCH_64)
    u8 offset = 16 - ((u8)*(u64*)&buffer & 15);
#else
    u8 offset = 16 - ((u8)*(u32*)&buffer & 15);
#endif

    buffer += offset;
    buffer[-1] = (u8)offset;

    return buffer;
}

// Frees an aligned pointer
void Aligned::Delete(void *ptr)
{
    if (ptr)
    {
        u8 *buffer = (u8 *)ptr;

        buffer -= buffer[-1];

        delete []buffer;
    }
}
