/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#include <cat/time/Clock.hpp>

#if defined(CAT_OS_WINDOWS)

# include <cat/port/WindowsInclude.hpp>

# if defined(CAT_COMPILER_MSVC)
#  pragma warning(push)
#  pragma warning(disable: 4201) // Squelch annoying warning from MSVC2005 SDK
#  include <mmsystem.h>
#  pragma warning(pop)
#  pragma comment (lib, "winmm")
# else
#  include <mmsystem.h>
# endif

#else // Linux/other version

# include <sys/time.h>

#endif

#include <stdlib.h> // qsort
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
#if defined(CAT_OS_WINDOWS)

    return GetTickCount() / 1000;

#else

	struct timeval cateq_v;
	struct timezone cateq_z;

	gettimeofday(&cateq_v, &cateq_z);

	return static_cast<u32>(cateq_v.tv_sec);

#endif
}

u32 Clock::hsec()
{
#if defined(CAT_OS_WINDOWS)

	return GetTickCount() / 10;

#else

	struct timeval cateq_v;
	struct timezone cateq_z;

	gettimeofday(&cateq_v, &cateq_z);

	return static_cast<u32>(100.0 * static_cast<double>(cateq_v.tv_sec) + static_cast<double>(cateq_v.tv_usec) / 10000.0);

#endif
}

u32 Clock::msec()
{
#if defined(CAT_OS_WINDOWS)

	return timeGetTime();

#else

    struct timeval cateq_v;
    struct timezone cateq_z;

    gettimeofday(&cateq_v, &cateq_z);

    return static_cast<u32>(1000.0 * static_cast<double>(cateq_v.tv_sec) + static_cast<double>(cateq_v.tv_usec) / 1000.0);

#endif
}

double Clock::usec()
{
#if defined(CAT_OS_WINDOWS)

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

#else

    struct timeval cateq_v;
    struct timezone cateq_z;

    gettimeofday(&cateq_v, &cateq_z);

    return 1000000.0 * static_cast<double>(cateq_v.tv_sec) + static_cast<double>(cateq_v.tv_usec);

#endif
}


void Clock::sleep(u32 milliseconds)
{
#if defined(CAT_OS_WINDOWS)

	Sleep(milliseconds);

#else

	struct timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = milliseconds * 1000000 - ts.tv_sec * 1000;
	while (nanosleep(&ts, &ts) == -1);

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

#elif defined(CAT_ASM_ATT) && defined(CAT_ISA_PPC)
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

# if defined(CAT_OS_WINDOWS)
	LARGE_INTEGER tim;
	QueryPerformanceCounter(&tim);
	x[0] = tim.LowPart;
# else
	struct timeval cateq_v;
	struct timezone cateq_z;
	gettimeofday(&cateq_v, &cateq_z);
	x[0] = (u32)cateq_v.tv_usec;
# endif

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

    u32 dtMin = ~(u32)0;

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
	d1 ^= d0; // prevent compiler warning

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
