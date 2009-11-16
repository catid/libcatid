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

static void OutputConsoleDebug(LogEvent *logEvent)
{
    region_ostringstream oss;
    oss << "[" << Clock::format("%b %d %H:%M") << "] <" << logEvent->subsystem << "> "
        << logEvent->msg.str().c_str() << endl;

	cout << oss.str();
    OutputDebugStringA(oss.str().c_str());
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

void Logging::Initialize()
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

    if (!logEvent)
	{
		// Out of memory
		CAT_ARTIFICIAL_BREAKPOINT;
	}
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
