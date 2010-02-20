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

#include <cat/io/ThreadPoolFiles.hpp>
#include <cat/port/AlignedAlloc.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;


AsyncFile::AsyncFile(int priorityLevel)
	: ThreadRefObject(priorityLevel)
{
	_file = 0;
	CAT_OBJCLR(_file_path);
}

AsyncFile::~AsyncFile()
{
	Close();
}

bool AsyncFile::Valid()
{
	return _file != 0;
}

const char *AsyncFile::GetFilePath()
{
	return _file_path;
}

bool AsyncFile::Open(const char *file_path, u32 async_file_modes)
{
	Close();

	CAT_STRNCPY(_file_path, file_path, sizeof(_file_path));

	u32 modes = 0, flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
	u32 creation = OPEN_EXISTING;

	if (async_file_modes & ASYNCFILE_READ)
		modes |= GENERIC_READ;

	if (async_file_modes & ASYNCFILE_WRITE)
	{
		modes |= GENERIC_WRITE;
		creation = OPEN_ALWAYS;
	}

	if (async_file_modes & ASYNCFILE_RANDOM)
		flags |= FILE_FLAG_RANDOM_ACCESS;

	_file = CreateFile(_file_path, modes, 0, 0, creation, flags, 0);
	if (!_file) return false;

	if (!ThreadPool::ref()->Associate(_file, this))
	{
		CloseHandle(_file);
		return false;
	}

	return true;
}

void AsyncFile::Close()
{
	if (_file)
	{
		CloseHandle(_file);

		_file = 0;
	}
}

u32 AsyncFile::GetSize()
{
	LARGE_INTEGER size;

	if (!GetFileSizeEx(_file, &size))
		return 0;

	return size.LowPart;
}

bool AsyncFile::BeginRead(u32 offset, u32 bytes, ReadFileCallback callback)
{
	AddRef();

	// Loop until memory may be allocated
	ReadFileOverlapped *readOv;
	do readOv = AcquireBuffer<ReadFileOverlapped>(bytes);
	while (!readOv);

	readOv->ov.Set(OVOP_READFILE_EX);
	readOv->offset = offset;
	readOv->callback = callback;

	BOOL result = ReadFileEx(_file, GetTrailingBytes(readOv), bytes, &readOv->ov.ov, 0);

	if (!result && GetLastError() != ERROR_IO_PENDING)
	{
		WARN("AsyncReadFile") << "ReadFileEx error: " << GetLastError();
		RegionAllocator::ii->Release(readOv);
		ReleaseRef();
		return false;
	}

	return true;
}

bool AsyncFile::BeginBulkRead(u32 offset, u32 bytes, void *buffer)
{
	AddRef();

	// Loop until memory may be allocated
	ReadFileBulkOverlapped *readOv;
	do readOv = AcquireBuffer<ReadFileBulkOverlapped>(0);
	while (!readOv);

	readOv->ov.Set(OVOP_READFILE_BULK);
	readOv->offset = offset;
	readOv->buffer = buffer;

	BOOL result = ReadFileEx(_file, buffer, bytes, &readOv->ov.ov, 0);

	if (!result && GetLastError() != ERROR_IO_PENDING)
	{
		WARN("AsyncReadFile") << "ReadFileEx bulk error: " << GetLastError();
		RegionAllocator::ii->Release(readOv);
		ReleaseRef();
		return false;
	}

	return true;
}

bool AsyncFile::BeginWrite(u32 offset, void *buffer, u32 bytes)
{
	AddRef();

	// Recover the full overlapped structure from data pointer
	TypedOverlapped *sendOv = reinterpret_cast<TypedOverlapped*>(
		reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped) );

	sendOv->Set(OVOP_WRITEFILE_EX);

	BOOL result = WriteFile(_file, buffer, bytes, 0, &sendOv->ov);

	if (!result && GetLastError() != ERROR_IO_PENDING)
	{
		WARN("AsyncReadFile") << "WriteFileEx error: " << GetLastError();
		RegionAllocator::ii->Release(sendOv);
		ReleaseRef();
		return false;
	}

	return true;
}

void AsyncFile::OnRead(ThreadPoolLocalStorage *tls, ReadFileOverlapped *readOv, u32 bytes)
{
	if (readOv->callback)
		readOv->callback(tls, readOv->offset, GetTrailingBytes(readOv), bytes);
}
