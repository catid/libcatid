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

#ifndef THREAD_POOL_FILES_HPP
#define THREAD_POOL_FILES_HPP

#include <cat/threads/ThreadPool.hpp>

#if defined(CAT_OS_WINDOWS)
#include <windows.h>
#endif

namespace cat {


typedef void (*ReadFileCallback)(void *buffer, u32 bytes);


// ReadFile() OVERLAPPED structure
struct ReadFileOverlapped : public TypedOverlapped
{
	ReadFileCallback callback;
};


class AsyncReadFile
{
#if defined(CAT_OS_WINDOWS)
	HANDLE _file;
#endif

public:
	AsyncReadFile();
	~AsyncReadFile();

public:
	bool Open(const char *path);

	bool Read(u32 offset, u32 bytes, ReadFileCallback callback);
};


} // namespace cat

#endif // THREAD_POOL_FILES_HPP
