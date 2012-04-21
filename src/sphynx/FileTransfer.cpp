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


static UDPSendAllocator *m_udp_send_allocator = 0;


const char *cat::sphynx::GetTransferAbortReasonString(int reason)
{
	switch (reason)
	{
	case TXERR_NO_PROBLEMO:		return "OK";
	case TXERR_BUSY:			return "Source is not idle and cannot service another request";
	case TXERR_REJECTED:		return "Source rejected the request based on file name";
	case TXERR_FILE_OPEN_FAIL:	return "Source unable to open the requested file";
	case TXERR_FILE_READ_FAIL:	return "Source unable to read part of the requested file";
	case TXERR_FEC_FAIL:		return "Forward error correction codec reported an error";
	case TXERR_OUT_OF_MEMORY:	return "Source ran out of memory";
	case TXERR_USER_ABORT:		return "Closed by user";
	case TXERR_SHUTDOWN:		return "Remote host is shutting down";
	default:					return "[Unknown]";
	}
}


//// FECHugeEndpoint

FECHugeEndpoint::FECHugeEndpoint()
{
	_read_bytes = 0;
	_file = 0;

	_streams = 0;
}

FECHugeEndpoint::~FECHugeEndpoint()
{
	Cleanup();
}

void FECHugeEndpoint::Initialize(Transport *transport, u8 opcode)
{
	// Initialize state
	_state = TXS_IDLE;
	_abort_reason = TXERR_NO_PROBLEMO;
	_transport = transport;
	_opcode = opcode;

	// Clear callbacks
	_on_send_request.Invalidate();
	_on_send_done.Invalidate();
	_on_recv_request.Invalidate();
	_on_recv_done.Invalidate();
}

bool FECHugeEndpoint::Setup()
{
	m_udp_send_allocator = UDPSendAllocator::ref();
	if (!m_udp_send_allocator) return false;

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
			streams[ii].read_buffer_object.callback = WorkerDelegate::FromMember<FECHugeEndpoint, &FECHugeEndpoint::OnFileRead>(this);
			streams[ii].requested = 0;
			streams[ii].read_buffer = 0;
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

void FECHugeEndpoint::Cleanup()
{
	CAT_WARN("FECHugeEndpoint") << "Cleanup";

	if (_streams)
	{
		void *read_buffer = _streams[0].read_buffer;
		if (read_buffer) LargeAllocator::ref()->Release(read_buffer);

		delete []_streams;
		_streams = 0;
	}
}

void FECHugeEndpoint::OnFileRead(ThreadLocalStorage &tls, const BatchSet &set)
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
		{
			CAT_WARN("FECHugeEndpoint") << "OnFileRead: Ignoring data from wrong buffer";
			continue; // Ignore it
		}

		// If read failed,
		u32 bytes = buffer->data_bytes;
		if (bytes == 0)
		{
			CAT_WARN("FECHugeEndpoint") << "OnFileRead: File read with length = 0 indicating read failure";

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
		int mss = _transport->GetMaxPayloadBytes();
		int block_bytes = CAT_FT_MSS_TO_BLOCK_BYTES(mss);

		// Set up stream state
		stream->mss = mss;
		stream->compress_bytes = compress_bytes;
		stream->next_id = 0;

		// Initialize the encoder (slow!)
		wirehair::Result r = stream->encoder.BeginEncode(compress_buffer, compress_bytes, block_bytes);
		if (r == wirehair::R_WIN)
		{
			const u32 block_count = stream->encoder.BlockCount();

			CAT_WARN("FECHugeEndpoint") << "OnFileRead: Read " << bytes << " bytes, compressed to " << compress_bytes << " block_bytes=" << block_bytes << " and blocks=" << block_count;

			stream->ready_flag = TXFLAG_READY;

			Atomic::StoreMemoryBarrier();

			stream->requested = block_count;
		}
		else if (r == wirehair::R_TOO_SMALL && compress_bytes <= block_bytes)
		{
			CAT_WARN("FECHugeEndpoint") << "OnFileRead: Read " << bytes << " bytes, compressed to " << compress_bytes << " block_bytes=" << block_bytes << " and single block";

			stream->ready_flag = TXFLAG_SINGLE;

			Atomic::StoreMemoryBarrier();

			stream->requested = 1;
		}
		else
		{
			CAT_WARN("FECHugeEndpoint") << "Wirehair encoder failed with error " << wirehair::GetResultString(r);
			_abort_reason = TXERR_FEC_FAIL;
		}

		break;
	}
}

