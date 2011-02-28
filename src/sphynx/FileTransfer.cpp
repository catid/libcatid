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

#include <cat/sphynx/FileTransfer.hpp>
using namespace cat;
using namespace sphynx;

FileTransferSource::FileTransferSource()
{
	_active = 0;
}

FileTransferSource::~FileTransferSource()
{
	ClearHeap();
}

u64 FileTransferSource::GetRemaining(StreamMode stream)
{
	if (stream != STREAM_BULK)
		return 0;

	return 0;
}

u32 FileTransferSource::Read(StreamMode stream, u8 *dest, u32 bytes, Transport *transport)
{
	if (stream != STREAM_BULK)
		return 0;

	return 0;
}

void FileTransferSource::ClearHeap()
{
	AutoMutex lock(_lock);

	// For each heap element,
	while (!_heap.empty())
	{
		// Get top element from heap
		QueuedFile *file = _heap.top();
		_heap.pop();

		if (file->msg)
			OutgoingMessage::Release(file->msg);

		// Free memory
		delete file;
	}
}

bool FileTransferSource::StartTransfer(QueuedFile *file, Transport *transport)
{
	// Grab the message and remove its reference from the file object
	u8 *msg = file->msg;
	file->msg = 0;

	return transport->WriteReliableZeroCopy(STREAM_BULK, msg, file->msg_bytes) &&
		   transport->WriteHuge(STREAM_BULK, this);
}

bool FileTransferSource::WriteFile(u8 opcode, const std::string &source_path, const std::string &sink_path, Transport *transport, u32 priority)
{
	// Build a queued file object
	QueuedFile *file = new QueuedFile;
	if (!file)
	{
		WARN("FileTransferSource") << "Out of memory: Unable to allocate queued file";
		return false;
	}

	// If source file could not be opened,
	if (!file->reader.Open(source_path.c_str()))
	{
		WARN("FileTransferSource") << "Unable to open specified file " << source_path;
		return false;
	}

	u32 sink_path_len = (u32)sink_path.length();
	u32 msg_bytes = 1 + sizeof(u64) + sink_path_len;

	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg)
	{
		WARN("FileTransferSource") << "Out of memory: Unable to allocate outgoing message bytes = " << msg_bytes;
		delete file;
		return false;
	}

	// Construct message
	msg[0] = opcode;
	*(u64*)(msg + 1) = getLE(file->reader.GetLength());
	memcpy(msg + 1 + 8, sink_path.c_str(), sink_path_len);

	// Configure file object
	file->priority = priority;
	file->msg = msg;
	file->msg_bytes = msg_bytes;

	// Push it on the heap
	_lock.Enter();
	_heap.push(file);
	_lock.Leave();

	return true;
}


//// FileTransferSink

FileTransferSink::FileTransferSink()
{
}

FileTransferSink::~FileTransferSink()
{
}

bool FileTransferSink::OnFileStart(BufferStream msg, u32 bytes)
{
	return true;
}

void FileTransferSink::OnReadHuge(StreamMode stream, BufferStream data, u32 size)
{
}
