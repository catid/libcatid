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

#if defined(CAT_OS_LINUX)

#include <cat/time/Clock.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if !defined(CAT_NO_ENTROPY_THREAD)

void FortunaFactory::EntropyCollectionThread()
{
    // Assume ~16 bits of entropy per fast poll, so it takes 16 fast polls to get 256 bits of entropy
    // This means there will be 4 slow polls in pool 0 for each reseed, which is 256 bits from /dev/urandom
    const int POOL0_RESEED_RATE = 16;

    // Milliseconds between fast polls
    // Indicates 51.2 seconds between reseeds
    const int COLLECTION_PERIOD = 100;

    int fast_pool = 0, slow_pool = 0, pool0_entropy = 0;

    // Loop while the flag is set
    while (thread_running)
    {
        Clock::sleep(COLLECTION_PERIOD);

        // Poll fast entropy sources once every COLLECTION_PERIOD
        PollFastEntropySources(fast_pool);

        // Poll slow entropy sources 4 times slower
        if ((fast_pool & 3) == 0)
        {
            PollSlowEntropySources(slow_pool);

            // Keep track of entropy in pool 0 and reseed when it is ready
            if (fast_pool == 0 && ++pool0_entropy >= POOL0_RESEED_RATE)
            {
                FortunaFactory::ii->Reseed();
                pool0_entropy = 0;
            }
        }

        slow_pool = (slow_pool + 1) % 32;
        fast_pool = (fast_pool + 1) % 32;
    }
}

void *FortunaFactory::EntropyCollectionThreadWrapper(void *vfactory)
{
    FortunaFactory *factory = (FortunaFactory *)vfactory;

    factory->EntropyCollectionThread();

    return 0;
}

#endif // !defined(CAT_NO_ENTROPY_THREAD)


bool FortunaFactory::InitializeEntropySources()
{
    urandom_fd = open("/dev/urandom", O_RDONLY);

    // Fire poll for entropy all goes into pool 0
    PollInvariantSources(0);
    PollSlowEntropySources(0);
    PollFastEntropySources(0);

#if !defined(CAT_NO_ENTROPY_THREAD)
    thread_running = true;
    if (pthread_create(&pthread_handle, 0, &FortunaFactory::EntropyCollectionThreadWrapper, (void*)this))
    {
        thread_running = false;
        return false;
    }
#endif // !defined(CAT_NO_ENTROPY_THREAD)

    return true;
}

void FortunaFactory::ShutdownEntropySources()
{
#if !defined(CAT_NO_ENTROPY_THREAD)
    if (thread_running)
    {
        thread_running = false;
        pthread_join(pthread_handle, 0);
    }
#endif

    if (urandom_fd >= 0)
        close(urandom_fd);
}

void FortunaFactory::PollInvariantSources(int pool_index)
{
    Skein &pool = Pool[pool_index];

    struct {
        u32 cycles_start;
        u8 system_prng[32];
        u32 pid;
        u32 cycles_end;
    } Sources;

    // Cycles at the start
    Sources.cycles_start = Clock::cycles();

    // /dev/urandom large request
    if (urandom_fd >= 0)
        read(urandom_fd, Sources.system_prng, sizeof(Sources.system_prng));

    // pid
    Sources.pid = (u32)getpid();

    // Cycles at the end
    Sources.cycles_end = Clock::cycles();

    pool.Crunch(&Sources, sizeof(Sources));
}

static void PollVMStat(Skein &pool)
{
    const char *PATH = "vmstat -s";

    fd_set fds;
    FD_ZERO(&fds);

    if (!access(PATH, F_OK)) return;

    FILE *fp = popen(PATH, "r");
    if (!fp) return;

    int fd = fileno(fp);
    if (fd < 0) return;

    FD_SET(fd, &fds);

    timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    int r;
    while ((r = select(1 + fd, &fds, 0, 0, &tv)))
    {
        if (r == -1 || !FD_ISSET(fd, &fds)) break;

        static u8 buffer[4096];
        int count = read(fd, buffer, sizeof(buffer));

        if (count > 0) pool.Crunch(buffer, count);
        else break;
    }

    pclose(fp);
    FD_CLR(fd, &fds);
}

void FortunaFactory::PollSlowEntropySources(int pool_index)
{
    Skein &pool = Pool[pool_index];

    struct {
        u32 cycles_start;
        u8 system_prng[8];
        double this_request;
        double request_diff;
        u32 cycles_end;
    } Sources;

    // Cycles at the start
    Sources.cycles_start = Clock::cycles();

    // /dev/urandom small request
    if (urandom_fd >= 0)
        read(urandom_fd, Sources.system_prng, sizeof(Sources.system_prng));

    // Poll vmstat -s
    PollVMStat(pool);

    // Poll time in microseconds
    Sources.this_request = Clock::usec();

    // Time since last poll in microseconds
    static double last_request = 0;
    Sources.request_diff = Sources.this_request - last_request;
    last_request = Sources.this_request;

    // Cycles at the end
    Sources.cycles_end = Clock::cycles();

    pool.Crunch(&Sources, sizeof(Sources));
}

void FortunaFactory::PollFastEntropySources(int pool_index)
{
    Skein &pool = Pool[pool_index];

    struct {
        u32 cycles_start;
        double this_request;
        double request_diff;
        u32 cycles_end;
    } Sources;

    // Cycles at the start
    Sources.cycles_start = Clock::cycles();

    // Poll time in microseconds
    Sources.this_request = Clock::usec();

    // Time since last poll in microseconds
    static double last_request = 0;
    Sources.request_diff = Sources.this_request - last_request;
    last_request = Sources.this_request;

    // Cycles at the end
    Sources.cycles_end = Clock::cycles();

    pool.Crunch(&Sources, sizeof(Sources));
}

#endif
