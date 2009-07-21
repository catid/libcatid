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

// 07/17/09 refactored with factory design pattern
// 07/15/09 began

/*
    Based on Fortuna algorithm from "Practical Cryptography" section 10.3
    Published 2003 by Niels Ferguson and Bruce Schneier

    Fortuna supplements the operating system (OS) pseudo-random number generator (PRNG)
    by incorporating additional entropy into the seeds that the OS provides.

    Modified for use with Skein in PRNG mode, sharing the strengths of both algorithms.

    My implementation of Fortuna (call it Cat For Tuna) has the following components:

    + Entropy Pools
        + 32 pools numbered 0..31
        + Implemented as 32 instances of the 256-bit Skein hash
        + Entropy hashed into pools in a round-robin fashion
        + Scheme allows for recovery against attacker with knowledge of some sources

    + Entropy Sources
        + Uses best OS random number generator
        + And a variable number of OS-dependent entropy sources
        + Mainly cycle counts and other timing information

    + Output Generator
        + Implemented as a 256-bit Skein hash of some combination of the entropy pools
            + The output is produced by the PRNG mode of Skein keyed by the pools
        + Hashing all of these pools together and keying the output is called "seeding"
        + Reseeded after sufficient entropy has been collected in pool 0
        + Pool 0 is always used for reseeding
        + Reseed X uses pools numbered by the 1-bits in X (except MSB)
        + Previous seed keys the next seed
        + Diverges from normal Fortuna due to use of Skein instead of a block cipher
            + Reseeds only once every ~51.2 seconds
            + Does not have a limit of 2^16 output blocks
            + Skein-PRNG is guaranteed sufficient security properties anyway

    The Fortuna algorithm is broken up into two objects for efficient thread-safety:

    + FortunaFactory
        + Must be initialized on startup and shut down on shutdown
        + Spawns a thread to periodically collect additional entropy
        + Manages the thread-local storage (TLS) of FortunaOutput objects
        + Provides an interface to get the FortunaOutput for the current thread

    + FortunaOutput
        + Reseeds based on master seed in the FortunaFactory
            + Reseeds after master seed is updated
        + Provides a unique random stream for each thread that uses Fortuna
*/

#ifndef FORTUNA_HPP
#define FORTUNA_HPP

#include <cat/rand/IRandom.hpp>
#include <cat/crypt/hash/Skein.hpp>
#include <cat/Singleton.hpp>

#if defined(CAT_OS_WINDOWS)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace cat {

class FortunaOutput;
class FortunaFactory;


// Factory for constructing FortunaOutput objects
class FortunaFactory : public Singleton<FortunaFactory>
{
    CAT_SINGLETON(FortunaFactory)
    {
    }

    friend class FortunaOutput;

#if defined(CAT_OS_WINDOWS)
    HANDLE EntropyThread, EntropySignal;
    HANDLE CurrentProcess;
    HCRYPTPROV hCryptProv;
    static unsigned int __stdcall EntropyCollectionThreadWrapper(void *factory);
    void EntropyCollectionThread();
#endif

protected:
    static const int ENTROPY_POOLS = 32; // Setting this higher would break something
    static const int POOL_BITS = 256; // Tuned for 256-bit hash
    static const int POOL_BYTES = POOL_BITS / 8;
    static const int POOL_QWORDS = POOL_BYTES / sizeof(u64);

    static u32 MasterSeedRevision; // Should not roll over for 13 years if incremented once every RESEED_MIN_TIME
    static Skein MasterSeed;

    u32 reseed_counter;
    Skein Pool[ENTROPY_POOLS];

    bool Reseed();
    bool InitializeEntropySources();
    void PollInvariantSources(int pool);
    void PollSlowEntropySources(int pool);
    void PollFastEntropySources(int pool);
    void ShutdownEntropySources();

public:
    // Start the entropy generator
    bool Initialize();

    // Stop the entropy generator
    void Shutdown();

    // Allocate/Get the Fortuna object for the current thread
    static FortunaOutput *GetLocalOutput();

    // Free memory associated with this thread to avoid a memory leak
    static void DeleteLocalOutput();
};


// Thread-safe output object for Fortuna
class FortunaOutput : public IRandom
{
    friend class FortunaFactory;

    static const int OUTPUT_CACHE_BYTES = FortunaFactory::POOL_BYTES * 4; // Arbitrary

    u32 thread_id, SeedRevision;
    Skein OutputHash;
    u8 CachedRandomBytes[OUTPUT_CACHE_BYTES];
    int used_bytes;

    void Reseed();

    FortunaOutput();
    FortunaOutput(FortunaOutput&) {}
    FortunaOutput &operator=(FortunaOutput &) {}

public:
    ~FortunaOutput();

    // Generate a 32-bit random number
    u32 Generate();

    // Generate a variable number of random bytes
    void Generate(void *buffer, int bytes);
};


} // namespace cat

#endif // FORTUNA_HPP
