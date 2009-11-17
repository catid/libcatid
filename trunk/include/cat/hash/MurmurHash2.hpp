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

// 10/26/09 updated for the Merkle-Damgard construction of MurmurHash2A
// 06/11/09 part of libcat-1.0

/*
    MurmurHash2 is a very fast noncryptographic 32/64-bit hash

    Algorithm by Austin Appleby <aappleby@gmail.com>
    http://murmurhash.googlepages.com/
*/

#ifndef CAT_MURMUR_HASH_2_HPP
#define CAT_MURMUR_HASH_2_HPP

#include <cat/Platform.hpp>

namespace cat {


// NOTE: Result is NOT endian-neutral.  Use getLE().
u32 MurmurHash32(const void *key, int bytes, u32 seed);
u64 MurmurHash64(const void *key, int bytes, u64 seed);


class IncrementalMurmurHash32
{
	u32 _hash, _tail, _count, _size;

    static const u32 M = 0x5bd1e995;
    static const u32 R = 24;

public:
	void Begin(u32 seed = 0);
	void Add(const void *data, int bytes);
	u32 End();
};


class IncrementalMurmurHash64
{
	u32 _count;
	u64 _hash, _tail, _size;

    static const u64 M = 0xc6a4a7935bd1e995ULL;
    static const u64 R = 47;

public:
	void Begin(u64 seed = 0);
	void Add(const void *data, int bytes);
	u64 End();
};


} // namespace cat

#endif // CAT_MURMUR_HASH_2_HPP
