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

// 07/14/2009 began

/*
    HMAC-MD5 is still secure despite the ease of producing collisions in MD5.
    See Mihir Bellare paper "New Proofs for NMAC and HMAC: Security without Collision-Resistance" (June 2006)

    Using HMAC construction:
        HMAC(x) = h(k || p1 || h(k || p2 || x))
        h() = MD5 hash
        p1,p2 = distinct padding to bring k up to the block size
        p1 = 0x36 repeated, p2 = 0x5c repeated

    Diverges from usual implementation by using little-endian rather than big-endian input
*/

#ifndef HMAC_MD5_HPP
#define HMAC_MD5_HPP

#include <cat/crypt/hash/ICryptHash.hpp>

namespace cat {


class HMAC_MD5 : public ICryptHash
{
protected:
    static const int DIGEST_BYTES = 16;
    static const int WORK_BYTES = 64; // bytes in one block
    static const int WORK_WORDS = WORK_BYTES / sizeof(u32);

    u32 CachedInitialState[4]; // Cached state for H(K||inner padding)
    u32 CachedFinalState[4];   // Cached state for H(K||outer padding)

    u64 byte_counter;
    u32 State[4];
    u8 Work[WORK_BYTES];
    int used_bytes;

    void HashComputation(const void *message, int blocks, u32 *NextState);

    // Unsupported modes
    bool BeginKey(int bits) { return false; }
    bool BeginKDF() { return false; }
    bool BeginPRNG() { return false; }

public:
    ~HMAC_MD5();
    bool SetKey(ICryptHash *parent);
    bool BeginMAC();
    void Crunch(const void *message, int bytes);
    void End();
    void Generate(void *out, int bytes);
};


} // namespace cat

#endif // HMAC_MD5_HPP
