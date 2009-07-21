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

// 06/15/09 began

#ifndef CAT_I_CRYPT_HASH_HPP
#define CAT_I_CRYPT_HASH_HPP

#include <cat/Platform.hpp>

namespace cat {


// Cryptographic hash functions of any size will derive from ICryptoHash and implement its public methods
class ICryptHash
{
protected:
    u32 digest_bytes;

public:
    virtual ~ICryptHash() {}

    // Returns the number of bytes in a message digest produced by this hash
    CAT_INLINE int GetDigestByteCount() { return digest_bytes; }

    CAT_INLINE void CrunchString(const char *s) { Crunch(s, (int)strlen(s) + 1); }

public:
    // Begin a new key
    virtual bool BeginKey(int bits) = 0;

    // Start from an existing key
    virtual bool SetKey(ICryptHash *parent) = 0;

    // Begin hash function in MAC, KDF, or PRNG mode
    virtual bool BeginMAC() = 0;
    virtual bool BeginKDF() = 0;
    virtual bool BeginPRNG() = 0;

    // Crunch some message bytes
    virtual void Crunch(const void *message, int bytes) = 0;

    // Finalize the hash and prepare to generate output
    virtual void End() = 0;

    // Extended hash output mode
    virtual void Generate(void *out, int bytes) = 0;
};


} // namespace cat

#endif // CAT_I_CRYPT_HASH_HPP
