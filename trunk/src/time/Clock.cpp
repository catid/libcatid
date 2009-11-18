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

#include <cat/time/Clock.hpp>

#if defined(CAT_OS_LINUX)
# include <sys/time.h>
# include <stdlib.h> // qsort
#elif defined(CAT_OS_WINDOWS)
# include <windows.h>
# include <mmsystem.h>
# pragma comment (lib, "winmm")
#endif

#include <ctime>
using namespace std;
using namespace cat;


Clock::Clock()
{
#if defined(CAT_OS_WINDOWS)
    // Set minimum timer resolution as low as possible
    for (period = 1; period < 20 && (timeBeginPeriod(period) != TIMERR_NOERROR); ++period);
#endif
}

Clock::~Clock()
{
#if defined(CAT_OS_WINDOWS)
    if (period < 20) timeEndPeriod(period);
#endif
}


u32 Clock::sec()
{
#if defined(CAT_OS_LINUX)

    struct timeval cateq_v;
    struct timezone cateq_z;

    gettimeofday(&cateq_v, &cateq_z);

    return static_cast<u32>(cateq_v.tv_sec);

#elif defined(CAT_OS_WINDOWS)

    return timeGetTime() / 1000;

#endif
}

u32 Clock::hsec()
{
#if defined(CAT_OS_LINUX)

    struct timeval cateq_v;
    struct timezone cateq_z;

    gettimeofday(&cateq_v, &cateq_z);

    return static_cast<u32>(100.0 * static_cast<double>(cateq_v.tv_sec) + static_cast<double>(cateq_v.tv_usec) / 10000.0);

#elif defined(CAT_OS_WINDOWS)

    return timeGetTime() / 10;

#endif
}

u32 Clock::msec()
{
#if defined(CAT_OS_LINUX)

    struct timeval cateq_v;
    struct timezone cateq_z;

    gettimeofday(&cateq_v, &cateq_z);

    return static_cast<u32>(1000.0 * static_cast<double>(cateq_v.tv_sec) + static_cast<double>(cateq_v.tv_usec) / 1000.0);

#elif defined(CAT_OS_WINDOWS)

    return timeGetTime();

#endif
}

double Clock::usec()
{
#if defined(CAT_OS_LINUX)

    struct timeval cateq_v;
    struct timezone cateq_z;

    gettimeofday(&cateq_v, &cateq_z);

    return 1000000.0 * static_cast<double>(cateq_v.tv_sec) + static_cast<double>(cateq_v.tv_usec);

#elif defined(CAT_OS_WINDOWS)

    /* In Windows, this value can leap forward randomly:
     * http://support.microsoft.com/default.aspx?scid=KB;EN-US;Q274323
     * So it may be best to do most of the timing in milliseconds.
     */

    LARGE_INTEGER tim, freq; // 64-bit! ieee
    double usec;

    QueryPerformanceCounter(&tim);
    QueryPerformanceFrequency(&freq);
    usec = (static_cast<double>(tim.QuadPart) * 1000000.0) / static_cast<double>(freq.QuadPart);

    return usec;

#endif
}


void Clock::sleep(u32 milliseconds)
{
#if defined(CAT_OS_LINUX)
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = milliseconds * 1000000 - ts.tv_sec * 1000;
    while (nanosleep(&ts, &ts) == -1);

#elif defined(CAT_OS_WINDOWS)
    Sleep(milliseconds);

#endif
}


std::string Clock::format(const char *format_string)
{
    char ts[256];

    struct tm *pLocalTime;

#if defined(CAT_OS_WINDOWS)
# if defined(CAT_COMPILER_MSVC)
    struct tm localTime;
    __time64_t long_time;
    _time64(&long_time);
    _localtime64_s(&localTime, &long_time);
    pLocalTime = &localTime;
# else
    // MinGW doesn't support 64-bit stuff very well yet...
    time_t long_time;
    time(&long_time);
    pLocalTime = localtime(&long_time);
# endif
#else
    struct tm localTime;
    time_t long_time;
    localtime_r(&long_time, &localTime);
    pLocalTime = &localTime;
#endif

    strftime(ts, sizeof(ts), format_string, pLocalTime);

    return ts;
}


// Algorithm from Skein test app
u32 Clock::cycles()
{
    u32 x[2];

#if defined(CAT_COMPILER_MSVC) && defined(CAT_WORD_64)
	x[0] = (u32)__rdtsc();

#elif defined(CAT_ASM_INTEL) && defined(CAT_ISA_X86)
	CAT_ASM_BEGIN
		push edx
		CAT_ASM_EMIT 0x0F
		CAT_ASM_EMIT 0x31
		pop edx
		mov x[0], eax
	CAT_ASM_END

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_X86)
	CAT_ASM_BEGIN
		"rdtsc" : "=a"(x[0]), "=d"(x[1])
	CAT_ASM_END

#elif defined(CAT_ISA_PPC) && defined(CAT_ASM_ATT)
	// Based on code from Kazutomo Yoshii ( http://www.mcs.anl.gov/~kazutomo/rdtsc.html )
	u32 tmp;

	CAT_ASM_BEGIN
		"0:                  \n"
		"\tmftbu   %0        \n"
		"\tmftb    %1        \n"
		"\tmftbu   %2        \n"
		"\tcmpw    %2,%0     \n"
		"\tbne     0b        \n"
		: "=r"(x[1]),"=r"(x[0]),"=r"(tmp)
	CAT_ASM_END

#else

# error "Please add your compiler here"

#endif

    return x[0];
}

// Algorithm from Skein test app
bool Clock::SetHighPriority()
{
#if defined(CAT_OS_WINDOWS)
    return 0 != SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
    return false;
#endif
}

// Algorithm from Skein test app
bool Clock::SetNormalPriority()
{
#if defined(CAT_OS_WINDOWS)
    return 0 != SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#else
    return false;
#endif
}

static int compare_u32(const void *aPtr,const void *bPtr)
{
    u32 a = *(u32*)aPtr;
    u32 b = *(u32*)bPtr;

    if (a > b) return  1;
    if (a < b) return -1;
    return 0;
}

// Algorithm from Skein test app
u32 Clock::MeasureClocks(int iterations, void (*FunctionPtr)())
{
    u32 *timings = new u32[iterations];

    Clock::SetHighPriority();
    Clock::sleep(200);

    u32 dtMin = ~0;

    for (int ii = 0; ii < 10; ++ii)
    {
        u32 a = Clock::cycles();
        u32 b = Clock::cycles();

        b -= a;

        if (dtMin > b)
            dtMin = b;
    }

    Clock::sleep(200);

    u32 d0 = Clock::cycles();
    u32 d1 = Clock::cycles();

    Clock::sleep(200);

    for (int jj = 0; jj < iterations; ++jj)
    {
        FunctionPtr();

        u32 a = Clock::cycles();
        FunctionPtr();
        FunctionPtr();
        u32 b = Clock::cycles();

        u32 dt = ((b - a) - dtMin) / 2;

        timings[jj] = dt;
    }

    qsort(timings, iterations, sizeof(u32), compare_u32);

    Clock::SetNormalPriority();

    u32 median = timings[iterations/2];

    delete []timings;

    return median;
}
