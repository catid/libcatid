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

#include <cat/io/Logging.hpp>
#include <cat/io/Settings.hpp>
#include <cat/time/Clock.hpp>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <process.h>
using namespace std;
using namespace cat;


#if defined(CAT_ISA_X86)

#if defined(CAT_WORD_64) && defined(CAT_COMPILER_MSVC)
# define CAT_ARTIFICIAL_BREAKPOINT { ::DebugBreak(); }
#elif defined(CAT_ASM_INTEL)
# define CAT_ARTIFICIAL_BREAKPOINT { CAT_ASM_BEGIN int 3 CAT_ASM_END }
#elif defined(CAT_ASM_ATT)
# define CAT_ARTIFICIAL_BREAKPOINT { CAT_ASM_BEGIN "int $3" CAT_ASM_END }
#endif

#elif defined(CAT_ISA_PPC)
# error "FIXME: Need to figure out how to do this"
#elif defined(CAT_ISA_ARM)
# error "FIXME: Need to figure out how to do this"
#else
# error "FIXME: Need to figure out how to do this"
#endif


static const char *const EVENT_NAME[5] = { "Inane", "Info", "Warn", "Oops", "Fatal" };
//static const char *const SHORT_EVENT_NAME[5] = { ".", "I", "W", "!", "F" };


//// Free functions

region_string HexDumpString(const void *vdata, u32 bytes)
{
    /* xxxx  xx xx xx xx xx xx xx xx  xx xx xx xx xx xx xx xx   aaaaaaaaaaaaaaaa*/

    const u8 *data = (const u8*)vdata;
    u32 ii, offset;

    char ascii[17];
    ascii[16] = 0;

    region_ostringstream oss;

    for (offset = 0; offset < bytes; offset += 16)
    {
        oss << endl << setfill('0') << hex << setw(4) << offset << "  ";

        for (ii = 0; ii < 16; ++ii)
        {
            if (ii == 8)
                oss << ' ';

            if (offset + ii < bytes)
            {
                u8 ch = data[offset + ii];

                oss << setw(2) << (u32)ch << ' ';
                ascii[ii] = (ch >= ' ' && ch <= '~') ? ch : '.';
            }
            else
            {
                oss << "   ";
                ascii[ii] = 0;
            }
        }

        oss << " " << ascii;
    }

    return oss.str();
}

void FatalStop(const char *message)
{
	cout << "Fatal Stop: " << message << endl;
#if defined(CAT_OS_WINDOWS)
	OutputDebugStringA(message);
#endif

	CAT_ARTIFICIAL_BREAKPOINT;
}

static void OutputConsoleDebug(LogEvent *logEvent)
{
    region_ostringstream oss;
    oss << "[" << Clock::format("%b %d %H:%M") << "] <" << logEvent->subsystem << "> "
        << logEvent->msg.str().c_str() << endl;

	cout << oss.str();
#if defined(CAT_OS_WINDOWS)
    OutputDebugStringA(oss.str().c_str());
#endif
}


//// LogEvent

LogEvent::LogEvent()
{
    subsystem = 0;
    severity = LVL_FATAL;
}

LogEvent::LogEvent(const char *subsystem, EventSeverity severity)
{
    this->subsystem = subsystem;
    this->severity = severity;
}


//// Logging

unsigned int WINAPI Logging::EventProcessorThread(void *param)
{
    ( (Logging*)param )->EventDequeueProcessor();
    return 0;
}

Logging::Logging()
{
    callback = 0;
    log_threshold = LVL_INANE;

    hThread = (HANDLE)_beginthreadex(0, 0, EventProcessorThread, this, 0, 0);
}

__declspec(dllexport) void SetLogCallback(LogCallback cb)
{
    Logging::ref()->SetLogCallback(cb);
}

void Logging::Initialize(EventSeverity min_severity)
{
    log_threshold = min_severity;
}

void Logging::ReadSettings()
{
    log_threshold = Settings::ii->getInt("Log.Threshold", LVL_INANE);
}

void Logging::Shutdown()
{
    if (hThread != (HANDLE)-1)
    {
        queue.Enqueue(new (RegionAllocator::ii) LogEvent);

        WaitForSingleObject(hThread, INFINITE);

        CloseHandle(hThread);

        // Dequeue and process late messages
        LogEvent *logEvent;
        while ((logEvent = queue.Dequeue()) && logEvent->subsystem)
            OutputConsoleDebug(logEvent);
    }
}

void Logging::QueueEvent(LogEvent *logEvent)
{
    queue.Enqueue(logEvent);
}

void Logging::EventDequeueProcessor()
{
    LogEvent *logEvent;

    while ((logEvent = queue.DequeueWait()) && logEvent->subsystem)
    {
        if (callback)
            callback(EVENT_NAME[logEvent->severity], logEvent->subsystem, logEvent->msg.str().c_str());
        else
            OutputConsoleDebug(logEvent);
    }
}


//// Recorder

Recorder::Recorder(const char *subsystem, EventSeverity severity)
{
    logEvent = new (RegionAllocator::ii) LogEvent(subsystem, severity);

    if (!logEvent) FatalStop("Out of memory in logging subsystem");
}

Recorder::~Recorder()
{
    Logging::ii->QueueEvent(logEvent);
}


//// Enforcer

Enforcer::Enforcer(const char *locus)
{
    oss << locus;
}

Enforcer::~Enforcer()
{
    cerr << oss.str();
    OutputDebugStringA(oss.str().c_str());
	cerr.flush();

	CAT_ARTIFICIAL_BREAKPOINT;
}
