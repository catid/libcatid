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

#include <cat/hash/MurmurHash2.hpp>
#include <cat/port/EndianNeutral.hpp>
using namespace cat;

u32 cat::MurmurHash32(const void *key, int bytes, u32 seed)
{
    const u32 M = 0x5bd1e995;
    const u32 R = 24;

    u32 h = seed ^ bytes;

    // Mix 4 bytes at a time into the hash
    const u32 *key32 = (const u32 *)key;
    const u32 *key32_end = key32 + bytes/sizeof(u32);

    while (key32 != key32_end)
    {
        u32 k = getLE(*key32++);

        k *= M;
        k ^= k >> R;
        k *= M;

        h *= M;
        h ^= k;
    }

    // Handle the last few bytes of the input array
    const u8 *key8 = (const u8 *)key32;

    switch (bytes & 3)
    {
    case 3: h ^= (u32)key8[2] << 16;
    case 2: h ^= (u32)key8[1] << 8;
    case 1: h ^= key8[0];
            h *= M;
    };

    h ^= h >> 13;
    h *= M;
    h ^= h >> 15;

    return h;
}


u64 cat::MurmurHash64(const void *key, int bytes, u64 seed)
{
    const u64 M = 0xc6a4a7935bd1e995LL;
    const u64 R = 47;

    u64 h = seed ^ (bytes * M);

    // Mix 8 bytes at a time into the hash
    const u64 *key64 = (const u64 *)key;
    const u64 *key64_end = key64 + bytes/sizeof(u64);

    while (key64 != key64_end)
    {
        u64 k = getLE(*key64++);

        k *= M;
        k ^= k >> R;
        k *= M;

        h *= M;
        h ^= k;
    }

    // Handle the last few bytes of the input array
    const u8 *key8 = (const u8 *)key64;

    switch (bytes & 7)
    {
    case 7: h ^= (u64)key8[6] << 48;
    case 6: h ^= (u64)key8[5] << 40;
    case 5: h ^= (u64)key8[4] << 32;
    case 4: h ^= getLE(*(u32*)key8);
            h *= M;
            break;
    case 3: h ^= (u64)key8[2] << 16;
    case 2: h ^= (u64)key8[1] << 8;
    case 1: h ^= key8[0];
            h *= M;
    };

    h ^= h >> R;
    h *= M;
    h ^= h >> R;

    return h;
}