bool FECHugeEndpoint::PostPart(u32 stream_id, BatchSet &buffers, u32 &count)
{
	CAT_WARN("FECHugeEndpoint") << "PostPart for stream_id=" << stream_id;

	Stream *stream = &_streams[stream_id];

	const u32 mss = stream->mss;

	u8 *msg = m_udp_send_allocator->Acquire(mss);
	if (!msg) return false;

	// Generate header and length
	u32 hdr = IOP_HUGE | ((stream_id & FT_STREAM_ID_MASK) << FT_STREAM_ID_SHIFT);

	// Attach header
	msg[0] = Transport::HUGE_HEADER_BYTE;

	// Add compress bit to header
	u32 hdr_bytes;
	u32 data_id = stream->next_id++;
	if (data_id < 65536)
	{
		hdr |= FT_COMPRESS_ID_MASK;
		hdr_bytes = 4;
	}
	else
	{
		msg[4] = (u8)(data_id >> 16);
		hdr_bytes = 5;
	}

	// Write header
	msg[1] = hdr;
	msg[2] = (u8)data_id;
	msg[3] = (u8)(data_id >> 8);

	// If FEC is bypassed,
	u32 bytes;
	if (stream->ready_flag == TXFLAG_SINGLE)
	{
		bytes = stream->compress_bytes;
		memcpy(msg + hdr_bytes, stream->read_buffer, bytes);
	}
	else
	{
		bytes = stream->encoder.Encode(data_id, msg + hdr_bytes);
	}

	// Carve out just the part of the buffer we're using
	SendBuffer *buffer = SendBuffer::Promote(msg);
	buffer->data_bytes = bytes + hdr_bytes;

	// Add it to the list
	buffers.PushBack(buffer);
	++count;

	return true;
}

