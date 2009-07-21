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

#include <cat/io/MMapFile.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;

#if defined(CAT_OS_LINUX)
# include <sys/mman.h>
# include <sys/stat.h>
# include <fcntl.h>
#endif


MMapFile::MMapFile(const char *path)
{
    data = 0;
    offset = 0;
    len = 0;

#if defined(CAT_OS_LINUX)

    fd = open(path, O_RDONLY);
    if (fd == -1) { INANE("MMapFile") << "Unable to open file: " << path; return; }

    struct stat st;
    if (fstat(fd, &st) < 0) { INANE("MMapFile") << "Unable to stat file: " << path; return; }
    len = st.st_size;
    if (len == 0) return;

    data = (char *)mmap(0, len, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { INANE("MMapFile") << "Unable to mmap file: " << path; return; }

    close(fd);
    fd = -1;

#elif defined(CAT_OS_WINDOWS)

    hMapping = hFile = 0;

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_RANDOM_ACCESS, 0);
    if (hFile == INVALID_HANDLE_VALUE) { INANE("MMapFile") << "Unable to open file: " << path; return; }

    len = GetFileSize(hFile, 0);
    if (len == -1) { INANE("MMapFile") << "Unable to stat file: " << path; return; }
    if (len == 0) return;

    hMapping = CreateFileMapping(hFile, 0, PAGE_READONLY, 0, 0, 0);
    if (!hMapping) { INANE("MMapFile") << "Unable to CreateFileMapping[" << GetLastError() << "]: " << path; return; }

    data = (char *)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!data) { INANE("MMapFile") << "Unable to MapViewOfFile[" << GetLastError() << "]: " << path; return; }

#endif
}

MMapFile::~MMapFile()
{
#if defined(CAT_OS_LINUX)

    if (fd != -1) close(fd);
    if (data) munmap(data, len);

#elif defined(CAT_OS_WINDOWS)

    if (data) UnmapViewOfFile(data);
    if (hMapping) CloseHandle(hMapping);
    if (hFile) CloseHandle(hFile);

#endif
}
