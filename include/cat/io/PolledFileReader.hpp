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

#ifndef CAT_POLLED_FILE_READER_HPP
#define CAT_POLLED_FILE_READER_HPP

#include <cat/io/IOLayer.hpp>

namespace cat {


class CAT_EXPORT PolledFileReader : public AsyncFile
{
	u8 *_cache[2];
	u32 _cache_size;
	u64 _remaining;
	ReadBuffer _buffer;

	void OnRead(IWorkerTLS *tls, BatchSet &buffers);

public:
	PolledFileReader();
	virtual ~PolledFileReader();

	bool Open(IOLayer *layer, const char *file_path);

	/*
		If Read() returns false, then the poll operation failed.
			The 'bytes' variable is unaffected.
		Otherwise if it returns true:
			If Read() sets 'bytes' to zero then the end of file has been reached.
			Otherwise it will set 'bytes' to the number of bytes read into 'data'.
	*/
	bool Read(void *data, u32 &bytes);
};


} // namespace cat

#endif // CAT_POLLED_FILE_READER_HPP
