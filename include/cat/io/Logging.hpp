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

    LVL_SILENT, // invalid for an actual event's level, valid value for a threshold
};


//// Utility

region_string HexDumpString(const void *vdata, u32 bytes);

void FatalStop(const char *message);


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
	void Initialize(EventSeverity min_severity);
	void ReadSettings();
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
#define INFO(subsystem)     RECORD(subsystem, LVL_INFO)
#define WARN(subsystem)     RECORD(subsystem, LVL_WARN)
#define OOPS(subsystem)     RECORD(subsystem, LVL_OOPS)
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
//  if (XYZ) ENFORCE(A == B) << "ERROR"; else INFO("SS") << "OK";       <-- bad!
// Instead use:
//  if (XYZ) { ENFORCE(A == B) << "ERROR"; } else INFO("SS") << "OK";   <-- good!

#define ENFORCE(exp) if ( (exp) == 0 ) Enforcer(ENFORCE_EXPRESSION_STRING(exp) ENFORCE_FILE_LINE_STRING "\n")
#define EXCEPTION() Enforcer("Exception" ENFORCE_FILE_LINE_STRING "\n")

#if defined(CAT_DEBUG)
# define DEBUG_ENFORCE(exp) ENFORCE(exp)
#else
# define DEBUG_ENFORCE(exp) if (0) ENFORCE(exp) /* hopefully will be optimized out of existence */
#endif


} // namespace cat

#endif // LOGGING_HPP
