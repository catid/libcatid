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

// 06/23/09 moved from Platform.hpp

#ifndef CAT_ENDIAN_NEUTRAL_HPP
#define CAT_ENDIAN_NEUTRAL_HPP

#include <cat/Platform.hpp>

namespace cat {


// getLE() converts from little-endian word to native byte-order word
// getBE() converts from big-endian word to native byte-order word

#if defined(CAT_ENDIAN_LITTLE)

# define swapLE(n) (n)
# define getLE(n) (n)
# define getLE16(n) (n)
# define getLE32(n) (n)
# define getLE64(n) (n)

    inline u16 swapBE(u16 &n) { return n = CAT_BOSWAP16(n); }
    inline u32 swapBE(u32 &n) { return n = CAT_BOSWAP32(n); }
    inline u64 swapBE(u64 &n) { return n = CAT_BOSWAP64(n); }
    inline u16 getBE(u16 n) { return CAT_BOSWAP16(n); }
    inline u32 getBE(u32 n) { return CAT_BOSWAP32(n); }
    inline u64 getBE(u64 n) { return CAT_BOSWAP64(n); }
    inline u16 getBE16(u16 n) { return CAT_BOSWAP16(n); }
    inline u32 getBE32(u32 n) { return CAT_BOSWAP32(n); }
    inline u64 getBE64(u64 n) { return CAT_BOSWAP64(n); }
    inline s16 swapBE(s16 &n) { return n = CAT_BOSWAP16((u16)n); }
    inline s32 swapBE(s32 &n) { return n = CAT_BOSWAP32((u32)n); }
    inline s64 swapBE(s64 &n) { return n = CAT_BOSWAP64((u64)n); }
    inline s16 getBE(s16 n) { return CAT_BOSWAP16((u16)n); }
    inline s32 getBE(s32 n) { return CAT_BOSWAP32((u32)n); }
    inline s64 getBE(s64 n) { return CAT_BOSWAP64((u64)n); }

    inline float getBE(float n) {
        Float32 c = n;
        c.i = CAT_BOSWAP32(c.i);
        return c.f;
    }

#else

# define swapBE(n) (n)
# define getBE(n) (n)
# define getBE16(n) (n)
# define getBE32(n) (n)
# define getBE64(n) (n)

    inline u16 swapLE(u16 &n) { return n = CAT_BOSWAP16(n); }
    inline u32 swapLE(u32 &n) { return n = CAT_BOSWAP32(n); }
    inline u64 swapLE(u64 &n) { return n = CAT_BOSWAP64(n); }
    inline u16 getLE(u16 n) { return CAT_BOSWAP16(n); }
    inline u32 getLE(u32 n) { return CAT_BOSWAP32(n); }
    inline u64 getLE(u64 n) { return CAT_BOSWAP64(n); }
    inline u16 getLE16(u16 n) { return CAT_BOSWAP16(n); }
    inline u32 getLE32(u32 n) { return CAT_BOSWAP32(n); }
    inline u64 getLE32(u64 n) { return CAT_BOSWAP64(n); }
    inline s16 swapLE(s16 &n) { return n = CAT_BOSWAP16((u16)n); }
    inline s32 swapLE(s32 &n) { return n = CAT_BOSWAP32((u32)n); }
    inline s64 swapLE(s64 &n) { return n = CAT_BOSWAP64((u64)n); }
    inline s16 getLE(s16 n) { return CAT_BOSWAP16((u16)n); }
    inline s32 getLE(s32 n) { return CAT_BOSWAP32((u32)n); }
    inline s64 getLE(s64 n) { return CAT_BOSWAP64((u64)n); }

    inline float getLE(float n) {
        Float32 c = n;
        c.i = CAT_BOSWAP32(c.i);
        return c.f;
    }

#endif


} // namespace cat

#endif // CAT_ENDIAN_NEUTRAL_HPP
