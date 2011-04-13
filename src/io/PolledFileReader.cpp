/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

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

#include <cat/io/PolledFileReader.hpp>
#include <cat/port/SystemInfo.hpp>
#include <cat/io/Settings.hpp>
#include <cat/mem/LargeAllocator.hpp>
using namespace cat;

PolledFileReader::PolledFileReader()
{
	u32 cache_size = Settings::ref()->getInt("File.ReadAheadCacheSize", 1024*1024*2);

	// Make it a multiple of the page size
	cache_size -= cache_size % system_info.PageSize;

	_cache_size = cache_size;

	// Allocate cache space and split it into two buffers
	_cache[0] = (u8*)LargeAllocator::ii->Acquire(cache_size * 2);
	_cache[1] = _cache[0] + cache_size;

	_remaining = 0;
}

PolledFileReader::~PolledFileReader()
{
	LargeAllocator::ii->Release(_cache[0]);
}

bool PolledFileReader::Open(IOLayer *layer, const char *file_path)
{
	if (!AsyncFile::Open(layer, file_path, ASYNCFILE_READ))
	{
		WARN("PolledFileReader") << "Unable to open " << file_path;
		return false;
	}

	AsyncFile::Read()
}

bool PolledFileReader::Read(void *data, u32 &bytes)
{

}

void PolledFileReader::OnRead(IWorkerTLS *tls, BatchSet &buffers)
{

}
