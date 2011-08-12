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

#ifndef CAT_CACHE_LINE_BYTES_HPP
#define CAT_CACHE_LINE_BYTES_HPP

#include <cat/threads/RefObjects.hpp>

namespace cat {


class SystemInfo : public RefObject
{
	// Number of bytes in each CPU cache line
	static u32 _CacheLineBytes;

	// Number of processors
	static u32 _ProcessorCount;

	// Page size
	static u32 _PageSize;

	// Allocation granularity
	static u32 _AllocationGranularity;

public:
	SystemInfo();
	CAT_INLINE virtual ~SystemInfo() {}

	static const u32 RefObjectGUID = 0xd4b15f58; // Global Unique IDentifier for acquiring RefObject singletons
	CAT_INLINE virtual const char *GetRefObjectName() { return "SystemInfo"; }

	static CAT_INLINE u32 GetCacheLineBytes() { return _CacheLineBytes; }
	static CAT_INLINE u32 GetProcessorCount() { return _ProcessorCount; }
	static CAT_INLINE u32 GetPageSize() { return _PageSize; }
	static CAT_INLINE u32 GetAllocationGranularity() { return _AllocationGranularity; }

protected:
	virtual bool OnRefObjectInitialize();
	CAT_INLINE virtual void OnRefObjectDestroy() {}
	CAT_INLINE virtual bool OnRefObjectFinalize() { return true; }
};


} // namespace cat

#endif // CAT_CACHE_LINE_BYTES_HPP
