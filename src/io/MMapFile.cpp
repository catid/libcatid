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

#include <cat/io/MMapFile.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;

#if defined(CAT_OS_LINUX)
# include <sys/mman.h>
# include <sys/stat.h>
# include <fcntl.h>
#endif


MMapFile::MMapFile()
{
    _data = 0;
    _len = 0;

#if defined(CAT_OS_WINDOWS)

	_map = 0;
	_file = 0;

#else

	_fd = -1;

#endif
}

MMapFile::~MMapFile()
{
	Close();
}

bool MMapFile::Open(const char *path, bool readonly, bool random_access)
{
	Close();

#if defined(CAT_OS_WINDOWS)

	u32 file_access, share_access, creation_disposition;
	u32 page_permissions;

	if (readonly)
	{
		file_access = GENERIC_READ;
		share_access = FILE_SHARE_READ;
		creation_disposition = OPEN_EXISTING;

		page_permissions = PAGE_READONLY;

		_map_permissions = FILE_MAP_READ;
	}
	else
	{
		file_access = GENERIC_READ | GENERIC_WRITE;
		share_access = FILE_SHARE_READ | FILE_SHARE_WRITE;
		creation_disposition = CREATE_ALWAYS;

		page_permissions = PAGE_READWRITE;

		_map_permissions = FILE_MAP_WRITE;
	}

	u32 access_pattern = random_access ? FILE_FLAG_RANDOM_ACCESS : FILE_FLAG_SEQUENTIAL_SCAN;

	_file = CreateFileA(path, file_access, share_access, 0, creation_disposition, access_pattern, 0);
	if (_file == INVALID_HANDLE_VALUE)
	{
		INANE("MMapFile") << "CreateFileA error " << GetLastError() << " for " << path;
		return false;
	}

	if (!GetFileSizeEx(_file, (LARGE_INTEGER*)&_len))
	{
		INANE("MMapFile") << "GetFileSizeEx error " << GetLastError() << " for " << path;
		return false;
	}

	_map = CreateFileMapping(_file, 0, page_permissions, 0, 0, 0);
	if (!_map)
	{
		INANE("MMapFile") << "CreateFileMapping error " << GetLastError() << " for " << path;
		return false;
	}

#else
#error "TODO"
#endif

	return true;
}

bool MMapFile::SetLength(u64 length)
{
#if defined(CAT_OS_WINDOWS)

	LARGE_INTEGER offset;
	offset.QuadPart = length;

	if (!SetFilePointerEx(_file, offset, 0, FILE_BEGIN))
	{
		INANE("MMapFile") << "SetFilePointerEx error " << GetLastError() << " for " << length;
		return false;
	}

	if (!SetEndOfFile(_file))
	{
		INANE("MMapFile") << "SetEndOfFile error " << GetLastError() << " for " << length;
		return false;
	}

#else
#error "TODO"
#endif
}

u8 *MMapFile::MapView(u64 offset, u32 length)
{
	if (_data && !UnmapViewOfFile(_data))
	{
		INANE("MMapFile") << "UnmapViewOfFile error " << GetLastError();
	}

	_data = (u8*)MapViewOfFile(_map, _map_permissions, (u32)(offset >> 32), (u32)offset, length);
	if (!_data)
	{
		INANE("MMapFile") << "MapViewOfFile error " << GetLastError();
		return 0;
	}

	return _data;
}

void MMapFile::Close()
{
#if defined(CAT_OS_WINDOWS)

	if (_data)
	{
		UnmapViewOfFile(_data);
		_data = 0;
	}
	if (_map)
	{
		CloseHandle(_map);
		_map = 0;
	}
	if (_file)
	{
		CloseHandle(_file);
		_file = 0;
	}

#else

	if (fd != -1)
	{
		close(fd);
		fd = -1;
	}

	if (data)
	{
		munmap(data, len);
		data = 0;
	}

#endif
}


//// SequentialFileReader

bool SequentialFileReader::Open(const char *path)
{
	_file_offset = 0;
	_map_offset = 0;
	_map_size = 0;

	return _mmf.Open(path, true, false);
}

u8 *SequentialFileReader::Read(u32 bytes)
{
	// Read ahead
	if (bytes > MAX_READ_SIZE)
	{
		WARN("SequentialFileReader") << "Read size too large = " << bytes;
		return 0;
	}

	u32 map_offset = _map_offset;
	u32 map_size = _map_size;

	// If bytes read is available,
	if (bytes <= map_size - map_offset)
	{
		_map_offset = map_offset + bytes;

		return _mmf.GetView() + map_offset;
	}

	u64 file_offset = _file_offset + map_offset;
	u64 file_remaining = _mmf.GetLength() - file_offset;

	// If requested data is beyond the end of the file,
	if (bytes > file_remaining)
		return 0;

	u32 acquire = bytes;
	if (acquire < READ_AHEAD_CACHE)
	{
		if (READ_AHEAD_CACHE > file_remaining)
			acquire = (u32)file_remaining;
		else
			acquire = READ_AHEAD_CACHE;
	}

	// Map new view of file
	u8 *data = _mmf.MapView(file_offset, acquire);

	_file_offset = file_offset;
	_map_offset = 0;
	_map_size = acquire;

	return data;
}
