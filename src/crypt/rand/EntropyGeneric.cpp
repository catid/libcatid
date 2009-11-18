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

#if !defined(CAT_OS_WINDOWS) && !defined(CAT_OS_LINUX) && !defined(CAT_OS_WINDOWS_CE)

#if !defined(CAT_NO_ENTROPY_THREAD)

void FortunaFactory::EntropyCollectionThread()
{
	// Generic version does not spawn a thread
}

#endif // !defined(CAT_NO_ENTROPY_THREAD)


bool FortunaFactory::InitializeEntropySources()
{
    // Fire poll for entropy all goes into pool 0
    PollInvariantSources(0);

    return true;
}

void FortunaFactory::ShutdownEntropySources()
{
}

void FortunaFactory::PollInvariantSources(int pool_index)
{
    Skein &pool = Pool[pool_index];

	struct {
		u32 cycles_start;
	    u8 system_prng[32];
		u32 cycles_end;
	} Sources;

    // Cycles at the start
    Sources.cycles_start = Clock::cycles();

	int urandom_fd = open("/dev/urandom", O_RDONLY);

	// /dev/urandom large request
	if (urandom_fd >= 0)
	{
		read(urandom_fd, Sources.system_prng, sizeof(Sources.system_prng));

		close(urandom_fd);
	}

    // Cycles at the end
    Sources.cycles_end = Clock::cycles();

	pool.Crunch(&Sources, sizeof(Sources));
}

void FortunaFactory::PollSlowEntropySources(int pool_index)
{
}

void FortunaFactory::PollFastEntropySources(int pool_index)
{
}

#endif
