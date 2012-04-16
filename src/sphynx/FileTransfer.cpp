/*
	Copyright (c) 2009-2012 Christopher A. Taylor.  All rights reserved.

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
#include <cat/threads/Atomic.hpp>
#include <cat/io/Log.hpp>
#include <ext/lz4/lz4.h>
using namespace cat;
using namespace sphynx;


//// FECHugeSource

FECHugeSource::FECHugeSource()
{
	_read_bytes = 0;
	_file = 0;

	_streams = 0;
}

FECHugeSource::~FECHugeSource()
{
	Cleanup();
}

void FECHugeSource::Initialize(Transport *transport, u32 worker_id)
{
	_transport = transport;
	_worker_id = worker_id;
}

bool FECHugeSource::Setup()
{
	if (_read_bytes == 0)
	{
		// Lookup page size
		u32 page_size = SystemInfo::ref()->GetPageSize();
		CAT_DEBUG_ENFORCE(CAT_IS_POWER_OF_2(page_size));

		// Round up to the next multiple of the page size above CHUNK_TARGET_LEN
		_read_bytes = CHUNK_TARGET_LEN - (CHUNK_TARGET_LEN & (page_size - 1)) + page_size;
		CAT_DEBUG_ENFORCE(_read_bytes >= CHUNK_TARGET_LEN);
	}

	if (!_streams)
	{
		// Allocate read buffer objects
		Stream *streams = new (std::nothrow) Stream[_num_streams];
		if (!streams) return false;
		_streams = streams;

		// Initialize buffers
		for (u32 ii = 0; ii < _num_streams; ++ii)
		{
			streams[ii].read_buffer_object.worker_id = _worker_id;
			streams[ii].read_buffer_object.callback = WorkerDelegate::FromMember<FECHugeSource, &FECHugeSource::OnFileRead>(this);
			streams[ii].requested = 0;
		}
	}

	if (!_streams[0].read_buffer)
	{
		// Determine number of bytes compression can inflate to when it fails
		u32 compress_bytes = LZ4_compressBound(_read_bytes);

		u32 compress_offset = _read_bytes * _num_streams;
		u32 alloc_bytes = compress_offset + compress_bytes * _num_streams;
		CAT_INFO("FileTransfer") << "Allocating " << alloc_bytes << " bytes for file transfer buffers";

		// Allocate read buffers
		u8 *buffer = (u8*)LargeAllocator::ref()->Acquire(alloc_bytes);
		if (!buffer) return false;

		// Initialize buffers
		u8 *read_buffer = buffer;
		u8 *compress_buffer = buffer + compress_offset;
		for (u32 ii = 0; ii < _num_streams; ++ii)
		{
			_streams[ii].read_buffer = read_buffer;
			_streams[ii].compress_buffer = compress_buffer;

			read_buffer += _read_bytes;
			compress_buffer += compress_bytes;
		}
	}

	return true;
}

void FECHugeSource::Cleanup()
{
	if (_streams)
	{
		void *read_buffer = _streams[0].read_buffer;
		if (read_buffer) LargeAllocator::ref()->Release(read_buffer);

		delete []_streams;
		_streams = 0;
	}
}

bool FECHugeSource::Start(const char *file_path)
{
	// If file is already open,
	if (_file) return false;

	// Open new file
	AsyncFile *file = new AsyncFile;
	CAT_ENFORCE(file);
	if (!file->Open(file_path, ASYNCFILE_READ | ASYNCFILE_SEQUENTIAL | ASYNCFILE_NOBUFFER))
	{
		file->Destroy(CAT_REFOBJECT_TRACE);
		return false;
	}
	_file = file;

	// Cache file size
	_file_size = _file->GetSize();

	// Initialize stream indices
	_load_stream = 0;
	_dom_stream = 0;

	if (!Setup()) return false;

	return StartRead(0, 0, _file_size <= _read_bytes ? (u32)_file_size : _read_bytes);
}

void FECHugeSource::OnFileRead(ThreadLocalStorage &tls, const BatchSet &set)
{
	Stream *stream = &_streams[_load_stream];

	// For each buffer in the set,
	for (BatchHead *node = set.head; node; node = node->batch_next)
	{
		// Unpack buffer
		ReadBuffer *buffer = static_cast<ReadBuffer*>( node );
		u8 *data = (u8*)buffer->data;

		// If buffer is not expected,
		if (stream->read_buffer != data)
			continue; // Ignore it

		// If read failed,
		u32 bytes = buffer->data_bytes;
		if (bytes == 0)
		{
			// Set abort reason, to be delivered on next data request
			_abort_reason = TXERR_FILE_READ_FAIL;
			break;
		}

		// Compress data (slow!)
		u8 *compress_buffer = stream->compress_buffer;
		int compress_bytes = LZ4_compress((const char*)data, (char*)compress_buffer, bytes);
		if (!compress_bytes)
		{
			compress_buffer = data;
			compress_bytes = bytes;
		}

		// Fix block bytes at the start of the transfer
		// Payload loadout = HDR(1) + TYPE|STREAM(1) + ID(3) + DATA
		u32 mss = _transport->GetMaxPayloadBytes();
		u32 block_bytes = CAT_MSS_TO_BLOCK_BYTES(mss);

		// Set up stream state
		stream->mss = mss;
		stream->compress_bytes = compress_bytes;
		stream->next_id = 0;

		// Initialize the encoder (slow!)
		wirehair::Result r = stream->encoder.BeginEncode(compress_buffer, bytes, block_bytes);
		if (r == wirehair::R_WIN)
		{
			Atomic::StoreMemoryBarrier();
			stream->ready_flag = TXFLAG_READY;
		}
		else if (r == wirehair::R_TOO_SMALL)
		{
			Atomic::StoreMemoryBarrier();
			stream->ready_flag = TXFLAG_SINGLE;
		}
		else
		{
			CAT_WARN("FECHugeSource") << "Wirehair encoder failed with error " << wirehair::GetResultString(r);
			_abort_reason = TXERR_FEC_FAIL;
		}

		break;
	}
}

bool FECHugeSource::PostPart(u32 stream_id)
{
	Stream *stream = &_streams[stream_id];

	const u32 mss = stream->mss;

	u8 *msg = SendBuffer::Acquire(mss);
	if (!msg) return false;

	msg[1] = IOP_HUGE | (stream_id << 2);

	u32 data_id = stream->next_id++;
	msg[2] = (u8)(data_id >> 16);
	msg[3] = (u8)(data_id >> 8);
	msg[4] = (u8)data_id;

	if (stream->ready_flag == TXFLAG_SINGLE)
	{
		// Just copy the number of bytes remaining
		memcpy(msg + 5, stream->read_buffer, stream->compress_bytes);
	}
	else
	{
		stream->encoder.Encode(data_id, msg + 5);
	}

	return _transport->PostHugeZeroCopy(msg, mss);
}

void FECHugeSource::NextHuge(s32 &available)
{
	// TODO: Send file requests and abortions and stuff here

	// If no space, abort
	if (available <= 0) return;

	// If no streams, abort
	if (!_streams) return;

	// Send requested streams first
	for (u32 stream_id = 0; stream_id < _num_streams; ++stream_id)
	{
		// Skip dominant stream
		if (stream_id == _dom_stream) continue;

		// If non-dominant stream is requested,
		Stream *stream = &_streams[stream_id];
		while (stream->requested > 0)
		{
			// Attempt to post a part of this stream
			if (!PostPart(stream_id))
				break;

			// Reduce request count on success
			stream->requested--;

			// If out of room,
			if ((available -= stream->mss) <= 0)
				return;	// Done for now!
		}
	}

	// Send dominant stream for remainder of the time
	u32 dom_stream = _dom_stream;
	Stream *stream = &_streams[dom_stream];

	// If dominant stream is still loading,
	if (stream->ready_flag == TXFLAG_LOADING)
		return; // Done for now!

	// If non-dominant stream is requested,
	CAT_FOREVER
	{
		// Attempt to post a part of this stream
		if (!PostPart(dom_stream))
			break;

		// If out of room,
		if ((available -= stream->mss) <= 0)
			return;	// Done for now!
	}
}


//// FECHugeSink

FECHugeSink::FECHugeSink()
{
}

FECHugeSink::~FECHugeSink()
{
}

void FECHugeSink::Initialize(Transport *transport, u32 worker_id)
{
	_transport = transport;
	_worker_id = worker_id;
}

bool FECHugeSink::Start(const char *file_path)
{
	return true;
}

void FECHugeSink::OnHuge(u8 *data, u32 bytes)
{
}
