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

#if defined(CAT_OS_WINDOWS)

/*
    Windows entropy sources inspired by the Fortuna implementation from
    http://www.citadelsoftware.ca/fortuna/Fortuna.htm
*/

#include <cat/math/BitMath.hpp>
#include <cat/time/Clock.hpp>

// Include and link to various Windows libraries needed to collect system info
#include <Psapi.h>
#include <Lmcons.h>   // LAN-MAN constants for "UNLEN" username max length
#include <Iphlpapi.h> // GetAdaptersInfo()
#include <Rpc.h>      // RPC header for UuidCreate()
#include <Process.h>  // _beginthreadex()

#pragma comment(lib, "rpcrt4")
#pragma comment(lib, "iphlpapi")
#pragma comment(lib, "psapi")
#pragma comment(lib, "advapi32")


void FortunaFactory::EntropyCollectionThread()
{
    // Assume ~16 bits of entropy per fast poll, so it takes 16 fast polls to get 256 bits of entropy
    // This means there will be 4 slow polls in pool 0 for each reseed, which is 256 bits from CryptoAPI
    const int POOL0_RESEED_RATE = 16;

    // Milliseconds between fast polls
    // Indicates 51.2 seconds between reseeds
    const int COLLECTION_PERIOD = 100;

    int fast_pool = 0, slow_pool = 0, pool0_entropy = 0;

    // Loop while the wait is timing out; will break on error or signalled termination
    while (WaitForSingleObject(EntropySignal, COLLECTION_PERIOD) == WAIT_TIMEOUT)
    {
        // Poll fast entropy sources once every half-second
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

unsigned int __stdcall FortunaFactory::EntropyCollectionThreadWrapper(void *vfactory)
{
    FortunaFactory *factory = (FortunaFactory *)vfactory;

    factory->EntropyCollectionThread();

    // Using _beginthreadex() and _endthreadex() since _endthread() calls CloseHandle()
    _endthreadex(0);
    return 0;
}


bool FortunaFactory::InitializeEntropySources()
{
    EntropySignal = 0;
    EntropyThread = 0;

    // Initialize a session with the CryptoAPI using newer cryptoprimitives (AES)
    CurrentProcess = GetCurrentProcess();
    if (!CryptAcquireContext(&hCryptProv, 0, 0, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;

    // Fire poll for entropy all goes into pool 0
    PollInvariantSources(0);
    PollSlowEntropySources(0);
    PollFastEntropySources(0);

    // Create an event to signal when the entropy collection thread should terminate
    EntropySignal = CreateEvent(0, FALSE, FALSE, 0);
    if (!EntropySignal) return false;

    // Using _beginthreadex() and _endthreadex() since _endthread() calls CloseHandle()
    EntropyThread = (HANDLE)_beginthreadex(0, 0, EntropyCollectionThreadWrapper, this, 0, 0);
    if (!EntropyThread) return false;

    return true;
}

void FortunaFactory::ShutdownEntropySources()
{
    if (EntropyThread && EntropySignal)
    {
        // Signal termination event and block waiting for thread to signal termination
        SetEvent(EntropySignal);
        WaitForSingleObject(EntropyThread, INFINITE);
    }

    if (EntropySignal) CloseHandle(EntropySignal);
    if (EntropyThread) CloseHandle(EntropyThread);
    if (hCryptProv) CryptReleaseContext(hCryptProv, 0);
}

void FortunaFactory::PollInvariantSources(int pool_index)
{
    Skein &pool = Pool[pool_index];

    // Cycles at the start
    u32 cycles_start = Clock::cycles();
    pool.Crunch(&cycles_start, sizeof(cycles_start));

    // Operating System PRNG: Large request
    u8 system_prng[32];
    if (CryptGenRandom(hCryptProv, sizeof(system_prng), (BYTE*)system_prng))
        pool.Crunch(system_prng, 32);

    // System info
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    pool.Crunch(&sys_info, sizeof(sys_info));

    // NetBIOS name
    TCHAR computer_name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD name_len = sizeof(computer_name) / sizeof(TCHAR);
    if (GetComputerName(computer_name, &name_len))
        pool.Crunch(computer_name, name_len);

    // User name
    TCHAR user_name[UNLEN + 1];
    DWORD user_len = sizeof(user_name) / sizeof(TCHAR);
    if (GetComputerName(user_name, &user_len))
        pool.Crunch(user_name, user_len);

    // Hardware profile
    HW_PROFILE_INFO hw_profile;
    if (GetCurrentHwProfile(&hw_profile))
        pool.Crunch(&hw_profile, sizeof(hw_profile));

    // Windows version
    DWORD win_ver = GetVersion();
    pool.Crunch(&win_ver, sizeof(win_ver));

    // Registry quota
    DWORD reg_quota[2];
    if (GetSystemRegistryQuota(&reg_quota[0], &reg_quota[1]))
        pool.Crunch(&reg_quota, sizeof(reg_quota));

    // Create a UUID
    UUID uuid;
    UuidCreate(&uuid);
    pool.Crunch(&uuid, sizeof(uuid));

    // Network adapter info
    IP_ADAPTER_INFO adapter_info[16];
    DWORD adapter_len = sizeof(adapter_info);
    if (ERROR_SUCCESS == GetAdaptersInfo(adapter_info, &adapter_len))
        for (IP_ADAPTER_INFO *adapter = adapter_info; adapter; adapter = adapter->Next)
            pool.Crunch(adapter, sizeof(*adapter));

    // Startup info
    STARTUPINFO startup_info;
    GetStartupInfo(&startup_info);
    pool.Crunch(&startup_info, sizeof(startup_info));

    // Global memory status
    MEMORYSTATUS mem_status;
    GlobalMemoryStatus(&mem_status);
    pool.Crunch(&mem_status, sizeof(mem_status));

    // Current process handle
    pool.Crunch(&CurrentProcess, sizeof(CurrentProcess));

    // Cycles at the end
    u32 cycles_end = Clock::cycles();
    pool.Crunch(&cycles_end, sizeof(cycles_end));
}

void FortunaFactory::PollSlowEntropySources(int pool_index)
{
    Skein &pool = Pool[pool_index];

    // Cycles at the start
    u32 cycles_start = Clock::cycles();
    pool.Crunch(&cycles_start, sizeof(cycles_start));

    // Cursor position
    POINT cursor_pos;
    GetCursorPos(&cursor_pos);
    pool.Crunch(&cursor_pos, sizeof(cursor_pos));

    // CryptoAPI PRNG: Small request
    u8 system_prng[8];
    if (CryptGenRandom(hCryptProv, sizeof(system_prng), (BYTE*)system_prng))
        pool.Crunch(system_prng, 8);

    // Poll time in microseconds
    double this_request = Clock::usec();
    pool.Crunch(&this_request, sizeof(this_request));

    // Time since last poll in microseconds
    static double last_request = 0;
    double request_diff = this_request - last_request;
    pool.Crunch(&request_diff, sizeof(request_diff));
    last_request = this_request;

    // IO Counters
    IO_COUNTERS io_counters;
    if (GetProcessIoCounters(CurrentProcess, &io_counters))
        pool.Crunch(&io_counters, sizeof(io_counters));

    // Process times
    FILETIME ft_creation, ft_exit, ft_kernel, ft_user;
    if (GetProcessTimes(CurrentProcess, &ft_creation, &ft_exit, &ft_kernel, &ft_user))
    {
        pool.Crunch(&ft_creation, sizeof(ft_creation));
        pool.Crunch(&ft_exit, sizeof(ft_exit));
        pool.Crunch(&ft_kernel, sizeof(ft_kernel));
        pool.Crunch(&ft_user, sizeof(ft_user));
    }

    // Process memory info
    PROCESS_MEMORY_COUNTERS_EX mem_counters;
    if (GetProcessMemoryInfo(CurrentProcess, (PPROCESS_MEMORY_COUNTERS)&mem_counters, sizeof(mem_counters)))
        pool.Crunch(&mem_counters, sizeof(mem_counters));

    // Performance info
    PERFORMANCE_INFORMATION perf_info;
    if (GetPerformanceInfo(&perf_info, sizeof(perf_info)))
        pool.Crunch(&perf_info, sizeof(perf_info));

    // Cycles at the end
    u32 cycles_end = Clock::cycles();
    pool.Crunch(&cycles_end, sizeof(cycles_end));
}

void FortunaFactory::PollFastEntropySources(int pool_index)
{
    Skein &pool = Pool[pool_index];

    // Cycles at the start
    u32 cycles_start = Clock::cycles();
    pool.Crunch(&cycles_start, sizeof(cycles_start));

    // Poll time in microseconds
    double this_request = Clock::usec();
    pool.Crunch(&this_request, sizeof(this_request));

    // Time since last poll in microseconds
    static double last_request = 0;
    double request_diff = this_request - last_request;
    pool.Crunch(&request_diff, sizeof(request_diff));
    last_request = this_request;

    // Cycles at the end
    u32 cycles_end = Clock::cycles();
    pool.Crunch(&cycles_end, sizeof(cycles_end));
}

#endif
