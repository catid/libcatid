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

#ifndef CAT_BUFFERED_FILE_WRITER_HPP
#define CAT_BUFFERED_FILE_WRITER_HPP

#include <cat/io/IOLayer.hpp>

/*
	This implementation of a polled file reader is designed for maximum
	throughput.  The access patterns are tuned to work for a wide range
	of common disk types:

	+ The reads are hinted as sequential access pattern to help the OS.
	I have noticed this helping ever so slightly.  It cannot hurt anyway.

	+ Each read is not buffered by the OS since that halves the throughput
	in some cases.  File caching should be implemented by the application
	to have your cake and eat it too.  Note that this means that the read
	buffers must be page-aligned, but that is handled internally here.

	+ There are always 2 * (processor count) requests outstanding, and at
	least 16 even if the processor count is low to allow for very fast
	RAID arrays of SSDs to perform at their peak.
	For single mechanical disks, this can be set lower without hurting.

	+ Each read from the disks is 32768 bytes.  It's the magic number.
	In some cases raising the number will not hurt much.
	In most cases lowering this number will hurt.
*/

namespace cat {


// See above for rationale of these choices
static const u32 OPTIMAL_FILE_WRITE_CHUNK_SIZE = 32768;
static const u32 OPTIMAL_FILE_WRITE_MODE = ASYNCFILE_WRITE | ASYNCFILE_SEQUENTIAL | ASYNCFILE_NOBUFFER;


} // namespace cat

#endif // CAT_BUFFERED_FILE_WRITER_HPP
