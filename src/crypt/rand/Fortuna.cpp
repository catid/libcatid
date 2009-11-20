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

#include <cat/crypt/rand/Fortuna.hpp>
#include <cat/time/Clock.hpp>
#include <cat/threads/Atomic.hpp>
using namespace cat;

u32 FortunaFactory::MasterSeedRevision = 0;
Skein FortunaFactory::MasterSeed;


//// FortunaOutput

static volatile u32 thread_id_generator = 0;

FortunaOutput::FortunaOutput()
{
    // Generate a unique thread id for each fortuna object
	thread_id = Atomic::Add(&thread_id_generator, 1);

    Reseed();
}

FortunaOutput::~FortunaOutput()
{
    CAT_OBJCLR(CachedRandomBytes);
    used_bytes = 0;
    SeedRevision = 0;
    thread_id = 0;
}

void FortunaOutput::Reseed()
{
    OutputHash.SetKey(&FortunaFactory::MasterSeed);
    SeedRevision = FortunaFactory::MasterSeedRevision;

    OutputHash.BeginPRNG();
    OutputHash.Crunch(&thread_id, sizeof(thread_id));
    OutputHash.End();

    OutputHash.Generate(CachedRandomBytes, OUTPUT_CACHE_BYTES);
    used_bytes = 0;
}

// Generate a 32-bit random number
u32 FortunaOutput::Generate()
{
    u32 x;
    Generate(&x, sizeof(x));
    return x;
}

// Generate a variable number of random bytes
void FortunaOutput::Generate(void *buffer, int bytes)
{
    if (SeedRevision != FortunaFactory::MasterSeedRevision)
        Reseed();

    int remaining = OUTPUT_CACHE_BYTES - used_bytes;

    // If the cache can fill this request, just copy it out
    if (bytes < remaining)
    {
        memcpy(buffer, CachedRandomBytes + used_bytes, bytes);
        used_bytes += bytes;
        return;
    }

    // Copy as much as we can from what remains
    memcpy(buffer, CachedRandomBytes + used_bytes, remaining);
    bytes -= remaining;
    u8 *output = (u8*)buffer + remaining;

    // Copy whole new blocks at a time
    while (bytes >= OUTPUT_CACHE_BYTES)
    {
        OutputHash.Generate(output, OUTPUT_CACHE_BYTES);
        bytes -= OUTPUT_CACHE_BYTES;
        output += OUTPUT_CACHE_BYTES;
    }

    // Generate a new cached block
    OutputHash.Generate(CachedRandomBytes, OUTPUT_CACHE_BYTES);

    // Copy any remaining needed bytes
    memcpy(output, CachedRandomBytes, bytes);
    used_bytes = bytes;
}


//// FortunaFactory

bool FortunaFactory::Reseed()
{
    Skein NextSeed;

    // Feed back previous output into next output after the first seeding
    if (reseed_counter == 0)
        NextSeed.BeginKey(POOL_BITS);
    else
        NextSeed.SetKey(&MasterSeed);

    NextSeed.BeginPRNG();

    u8 PoolOutput[POOL_BYTES];

    // Pool 0 is always used for re-seeding
    Pool[0].End();
    Pool[0].Generate(PoolOutput, POOL_BYTES);
    NextSeed.Crunch(PoolOutput, POOL_BYTES);
    Pool[0].BeginKey(POOL_BITS);

    // Pool 1 is used every other time, and so on
    for (int ii = 1; ii < ENTROPY_POOLS; ++ii)
    {
        if (reseed_counter & (1 << (ii-1)))
        {
            Pool[ii].End();
            Pool[ii].Generate(PoolOutput, POOL_BYTES);
            NextSeed.Crunch(PoolOutput, POOL_BYTES);
            Pool[ii].BeginKey(POOL_BITS);
        }
    }

    ++reseed_counter;

    // Initialize the master seed with the new seed
    NextSeed.End();
    MasterSeed.SetKey(&NextSeed);
    ++MasterSeedRevision;

    return true;
}

// Start the entropy generator
bool FortunaFactory::Initialize()
{
	if (_initialized)
		return true;

    MasterSeedRevision = 0;
    reseed_counter = 0;

    // Initialize all the pools
    for (int ii = 0; ii < ENTROPY_POOLS; ++ii)
        Pool[ii].BeginKey(POOL_BITS);

    // Initialize the various OS-dependent entropy sources
    if (!InitializeEntropySources())
        return false;

    // Reseed the pools from the entropy sources
    if (!Reseed())
        return false;

	_initialized = true;

    return true;
}

// Stop the entropy generator
void FortunaFactory::Shutdown()
{
	if (_initialized)
	{
		// Block and wait for entropy collection thread to end
		ShutdownEntropySources();

		_initialized = false;
	}
}

// Create a new Fortuna object
FortunaOutput *FortunaFactory::Create()
{
    return new FortunaOutput;
}
