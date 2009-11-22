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

#include <cat/io/ThreadPoolFiles.hpp>
#include <cat/port/AlignedAlloc.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;

AsyncReadFile::AsyncReadFile()
{
	_file = 0;
}

AsyncReadFile::~AsyncReadFile()
{
	if (_file) CloseHandle(_file);
}

bool AsyncReadFile::Open(const char *path)
{
	_file = CreateFile(path, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY | FILE_FLAG_OVERLAPPED, 0);

	if (!_file)
	{
		WARN("AsyncReadFile") << "CreateFile(" << path << ") error: " << GetLastError();
		return false;
	}

    if (!ThreadPool::ref()->Associate(_file, 0))
	{
		WARN("AsyncReadFile") << "ThreadPool::Associate() failed";
		CloseHandle(_file);
		return false;
	}

	return true;
}

bool AsyncReadFile::Read(u32 offset, u32 bytes, ReadFileCallback callback)
{
	DEBUG_ENFORCE(_file != 0) << "Attempt to read from unopened file";

	// Calculate the size of the memory region containing the overlapped structure, keys and buffer
	u32 ovb = sizeof(ReadFileOverlapped) + bytes;

	ReadFileOverlapped *readOv = reinterpret_cast<ReadFileOverlapped *>( Aligned::Acquire(ovb) );
	ENFORCE(readOv != 0) << "Out of memory";

	readOv->Set(OVOP_READFILE_EX);
	readOv->callback = callback;
	readOv->ov.Offset = offset;

	BOOL result = ReadFileEx(_file, GetTrailingBytes(readOv), bytes, &readOv->ov, 0);

	if (!result && GetLastError() != ERROR_IO_PENDING)
	{
        WARN("AsyncReadFile") << "ReadFileEx error: " << GetLastError();
		Aligned::Release(readOv);
		return false;
	}

	return true;
}
