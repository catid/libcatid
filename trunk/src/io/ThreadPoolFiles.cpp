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

#include <cat/io/ThreadPoolFiles.hpp>
#include <cat/port/AlignedAlloc.hpp>
using namespace cat;

AsyncReadFile::AsyncReadFile()
{
	_file = 0;
}

AsyncReadFile::~AsyncReadFile()
{
}

bool AsyncReadFile::Open(const char *path)
{
	_file = CreateFile(path, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY | FILE_FLAG_OVERLAPPED, 0);

	if (!_file) return false;

	ThreadPool::Associate(_file, 0);

	return true;
}

bool AsyncReadFile::Read(u32 offset, u32 bytes, ReadFileCallback callback)
{
	if (!_file) return false;

	// Calculate the size of the memory region containing the overlapped structure, keys and buffer
	u32 ovb = sizeof(ReadFileOverlapped) + bytes;

	ReadFileOverlapped *ov = reinterpret_cast<ReadFileOverlapped *>( Aligned::Acquire(ovb) );
	if (!ov) return false;

	ov->Set(OVOP_READFILE_EX);
	ov->_callback = callback;

	if (!ReadFileEx(_file, ov->_buffer, bytes, &ov->_ov, 0))
		return false;

	return true;
}
