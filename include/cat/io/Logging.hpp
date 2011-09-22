/*
	Copyright (c) 2009-2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_LOGGING_HPP
#define CAT_LOGGING_HPP

#include <cat/lang/RefObject.hpp>
#include <cat/lang/Delegates.hpp>
#include <cat/lang/Singleton.hpp>
#include <string>
#include <sstream>

#if defined(CAT_OS_WINDOWS)
#include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


class Logging;
class Recorder;
class Enforcer;


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

#if defined(CAT_DEBUG)
	static const int DEFAULT_LOG_LEVEL = LVL_INANE;
#else
	static const int DEFAULT_LOG_LEVEL = LVL_INFO;
#endif



//// Utility

std::string CAT_EXPORT HexDumpString(const void *vdata, u32 bytes);

// Write to console (and debug log in windows) then trigger a breakpoint and exit
void CAT_EXPORT FatalStop(const char *message);

void CAT_EXPORT DefaultLogCallback(EventSeverity severity, const char *source, std::ostringstream &msg);


//// Logging

class CAT_EXPORT Logging : public Singleton<Logging>
{
	friend class Recorder;

	bool OnInitialize();

public:
	typedef Delegate3<void, EventSeverity, const char *, std::ostringstream &> Callback;

private:
	Mutex _lock;
	Callback _callback;

	bool _service;
#if defined(CAT_OS_WINDOWS)
	HANDLE _event_source;
#endif

	int _log_threshold;

	void LogEvent(Recorder *recorder);

public:
	CAT_INLINE void SetThreshold(EventSeverity min_severity) { _log_threshold = min_severity; }
	CAT_INLINE int GetThreshold() { return _log_threshold; }

	// Service mode
	CAT_INLINE bool IsService() { return _service; }
	void EnableServiceMode(const char *service_name);
	void WriteServiceLog(EventSeverity severity, const char *line);

	void SetLogCallback(const Callback &cb);
};


//// Recorder

class CAT_EXPORT Recorder
{
	CAT_NO_COPY(Recorder);

	friend class Logging;

	EventSeverity _severity;
	const char *_subsystem;
	std::ostringstream _msg;

public:
	Recorder(const char *subsystem, EventSeverity severity);
	~Recorder();

public:
	template<class T> inline Recorder &operator<<(const T &t)
	{
		_msg << t;
		return *this;
	}
};

// Because there is an IF statement in the macro, you cannot use the
// braceless if-else construction:
//  if (XYZ) WARN("SS") << "ERROR!"; else INFO("SS") << "OK!";	   <-- bad
// Instead use:
//  if (XYZ) { WARN("SS") << "ERROR!"; } else INFO("SS") << "OK!";   <-- good
#define CAT_RECORD(subsystem, severity) \
	if (severity >= Logging::ref()->GetThreshold()) Recorder(subsystem, severity)

#define CAT_INANE(subsystem)	CAT_RECORD(subsystem, cat::LVL_INANE)
#define CAT_INFO(subsystem)		CAT_RECORD(subsystem, cat::LVL_INFO)
#define CAT_WARN(subsystem)		CAT_RECORD(subsystem, cat::LVL_WARN)
#define CAT_OOPS(subsystem)		CAT_RECORD(subsystem, cat::LVL_OOPS)
#define CAT_FATAL(subsystem)	CAT_RECORD(subsystem, cat::LVL_FATAL)


//// Enforcer

class CAT_EXPORT Enforcer
{
	CAT_NO_COPY(Enforcer);

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


#define CAT_USE_ENFORCE_EXPRESSION_STRING
#define CAT_USE_ENFORCE_FILE_LINE_STRING


#if defined(CAT_USE_ENFORCE_EXPRESSION_STRING)
# define CAT_ENFORCE_EXPRESSION_STRING(exp) "Failed assertion (" #exp ")"
#else
# define CAT_ENFORCE_EXPRESSION_STRING(exp) "Failed assertion"
#endif

#if defined(CAT_USE_ENFORCE_FILE_LINE_STRING)
# define CAT_ENFORCE_FILE_LINE_STRING " at " CAT_FILE_LINE_STRING
#else
# define CAT_ENFORCE_FILE_LINE_STRING ""
#endif

// Because there is an IF statement in the macro, you cannot use the
// braceless if-else construction:
//  if (XYZ) ENFORCE(A == B) << "ERROR"; else INFO("SS") << "OK";	   <-- bad!
// Instead use:
//  if (XYZ) { ENFORCE(A == B) << "ERROR"; } else INFO("SS") << "OK";   <-- good!

#define CAT_ENFORCE(exp) if ( (exp) == 0 ) Enforcer(CAT_ENFORCE_EXPRESSION_STRING(exp) CAT_ENFORCE_FILE_LINE_STRING "\n")
#define CAT_EXCEPTION() Enforcer("Exception" CAT_ENFORCE_FILE_LINE_STRING "\n")

#if defined(CAT_DEBUG)
# define CAT_DEBUG_ENFORCE(exp) CAT_ENFORCE(exp)
#else
# define CAT_DEBUG_ENFORCE(exp) while (false) CAT_ENFORCE(exp) /* hopefully will be optimized out of existence */
#endif


} // namespace cat

#endif // CAT_LOGGING_HPP
