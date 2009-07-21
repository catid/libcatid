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

// 06/11/09 part of libcat-1.0

#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <cat/threads/LocklessFIFO.hpp>

namespace cat {


//// Enumerations

enum EventSeverity
{
    LVL_INANE,
    LVL_INFO,
    LVL_WARN,
    LVL_OOPS,
    LVL_FATAL,

    LVL_SILENT,    // invalid for an actual event's level, valid value for a threshold
};


//// Utility

region_string HexDumpString(const void *vdata, u32 bytes);


//// LogEvent

struct LogEvent
{
    LogEvent();
    LogEvent(const char *subsystem, EventSeverity severity);

public:
    EventSeverity severity;
    const char *subsystem;
    region_ostringstream msg;
};


//// Logging

typedef void (*LogCallback)(const char *severity, const char *source, const char *msg);

class Logging : public Singleton<Logging>
{
    CAT_SINGLETON(Logging);

    static unsigned int WINAPI EventProcessorThread(void *param);
    FIFO::Queue<LogEvent> queue;
    HANDLE hThread;
    LogCallback callback;

    friend class Recorder;
    void QueueEvent(LogEvent *logEvent);
    void EventDequeueProcessor();

public:
    int log_threshold;

public:
    void Initialize();
    void Shutdown();

    void SetLogCallback(LogCallback cb) { callback = cb; }
};


//// Recorder

class Recorder
{
    LogEvent *logEvent;

public:
    Recorder(const char *subsystem, EventSeverity severity);
    ~Recorder();

public:
    template<class T> inline Recorder &operator<<(const T &t)
    {
        logEvent->msg << t;
        return *this;
    }
};

// Because there is an IF statement in the macro, you cannot use the
// braceless if-else construction:
//  if (XYZ) WARN("SS") << "ERROR!"; else INFO("SS") << "OK!";       <-- bad
// Instead use:
//  if (XYZ) { WARN("SS") << "ERROR!"; } else INFO("SS") << "OK!";   <-- good
#define RECORD(subsystem, severity) \
    if (severity >= Logging::ii->log_threshold) Recorder(subsystem, severity)

#define INANE(subsystem)    RECORD(subsystem, LVL_INANE)
#define INFO(subsystem)        RECORD(subsystem, LVL_INFO)
#define WARN(subsystem)        RECORD(subsystem, LVL_WARN)
#define OOPS(subsystem)        RECORD(subsystem, LVL_OOPS)
#define FATAL(subsystem)    RECORD(subsystem, LVL_FATAL)


//// Enforcer

class Enforcer
{
protected:
    std::ostringstream oss;

public:
    Enforcer(const char *locus);
    ~Enforcer();

public:
    template<class T> inline Enforcer &operator<<(const T &t)
    {
        oss << t;
        return *this;
    }
};


#define USE_ENFORCE_EXPRESSION_STRING
#define USE_ENFORCE_FILE_LINE_STRING


#if defined(USE_ENFORCE_EXPRESSION_STRING)
# define ENFORCE_EXPRESSION_STRING(exp) "Failed assertion (" #exp ")"
#else
# define ENFORCE_EXPRESSION_STRING(exp) "Failed assertion"
#endif

#if defined(USE_ENFORCE_FILE_LINE_STRING)
# define ENFORCE_FILE_LINE_STRING " at " __FILE__ ":" CAT_STRINGIZE(__LINE__)
#else
# define ENFORCE_FILE_LINE_STRING ""
#endif

// Because there is an IF statement in the macro, you cannot use the
// braceless if-else construction:
//  if (XYZ) ENFORCE(A == B) << "ERROR!"; else INFO("SS") << "OK!";       <-- bad
// Instead use:
//  if (XYZ) { ENFORCE(A == B) << "ERROR!"; } else INFO("SS") << "OK!";   <-- good
#define ENFORCE(exp) if ( (exp) == 0 ) Enforcer(ENFORCE_EXPRESSION_STRING(exp) ENFORCE_FILE_LINE_STRING "\n")
#define EXCEPTION() Enforcer("Exception" ENFORCE_FILE_LINE_STRING "\n")

#if defined(DEBUG)
# define TESTCASE(exp) ENFORCE(exp)
#else
# define TESTCASE(exp) if (0) ENFORCE(exp) /* hopefully will be optimized out of existence */
#endif


} // namespace cat

#endif // LOGGING_HPP
