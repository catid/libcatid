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

// 06/11/09 part of libcat-1.0

#ifndef MMAPFILE_HPP
#define MMAPFILE_HPP

#include <cat/Platform.hpp>

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#endif

namespace cat {


class MMapFile
{
    char *data;
    u32 len;
    s32 offset;

#if defined(CAT_OS_LINUX)
    int fd;
#elif defined(CAT_OS_WINDOWS)
    HANDLE hFile, hMapping;
#endif

public:
    MMapFile(const char *path);
    ~MMapFile();

    inline bool good() { return data != 0; }
    inline bool inside() { return offset >= 0 && offset < (s32)len; }

    inline u32 size() { return len; }

    inline void seek(s32 poffset) { offset = poffset; }
    inline bool underrun(s32 requested) { return (u32)(offset + requested) > len; }
    inline char *look() { return data + offset; }
    inline char *look(s32 offset) { return data + offset; }
    inline char *read(s32 requested) { offset += requested; return data + (offset - requested); }
    inline void skip(s32 requested) { offset += requested; }
    inline u32 remaining() { return len - offset; }
    inline u32 getOffset() { return offset; }
};


} // namespace cat

#endif // MMAPFILE_HPP
