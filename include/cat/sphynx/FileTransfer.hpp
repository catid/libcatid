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

#ifndef CAT_SPHYNX_FILE_TRANSFER_HPP
#define CAT_SPHYNX_FILE_TRANSFER_HPP

#include <cat/sphynx/Transport.hpp>
#include <cat/fec/Wirehair.hpp>
#include <vector>
#include <queue> // priority_queue<>

namespace cat {


namespace sphynx {


enum TransferAbortReasons
{
	TXERR_FILE_OPEN_FAIL,	// Source unable to open the requested file
	TXERR_FILE_READ_FAIL,	// Source unable to read part of the requested file
	TXERR_FEC_FAIL,			// Forward error correction codec reported an error
};

enum TransferStatusFlags
{
	TXFLAG_LOADING,
	TXFLAG_READY,
	TXFLAG_SINGLE,	// Exceptional case where FEC cannot be used since the data fits in one datagram
};

#define CAT_MSS_TO_BLOCK_BYTES(mss) ( (mss) - 1 - 1 - 3 )

class CAT_EXPORT FECHugeSource : public IHugeSource
{
	static const u32 OVERHEAD = 1 + 1 + 3; // HDR(1) + IOP_HUGE|STREAM(1) + ID(3)
	static const u32 CHUNK_TARGET_LEN = 4000000; // 4 MB

protected:
	Transport *_transport;
	u32 _read_bytes;
	u32 _worker_id;

	volatile u32 _abort_reason;	// If non-zero: Abort transfer with this reason

	AsyncFile *_file;
	u64 _file_size;

	struct Stream
	{
		volatile u32 ready_flag;
		u8 *read_buffer;
		u8 *compress_buffer;
		ReadBuffer read_buffer_object;
		wirehair::Encoder encoder;
		u32 next_id;
		u32 mss;
		u32 compress_bytes;
		int requested;
	} *_streams;

	u32 _load_stream;	// Indicates which stream is currently pending on a file read
	u32 _dom_stream;	// Indicates which stream is dominant on the network
	u32 _num_streams;	// Number of streams that can be run in parallel

	bool Setup();
	void Cleanup();

	bool PostPart(u32 stream_id);

	CAT_INLINE bool StartRead(u32 stream, u64 offset, u32 bytes)
	{
		return _file->Read(&_streams[stream].read_buffer_object, offset, _streams[stream].read_buffer, bytes);
	}

	void OnFileRead(ThreadLocalStorage &tls, const BatchSet &set);

public:
	FECHugeSource();
	virtual ~FECHugeSource();

	void Initialize(Transport *transport, u32 worker_id);

	bool Start(const char *file_path);

	void NextHuge(s32 &available);
};


class CAT_EXPORT FECHugeSink : public IHugeSink
{
	Transport *_transport;
	u32 _worker_id;
	AsyncFile *_file;
	wirehair::Decoder *_decoder[2];
	WriteBuffer *_write_buffers[2];
	void *_data_buffers[2];

public:
	FECHugeSink();
	virtual ~FECHugeSink();

	void Initialize(Transport *transport, u32 worker_id);

	bool Start(const char *file_path);

	void OnHuge(u8 *data, u32 bytes);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_FILE_TRANSFER_HPP
