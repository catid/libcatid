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
#include <cat/io/Logging.hpp>
using namespace cat;
using namespace sphynx;


//// FileTransferSource

FileTransferSource::FileTransferSource()
{
}

FileTransferSource::~FileTransferSource()
{
	ClearHeap();
}

u64 FileTransferSource::GetRemaining(StreamMode stream)
{
	AutoMutex lock(_lock);

	if (stream != STREAM_BULK)
	{
		CAT_WARN("FileTransferSource") << "Stream is not bulk";
		return 0;
	}

	if (_active_list.size() <= 0)
	{
		CAT_WARN("FileTransferSource") << "Read request on empty queue";
		return 0;
	}

	return _active_list[0]->reader->Remaining();
}

void FileTransferSource::OnTransferDone(Transport *transport)
{
	if (_heap.size() > 0)
	{
		QueuedFile *file = _heap.top();
		_heap.pop();

		StartTransfer(file, transport);
	}
}

bool FileTransferSource::Read(StreamMode stream, u8 *dest, u32 &bytes, Transport *transport)
{
	if (stream != STREAM_BULK)
	{
		CAT_WARN("FileTransferSource") << "Stream is not bulk";
		return 0;
	}

	AutoMutex lock(_lock);

	if (_active_list.size() <= 0)
	{
		CAT_WARN("FileTransferSource") << "Read request on empty queue";
		return 0;
	}

	QueuedFile *active = _active_list[0];

	// TODO: Testing transfer speed
	memset(dest, 0, bytes);
/*
	if (!active->reader->Read(dest, bytes, bytes))
	{
		CAT_WARN("FileTransferSource") << "Reached end of data";

		OnTransferDone(transport);

		bytes = 0;
		return false;
	}
*/
	return true;
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

void FileTransferSource::StartTransfer(QueuedFile *file, Transport *transport)
{
	// Grab the message and remove its reference from the file object
	u8 *msg = file->msg;
	file->msg = 0;


	_active_list.push_back(file);

	if (!transport->WriteReliableZeroCopy(STREAM_BULK, msg, file->msg_bytes))
	{
		CAT_WARN("FileTransferSource") << "Unable to write reliable with " << file->msg_bytes;
		return;
	}

	// Retry writing the huge message until it succeeds
	while (!transport->WriteHuge(STREAM_BULK, this));
}

bool FileTransferSource::TransferFile(u32 worker_id, u8 opcode, const std::string &source_path, const std::string &sink_path, Transport *transport, u32 priority)
{
	// Build a queued file object
	QueuedFile *file = new QueuedFile;
	if (!file)
	{
		CAT_WARN("FileTransferSource") << "Out of memory: Unable to allocate QueuedFile";
		return false;
	}

	if (!RefObjects::Create(CAT_REFOBJECT_TRACE, file->reader))
	{
		CAT_WARN("FileTransferSource") << "Out of memory: Unable to allocate PolledFileReader";
		return false;
	}

	// If source file could not be opened,
	if (!file->reader->Open(source_path.c_str(), worker_id))
	{
		CAT_WARN("FileTransferSource") << "Unable to open specified file " << source_path;
		return false;
	}

	u32 sink_path_len = (u32)sink_path.length();
	u32 msg_bytes = 1 + sink_path_len + sizeof(u64);

	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg)
	{
		CAT_WARN("FileTransferSource") << "Out of memory: Unable to allocate outgoing message bytes = " << msg_bytes;
		delete file;
		return false;
	}

	// Construct message
	msg[0] = opcode;
	memcpy(msg + 1, sink_path.c_str(), sink_path_len);
	*(u64*)(msg + 1 + sink_path_len) = getLE(file->reader->Size());

	// Configure file object
	file->priority = priority;
	file->msg = msg;
	file->msg_bytes = msg_bytes;

	AutoMutex lock(_lock);

	// If there is room for more simultaneous file transfers,
	if (_active_list.size() < SIMULTANEOUS_FILES)
		StartTransfer(file, transport);
	else
		_heap.push(file);

	return true;
}


//// FileTransferSink

FileTransferSink::FileTransferSink()
{
	_file = 0;
	//_write_offset = 0;
}

FileTransferSink::~FileTransferSink()
{
	Close();
}

void FileTransferSink::Close()
{
	if (_file)
	{
		_file->Destroy(CAT_REFOBJECT_TRACE);
		_file = 0;
	}
}

bool ValidFilePath(const char *file_path)
{
	// TODO
	return true;
}

bool FileTransferSink::OnFileStart(u32 worker_id, BufferStream msg, u32 bytes)
{
	if (_file)
	{
		CAT_WARN("FileTransferSink") << "New file transfer request ignored: File transfer already in progress";
		return false;
	}

	if (bytes < 1 + sizeof(u64) + 1)
	{
		CAT_WARN("FileTransferSink") << "Truncated file transfer start message ignored";
		return false;
	}

	// Extract the file size
	u32 sink_path_len = bytes - (1 + sizeof(u64));
	u64 file_size = *(u64*)(msg + 1 + sink_path_len);

	// Terminate the path
	msg[1 + sink_path_len] = '\0';

	const char *file_path = msg.c_str() + 1;

	if (!ValidFilePath(file_path))
	{
		CAT_WARN("FileTransferSink") << "Ignored invalid file transfer path";
		return false;
	}

	if (!RefObjects::Create(CAT_REFOBJECT_TRACE, _file))
	{
		CAT_WARN("FileTransferSink") << "Out of memory allocating AsyncFile object";
		return false;
	}

	if (!_file->Open(file_path,
					 ASYNCFILE_WRITE|ASYNCFILE_SEQUENTIAL|ASYNCFILE_NOBUFFER))
	{
		CAT_WARN("FileTransferSink") << "Unable to open output file for writing";
		Close();
		return false;
	}

	if (!_file->SetSize(file_size))
	{
		CAT_WARN("FileTransferSink") << "Unable to set output file size.  Out of disk space?";
		Close();
		return false;
	}

	CAT_WARN("FileTransferSink") << "Accepting file transfer for " << file_path << " of " << file_size << " bytes";

	_worker_id = worker_id;
	_write_offset = 0;

	return true;
}

void FileTransferSink::OnWrite(const BatchSet &buffers)
{
	for (BatchHead *next, *head = buffers.head; head; head = next)
	{
		WriteBuffer *buffer = (WriteBuffer*)head;
		next = head->batch_next;

		delete buffer;
	}
}

void FileTransferSink::OnReadHuge(u32 stream, BufferStream data, u32 size)
{
	if (!_file)
	{
		// TODO: Don't waste console time on this
		//CAT_WARN("FileTransferSink") << "Ignored huge read when no file transfer was expected";
		return;
	}

	_file->AddRef(CAT_REFOBJECT_TRACE);

	WriteBuffer *buffer = new WriteBuffer;
	buffer->worker_id = _worker_id;
	buffer->callback.SetMember<FileTransferSink, &FileTransferSink::OnWrite>(this);

	if (!buffer)
	{
		CAT_WARN("FileTransferSink") << "File transfer part failed: Out of memory to allocate write buffer";
		return;
	}

	if (!_file->Write(buffer, _write_offset, data, size))
	{
		CAT_WARN("FileTransferSink") << "Unable to write to output file.  Out of disk space?";
		delete buffer;
		Close();
	}

	_write_offset += size;
}
