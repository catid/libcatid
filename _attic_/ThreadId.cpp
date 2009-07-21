#include <cat/threads/ThreadId.hpp>

#if defined(CAT_OS_WINDOWS)

#include <windows.h>

namespace cat {

	u32 GetThreadId()
	{
		return GetCurrentThreadId();
	}

} // namespace cat

#endif