bool FECHugeEndpoint::PostPushRequest(const char *file_path)
{
	CAT_WARN("FECHugeEndpoint") << "PostPushRequest for file " << file_path;

	int file_name_length = (int)strlen(file_path);

	const u32 msg_bytes = 1 + 1 + file_name_length + 1;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_PUSH_REQUEST;
	memcpy(msg + 2, file_path, file_name_length + 1);

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostPullRequest(const char *file_path)
{
	CAT_WARN("FECHugeEndpoint") << "PostPullRequest for file " << file_path;

	int file_name_length = (int)strlen(file_path);

	const u32 msg_bytes = 1 + 1 + file_name_length + 1;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_PULL_REQUEST;
	memcpy(msg + 2, file_path, file_name_length + 1);

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostPullGo(u64 file_bytes, int stream_count)
{
	CAT_WARN("FECHugeEndpoint") << "PostPullGo file_bytes=" << file_bytes << " stream_count=" << stream_count;

	const u32 msg_bytes = 1 + 1 + 8 + 1;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_PULL_GO;
	*(u64*)(msg + 2) = getLE(file_bytes);
	msg[2 + 8] = (u8)stream_count;

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostDeny(int reason)
{
	CAT_WARN("FECHugeEndpoint") << "PostDeny for reason " << reason << " : " << GetTransferAbortReasonString(reason);

	const u32 msg_bytes = 1 + 1 + 1;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_DENY;
	msg[2] = (u8)reason;

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostStart(u64 file_offset, u32 chunk_decompressed_size, u32 chunk_compressed_size, int block_bytes, int stream_id)
{
	CAT_WARN("FECHugeEndpoint") << "PostStart file_offset=" << file_offset << " chunk_decompressed_size=" << chunk_decompressed_size << " chunk_compressed_size=" << chunk_compressed_size << " block_bytes=" << block_bytes << " stream_id=" << stream_id;

	const u32 msg_bytes = 1 + 1 + 8 + 4 + 4 + 2 + 1;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_START;
	*(u64*)(msg + 2) = getLE(file_offset);
	*(u32*)(msg + 10) = getLE(chunk_decompressed_size);
	*(u32*)(msg + 14) = getLE(chunk_compressed_size);
	*(u16*)(msg + 18) = getLE16((u16)block_bytes);
	msg[20] = (u8)stream_id;

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostStartAck(int stream_id)
{
	CAT_WARN("FECHugeEndpoint") << "PostStartAck stream_id=" << stream_id;

	const u32 msg_bytes = 1 + 1 + 1;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_START_ACK;
	msg[2] = (u8)stream_id;

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostRate(u32 rate_counter)
{
	CAT_WARN("FECHugeEndpoint") << "PostRate rate_counter=" << rate_counter;

	const u32 msg_bytes = 1 + 1 + 4;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_RATE;
	*(u32*)(msg + 2) = getLE(rate_counter);

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostRequest(int stream_id, int request_count)
{
	CAT_WARN("FECHugeEndpoint") << "PostRequest stream id=" << stream_id << " and request_count=" << request_count;

	const u32 msg_bytes = 1 + 1 + 1 + 2;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_REQUEST;
	msg[2] = (u8)stream_id;
	*(u16*)(msg + 3) = getLE16((u16)request_count);

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

bool FECHugeEndpoint::PostClose(int reason)
{
	CAT_WARN("FECHugeEndpoint") << "PostClose for reason " << reason << " : " << GetTransferAbortReasonString(reason);

	const u32 msg_bytes = 1 + 1 + 1;
	u8 *msg = OutgoingMessage::Acquire(msg_bytes);
	if (!msg) return false;

	msg[0] = _opcode;
	msg[1] = TOP_CLOSE;
	msg[2] = (u8)reason;

	return _transport->WriteReliableZeroCopy(STREAM_1, msg, msg_bytes);
}

void FECHugeEndpoint::OnPushRequest(const char *file_path)
{
	// If state is not idle,
	if (_state != TXS_IDLE)
	{
		CAT_WARN("FECHugeEndpoint") << "OnPushRequest: Ignoring push request for file " << file_path << " as state is not idle";
		PostDeny(TXERR_BUSY);
		return;
	}

	// If recv request callback rejects the file path,
	if (_on_recv_request.IsValid() && !_on_recv_request(file_path))
	{
		CAT_WARN("FECHugeEndpoint") << "OnPushRequest: Rejecting push request for file " << file_path << " by callback";
		PostDeny(TXERR_REJECTED);
		return;
	}

	if (!PostPullRequest(file_path))
	{
		CAT_WARN("FECHugeEndpoint") << "OnPushRequest: Unable to post pull request";
		PostDeny(TXERR_OUT_OF_MEMORY);
		return;
	}

	CAT_WARN("FECHugeEndpoint") << "OnPushRequest: Accepted push request for file " << file_path;
}

void FECHugeEndpoint::OnPullRequest(const char *file_path)
{
	// If state is not idle,
	if (_state != TXS_IDLE)
	{
		CAT_WARN("FECHugeEndpoint") << "OnPullRequest: Ignoring pull request for file " << file_path << " because state is not idle";
		PostDeny(TXERR_BUSY);
		return;
	}

	// If send request callback rejects the file path,
	if (_on_send_request.IsValid() && !_on_send_request(file_path))
	{
		CAT_WARN("FECHugeEndpoint") << "OnPullRequest: File " << file_path << " rejected by send request callback";
		PostDeny(TXERR_REJECTED);
		return;
	}

	// Open new file
	AsyncFile *file;
	if (!RefObjects::ref()->Create(CAT_REFOBJECT_TRACE, file))
	{
		CAT_WARN("FECHugeEndpoint") << "OnPullRequest: Could not create refobject for " << file_path;
		PostDeny(TXERR_OUT_OF_MEMORY);
		return;
	}

	// If file could not be opened,
	if (!file->Open(file_path, ASYNCFILE_READ | ASYNCFILE_SEQUENTIAL | ASYNCFILE_NOBUFFER))
	{
		CAT_WARN("FECHugeEndpoint") << "OnPullRequest: File " << file_path << " could not be opened";

		file->Destroy(CAT_REFOBJECT_TRACE);

		PostDeny(TXERR_FILE_OPEN_FAIL);
		return;
	}

	// Cache file size
	_file_size = file->GetSize();

	// Initialize stream indices
	_num_streams = 4;
	_load_stream = 0;
	_dom_stream = 0;

	if (!Setup())
	{
		CAT_WARN("FECHugeEndpoint") << "OnPullRequest: Could not initialize buffers (out of memory) for " << file_path;

		file->Destroy(CAT_REFOBJECT_TRACE);

		PostDeny(TXERR_OUT_OF_MEMORY);
		return;
	}

	_file = file;

	// Pushing a file!
	_state = TXS_PUSHING;
	_abort_reason = TXERR_NO_PROBLEMO;

	CAT_WARN("FECHugeEndpoint") << "OnPullRequest: Starting to read " << file_path;

	if (!StartRead(0, 0, _read_bytes))
	{
		CAT_WARN("FECHugeEndpoint") << "OnPullRequest: Unreadable file " << file_path;

		_state = TXS_IDLE;

		file->Destroy(CAT_REFOBJECT_TRACE);

		PostDeny(TXERR_FILE_READ_FAIL);
		return;
	}
}

void FECHugeEndpoint::OnPullGo(u64 file_bytes, int stream_count)
{
	CAT_WARN("FECHugeEndpoint") << "OnPullGo file_bytes=" << file_bytes << " stream_count=" << stream_count;


}

void FECHugeEndpoint::OnDeny(int reason)
{
	CAT_WARN("FECHugeEndpoint") << "OnDeny for reason " << reason << " : " << GetTransferAbortReasonString(reason);
}

void FECHugeEndpoint::OnStart(u64 file_offset, u32 chunk_decompressed_size, u32 chunk_compressed_size, int block_bytes, int stream_id)
{
	CAT_WARN("FECHugeEndpoint") << "OnStart file_offset=" << file_offset << " chunk_decompressed_size=" << chunk_decompressed_size << " chunk_compressed_size=" << chunk_compressed_size << " block_bytes=" << block_bytes << " stream_id=" << stream_id;

}

void FECHugeEndpoint::OnStartAck(int stream_id)
{
	CAT_WARN("FECHugeEndpoint") << "OnStartAck for stream_id=" << stream_id;

}

void FECHugeEndpoint::OnRate(u32 rate_counter)
{
	CAT_WARN("FECHugeEndpoint") << "OnRate with rate_counter=" << rate_counter;
}

void FECHugeEndpoint::OnRequest(int stream_id, int request_count)
{
	CAT_WARN("FECHugeEndpoint") << "OnRequest with stream_id=" << stream_id << " and request_count=" << request_count;
}

void FECHugeEndpoint::OnClose(int reason)
{
	CAT_WARN("FECHugeEndpoint") << "OnClose for reason " << reason << " : " << GetTransferAbortReasonString(reason);

	_abort_reason = reason;
}

void FECHugeEndpoint::NextHuge(s32 &available, BatchSet &buffers, u32 &count)
{
	// If no space, abort
	if (available <= 0) return;

	CAT_WARN("FECHugeEndpoint") << "NextHuge available=" << available;

	// If abortion requested,
	if (_abort_reason != TXERR_NO_PROBLEMO)
	{
		// NOTE: This will actually be sent late on the next tick
		PostClose(_abort_reason);

		_abort_reason = TXERR_NO_PROBLEMO;
		_state = TXS_IDLE;

		if (_file)
			_file->Destroy(CAT_REFOBJECT_TRACE);

		return;
	}

	// If state is idle, abort
	if (_state == TXS_IDLE) return;

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
			if (!PostPart(stream_id, buffers, count))
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

	// If non-dominant stream is requested,
	while (stream->requested > 0)
	{
		// Attempt to post a part of this stream
		if (!PostPart(dom_stream, buffers, count))
			break;

		// Reduce request count on success
		stream->requested--;

		// If out of room,
		if ((available -= stream->mss) <= 0)
			return;	// Done for now!
	}
}

void FECHugeEndpoint::OnHuge(u8 *data, u32 bytes)
{
	CAT_WARN("FECHugeEndpoint") << "OnHuge";


}

void FECHugeEndpoint::OnControlMessage(u8 *data, u32 bytes)
{
	if (bytes < 2)
	{
		CAT_WARN("FECHugeEndpoint") << "Ignored truncated control message";
		return;
	}

	if (data[0] != _opcode)
	{
		CAT_WARN("FECHugeEndpoint") << "Ignored control message with wrong opcode";
		return;
	}

	switch (data[1])
	{
	// Transmitter requesting to push a file, will cause receiver to issue a pull request
	case TOP_PUSH_REQUEST:	// HDR | FileName(X)
		if (bytes >= 2)
		{
			const char *file_path = reinterpret_cast<const char*>( data + 2 );

			// Ensure it is null-terminated
			data[bytes-1] = 0;

			OnPushRequest(file_path);
		}
		break;

	// Receiver requesting to pull a file
	case TOP_PULL_REQUEST:	// HDR | FileName(X)
		if (bytes >= 2)
		{
			const char *file_path = reinterpret_cast<const char*>( data + 2 );

			// Ensure it is null-terminated
			data[bytes-1] = 0;

			OnPullRequest(file_path);
		}
		break;

	// Transmitter indicating start of a file transfer
	case TOP_PULL_GO:		// HDR | FileBytes(8) | StreamCount(1)
		if (bytes >= 11)
		{
			u64 file_bytes = getLE(*(u64*)(data + 2));
			int stream_count = (u32)data[10];

			OnPullGo(file_bytes, stream_count);
		}
		break;

	// Deny a push or pull request with a reason
	case TOP_DENY:			// HDR | Reason(1)
		if (bytes >= 3)
		{
			int reason = (u32)data[2];

			OnDeny(reason);
		}
		break;

	// Transmitter notifying receiver that a stream is starting
	case TOP_START:			// HDR | FileOffset(8) | ChunkDecompressedSize(4) | ChunkCompressedSize(4) | BlockBytes(2) | StreamID(1)
		if (bytes >= 21)
		{
			u64 file_offset = getLE(*(u64*)(data + 2));
			u32 chunk_decompressed_size = getLE(*(u32*)(data + 10));
			u32 chunk_compressed_size = getLE(*(u32*)(data + 14));
			int block_bytes = getLE(*(u16*)(data + 18));
			int stream_id = (u32)data[20];

			OnStart(file_offset, chunk_decompressed_size, chunk_compressed_size, block_bytes, stream_id);
		}
		break;

	// Receiver notifying transmitter that it is ready to receive a stream
	case TOP_START_ACK:		// HDR | StreamID(1)
		if (bytes >= 3)
		{
			int stream_id = (u32)data[2];

			OnStartAck(stream_id);
		}
		break;

	// Adjust transmit rate on all streams
	case TOP_RATE:			// HDR | RateCounter(4)
		if (bytes >= 6)
		{
			u32 rate_counter = getLE(*(u32*)(data + 2));

			OnRate(rate_counter);
		}
		break;

	// Request a number of blocks on a stream
	case TOP_REQUEST:		// HDR | StreamID(1) | RequestCount(2)
		if (bytes >= 5)
		{
			int stream_id = (u32)data[2];
			int request_count = getLE(*(u16*)(data + 3));

			OnRequest(stream_id, request_count);
		}
		break;

	// Close transfer and indicate reason (including success)
	case TOP_CLOSE:			// HDR | Reason(1)
		if (bytes >= 3)
		{
			int reason = (u32)data[2];

			OnClose(reason);
		}
		break;
	}
}

bool FECHugeEndpoint::Request(const char *file_path)
{
	if (_state != TXS_IDLE)
	{
		CAT_WARN("FECHugeEndpoint") << "File transfer request ignored: Busy";
		return false;
	}

	return PostPullRequest(file_path);
}

bool FECHugeEndpoint::Send(const char *file_path)
{
	if (_state != TXS_IDLE)
	{
		CAT_WARN("FECHugeEndpoint") << "File transfer send ignored: Busy";
		return false;
	}

	return PostPushRequest(file_path);
}
