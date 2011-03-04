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
#include <cat/io/MappedFile.hpp>
#include <vector>
#include <queue> // priority_queue<>

namespace cat {


namespace sphynx {


class CAT_EXPORT FileTransferSource : public IHugeSource
{
	static const u32 SIMULTANEOUS_FILES = 3;

	struct QueuedFile
	{
		u32 priority; // lower = lower priority
		SequentialFileReader reader;

		// File announcement message
		u8 *msg;
		u32 msg_bytes;

		// Returns true if lhs > rhs for priority
		CAT_INLINE bool operator()(const QueuedFile *lhs, const QueuedFile *rhs)
		{
			return lhs->priority > rhs->priority;
		}
	};

	typedef std::priority_queue<QueuedFile*, std::vector<QueuedFile*>, QueuedFile> FileHeap;
	typedef std::vector<QueuedFile*> FileVector;

	Mutex _lock;
	FileHeap _heap;
	FileVector _active_list;

	void ClearHeap();
	void StartTransfer(QueuedFile *file, Transport *transport);

	void OnTransferDone(Transport *transport);

protected:
	u64 GetRemaining(StreamMode stream);
	bool Read(StreamMode stream, u8 *dest, u32 &bytes, Transport *transport);

public:
	FileTransferSource();
	~FileTransferSource();

	// Queue up a file transfer
	bool WriteFile(u8 opcode, const std::string &source_path, const std::string &sink_path, Transport *transport, u32 priority = 0);
};


class CAT_EXPORT FileTransferSink
{
	// TODO: Thread safety
public:
	FileTransferSink();
	~FileTransferSink();

	// Queue up a file transfer
	bool OnFileStart(BufferStream msg, u32 bytes);

	// Takes over void OnReadHuge(StreamMode stream, BufferStream data, u32 size)
	void OnReadHuge(u32 stream, BufferStream data, u32 size);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_FILE_TRANSFER_HPP
