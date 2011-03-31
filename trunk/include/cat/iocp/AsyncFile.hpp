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

#ifndef CAT_IOCP_ASYNCFILE_HPP
#define CAT_IOCP_ASYNCFILE_HPP

#include <cat/threads/RefObject.hpp>

namespace cat {


enum AsyncFileModes
{
	ASYNCFILE_READ = 1,
	ASYNCFILE_WRITE = 2,
	ASYNCFILE_RANDOM = 4
};


class CAT_EXPORT AsyncFile : public WatchedRefObject
{
	friend class IOThread;

	IOLayer *_iolayer;
	HANDLE _file;

	void OnReadCompletion(const BatchSet &buffers, u32 count);

public:
    AsyncFile();
    virtual ~AsyncFile();

	CAT_INLINE bool Valid() { return _file != INVALID_HANDLE_VALUE; }

	/*
		In read mode, Open() will fail if the file does not exist.
		In write mode, Open() will create the file if it does not exist.
	*/
	bool Open(const char *file_path, u32 async_file_modes);
	void Close();

	bool SetSize(u64 bytes);
	u64 GetSize();

	bool Read(ReadBuffer *buffer, u64 offset);
	bool Write(WriteBuffer *buffer, u64 offset);

protected:
	CAT_INLINE IOLayer *GetIOLayer() { return _iolayer; }

	virtual void OnShutdownRequest();
	virtual bool OnZeroReferences();
};


} // namespace cat

#endif // CAT_IOCP_ASYNCFILE_HPP
