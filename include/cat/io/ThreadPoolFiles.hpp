/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_THREAD_POOL_FILES_HPP
#define CAT_THREAD_POOL_FILES_HPP

#include <cat/threads/ThreadPool.hpp>
#include <cat/port/FastDelegate.h>

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


typedef fastdelegate::FastDelegate4<ThreadPoolLocalStorage *, u32, u8 *, u32> ReadFileCallback;

// ReadFileEx() OVERLAPPED object
struct ReadFileOverlapped
{
	TypedOverlapped ov;

	u32 offset;
	ReadFileCallback callback;
};

// ReadFileEx() OVERLAPPED object: Bulk version
struct ReadFileBulkOverlapped
{
	TypedOverlapped ov;

	u32 offset;
	void *buffer;
};

enum AsyncFileModes
{
	ASYNCFILE_READ = 1,
	ASYNCFILE_WRITE = 2,
	ASYNCFILE_RANDOM = 4,
};

class AsyncFile : public ThreadRefObject
{
	friend class ThreadPool;

protected:
	HANDLE _file;
	char _file_path[MAX_PATH+1];

public:
	AsyncFile(int priorityLevel);
	virtual ~AsyncFile();

public:
	bool Valid();
	const char *GetFilePath();

public:
	/*
		In read mode, Open() will fail if the file does not exist.
		In write mode, Open() will create the file if it does not exist.
	*/
	bool Open(const char *file_path, u32 async_file_modes);
	void Close();

	u32 GetSize();

	bool BeginRead(u32 offset, u32 bytes, ReadFileCallback);

	// Buffer must exist until completion
	bool BeginBulkRead(u32 offset, u32 bytes, void *buffer);

	// Buffer passed to BeginWrite() must be retrieved from GetPostBuffer()
	bool BeginWrite(u32 offset, void *buffer, u32 bytes);

protected:
	virtual void OnRead(ThreadPoolLocalStorage *tls, ReadFileOverlapped *readOv, u32 bytes);
	virtual void OnReadBulk(ThreadPoolLocalStorage *tls, ReadFileBulkOverlapped *readOv, u32 bytes) {}
};


} // namespace cat

#endif // CAT_THREAD_POOL_FILES_HPP
