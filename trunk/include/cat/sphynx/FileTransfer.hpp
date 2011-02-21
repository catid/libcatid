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

#ifndef CAT_SPHYNX_FILE_TRANSFER_HPP
#define CAT_SPHYNX_FILE_TRANSFER_HPP

#include <cat/sphynx/Transport.hpp>

namespace cat {


namespace sphynx {


class FileTransferSource
{
public:
	FileTransferSource();
	~FileTransferSource();

	// Queue up a file transfer
	bool WriteFile(u8 opcode, const char *source_path, const char *sink_path, Transport *transport);

	// Takes over u32 OnWriteHugeRequest(StreamMode stream, u8 *data, u32 space)
	u32 OnWriteHugeRequest(StreamMode stream, u8 *data, u32 space);

	// Takes over u32 OnWriteHugeNext(StreamMode stream)
	u32 OnWriteHugeNext(StreamMode stream, Transport *transport);
};

class FileTransferSink
{
public:
	FileTransferSink();
	~FileTransferSink();

	// Queue up a file transfer
	bool OnFileStart(BufferStream msg, u32 bytes);

	// Takes over void OnReadHuge(StreamMode stream, BufferStream data, u32 size)
	void OnReadHuge(StreamMode stream, BufferStream data, u32 size);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_FILE_TRANSFER_HPP
