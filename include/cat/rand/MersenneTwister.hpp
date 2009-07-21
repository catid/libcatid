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

// 06/15/09 derives from IRandom
// 06/11/09 part of libcat-1.0

/*
    Algorithm by Makoto Matsumoto
    http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
*/

#ifndef CAT_MERSENNE_TWISTER_HPP
#define CAT_MERSENNE_TWISTER_HPP

#include <cat/rand/IRandom.hpp>

namespace cat {


// Noncryptographic pseudo-random number generator
class MersenneTwister : public IRandom
{
    static const u32 MEXP = 19937;
    static const u32 N128 = MEXP/128 + 1;
    static const u32 N64 = N128 * 2;
    static const u32 N32 = N128 * 4;
    static const u32 POS1 = 122;
    static const u32 SL1 = 18;
    static const u32 SL2 = 1;
    static const u32 SL2BITS = SL2*8;
    static const u32 SR1 = 11;
    static const u32 SR2 = 1;
    static const u32 SR2BITS = SR2*8;
    static const u32 MSK1 = 0xdfffffefU;
    static const u32 MSK2 = 0xddfecb7fU;
    static const u32 MSK3 = 0xbffaffffU;
    static const u32 MSK4 = 0xbffffff6U;

    struct MT128 {
        u32 u[4];
    };

    MT128 state[19937/128 + 1];
    u32 *state32;
    u32 used;

    CAT_INLINE void shiftLeft128(MT128 *r, MT128 *n, u32 bits);
    CAT_INLINE void shiftRight128(MT128 *r, MT128 *n, u32 bits);

    void enforcePeriod(); // make corrections to ensure that the generator has the full period
    void round(MT128 *a, MT128 *b, MT128 *c, MT128 *d); // a = MTMIX(a,b,c,d)
    void update(); // permute the existing state into a new one

public:
    MersenneTwister();

    bool Initialize(u32 seed);
    bool Initialize(u32 *seeds, u32 words);
    bool Initialize();

    u32 Generate(); // generate a 32-bit number
    void Generate(void *buffer, int bytes); // generate a series of random numbers
};


} // namespace cat

#endif // CAT_MERSENNE_TWISTER_HPP
