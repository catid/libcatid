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
using namespace cat;

#if defined(CAT_OS_WINDOWS_CE)

// Include and link to various Windows libraries needed to collect system info
#pragma comment(lib, "coredll")

#if !defined(CAT_NO_ENTROPY_THREAD)

void FortunaFactory::EntropyCollectionThread()
{
	// No entropy collection thread for Windows CE
	// For this platform we just query the CryptoAPI and some other sources
	// on startup and let the PRNG run forever without any new data
}

#endif // !defined(CAT_NO_ENTROPY_THREAD)


bool FortunaFactory::InitializeEntropySources()
{
    // Initialize a session with the CryptoAPI using newer cryptoprimitives (AES)
    if (!CryptAcquireContext(&hCryptProv, 0, 0, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;

    // Fire poll for entropy all goes into pool 0
    PollInvariantSources(0);

    return true;
}

void FortunaFactory::ShutdownEntropySources()
{
    if (hCryptProv) CryptReleaseContext(hCryptProv, 0);
}

void FortunaFactory::PollInvariantSources(int pool_index)
{
    Skein &pool = Pool[pool_index];

	struct {
		u32 cycles_start;
	    u8 system_prng[32];
	    SYSTEM_INFO sys_info;
		DWORD win_ver;
		u32 cycles_end;
	} Sources;

    // Cycles at the start
    Sources.cycles_start = Clock::cycles();

    // CryptoAPI PRNG: Large request
    CryptGenRandom(hCryptProv, sizeof(Sources.system_prng), (BYTE*)Sources.system_prng);

    // System info
    GetSystemInfo(&Sources.sys_info);

    // Windows version
    Sources.win_ver = GetVersion();

    // Cycles at the end
    Sources.cycles_end = Clock::cycles();

	pool.Crunch(&Sources, sizeof(Sources));
}

void FortunaFactory::PollSlowEntropySources(int pool_index)
{
	// Not used
}

void FortunaFactory::PollFastEntropySources(int pool_index)
{
	// Not used
}

#endif
