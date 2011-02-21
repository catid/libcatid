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

#ifndef CAT_MMAPFILE_HPP
#define CAT_MMAPFILE_HPP

#include <cat/Platform.hpp>

#if defined(CAT_OS_WINDOWS)
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


class MMapFile
{
    u8 *_data;
    u64 _len;
	u32 _map_permissions;

#if defined(CAT_OS_WINDOWS)
    HANDLE _file, _map;
#else
	int _fd;
#endif

public:
    MMapFile();
    ~MMapFile();

	bool Open(const char *path, bool readonly = true, bool random_access = false);
	bool SetLength(u64 length);
	void Close();

	// Only one view is valid at a time
	u8 *MapView(u64 offset, u32 length);

	CAT_INLINE bool IsValid() { return _data != 0; }
	CAT_INLINE u64 GetLength() { return _len; }
	CAT_INLINE u8 *GetView() { return _data; }
};


class SequentialFileReader
{
public:
	static const u32 READ_AHEAD_CACHE = 16000000; // 16 MB read ahead cache
	static const u32 MAX_READ_SIZE = 512000000; // 512 MB read limit (per read)

private:
	MMapFile _mmf;
	u64 _file_offset;

	u32 _map_offset, _map_size;

public:
	bool Open(const char *path);

	// Returns 0 if read would be beyond end of file
	u8 *Read(u32 bytes);

	CAT_INLINE bool IsValid() { return _mmf.IsValid(); }
	CAT_INLINE u64 GetLength() { return _mmf.GetLength(); }
	CAT_INLINE void Close() { _mmf.Close(); }
};


class SequentialFileWriter
{
public:
	static const u32 WRITE_AHEAD_CACHE = 4000000; // 4 MB write ahead cache
	static const u32 MAX_WRITE_SIZE = 512000000; // 512 MB write limit (per write)

private:
	MMapFile _mmf;
	u64 _file_offset;

	u32 _map_offset, _map_size;

public:
	bool Open(const char *path, u64 length);

	// Returns false if write would be beyond end of file
	bool Write(void *data, u32 bytes);

	CAT_INLINE bool IsValid() { return _mmf.IsValid(); }
	CAT_INLINE u64 GetLength() { return _mmf.GetLength(); }
	CAT_INLINE void Close() { _mmf.Close(); }
};


} // namespace cat

#endif // CAT_MMAPFILE_HPP
