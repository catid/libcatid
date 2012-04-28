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

/*
	File Transfer Header Format

	<--- LSB      MSB --->
	0 1 2|3|4|5 6|7|0 1|2 3 4 5 6|7| 0 1 2 3 4 5 6 7 | 0 1 2 3 4 5 6 7 | 0 1 2 3 4 5 6 7
	-----|-|-|---|-|---|---------|-|-----------------|-----------------|----------------
	 BLO |I|R|SOP|C|IOP|STREAM_ID|X| A A A A A A A A | B B B B B B B B | C C C C C C C C

	BLO = 1 (arbitrary)
	I = 1, R = 0 (indicating message takes whole datagram)
	SOP = INTERNAL
	C = 0
	IOP = HUGE
	STREAM_ID = Which concurrent FEC stream it corresponds to 0..31
	X = 0:No effect, 1:Part C of ID is implicitly zero and is not sent
	ID = C | B | A (high to low)

	Block data is appended to the header, and should be produced by Wirehair FEC.
*/

namespace cat {


namespace sphynx {


enum TransferOpCodes
{
	// Transmitter requesting to push a file, will cause receiver to issue a pull request
	TOP_PUSH_REQUEST,	// HDR | FileName(X)

	// Receiver requesting to pull a file
	TOP_PULL_REQUEST,	// HDR | FileName(X)

	// Transmitter indicating start of a file transfer
	TOP_PULL_GO,		// HDR | FileBytes(8) | StreamCount(1)

	// Deny a push or pull request with a reason
	TOP_DENY,			// HDR | Reason(1)

	// Transmitter notifying receiver that a stream is starting
	TOP_START,			// HDR | FileOffset(8) | ChunkDecompressedSize(4) | ChunkCompressedSize(4) | BlockBytes(2) | StreamID(1)

	// Receiver notifying transmitter that it is ready to receive a stream
	TOP_START_ACK,		// HDR | StreamID(1)

	// Adjust transmit rate on all streams
	TOP_RATE,			// HDR | RateCounter(4)

	// Request a number of blocks on a stream
	TOP_REQUEST,		// HDR | StreamID(1) | RequestCount(2)

	// Close transfer and indicate reason (including success)
	TOP_CLOSE,			// HDR | Reason(1)
};

/*
	File Transfer Protocol

	Example:

	c2s TOP_PUSH_REQUEST "ThisIsSparta.txt"
	s2c TOP_PULL_REQUEST "ThisIsSparta.txt"
	c2s TOP_PULL_GO (4,650,000 bytes) (4 streams)
	c2s TOP_START (offset 0) (4,000,000 bytes decompressed) (3,500,000 bytes compressed) (1398 bytes per block) (stream 0)
	s2c TOP_START_ACK (stream 0)
	s2c TOP_RATE (50 KB/s)
	c2s <HUGE DATA TRANSFER HERE>
	s2c TOP_RATE (70 KB/s)
	c2s <HUGE DATA TRANSFER HERE>
	s2c TOP_RATE (100 KB/s)
	c2s <HUGE DATA TRANSFER HERE>
	s2c TOP_RATE (120 KB/s)
	c2s <HUGE DATA TRANSFER HERE>
	c2s TOP_START (offset 4,000,000) (650,000 bytes decompressed) (420,000 bytes compressed) (1398 bytes per block) (stream 1)
	s2c TOP_START_ACK (stream 1)
	s2c TOP_REQUEST (stream 0) (requesting 2 more blocks)
	c2s <HUGE DATA TRANSFER HERE>
	s2c TOP_CLOSE (success code!)
*/

enum TransferAbortReasons
{
	TXERR_NO_PROBLEMO,		// OK

	TXERR_BUSY,				// Source is not idle and cannot service another request
	TXERR_REJECTED,			// Source rejected the request based on file name
	TXERR_FILE_OPEN_FAIL,	// Source unable to open the requested file
	TXERR_FILE_READ_FAIL,	// Source unable to read part of the requested file
	TXERR_FEC_FAIL,			// Forward error correction codec reported an error
	TXERR_OUT_OF_MEMORY,	// Source ran out of memory
	TXERR_USER_ABORT,		// Closed by user
	TXERR_SHUTDOWN,			// Remote host is shutting down
};

const char *GetTransferAbortReasonString(int reason);

enum TransferStatusFlags
{
	TXFLAG_LOADING,
	TXFLAG_READY,
	TXFLAG_SINGLE,	// Exceptional case where FEC cannot be used since the data fits in one datagram
};

static const u32 FT_STREAM_ID_SHIFT = 2;
static const u32 FT_STREAM_ID_MASK = 31;
static const u32 FT_COMPRESS_ID_MASK = 0x80;
static const u32 FT_MAX_HEADER_BYTES = 1 + 1 + 3;

#define CAT_FT_MSS_TO_BLOCK_BYTES(mss) ( (mss) - FT_MAX_HEADER_BYTES )


/*
	FECHugeEndpoint
*/
class CAT_EXPORT FECHugeEndpoint : public IHugeEndpoint
{
public:
	// Delegate types:

	// Return true to accept the file request (may still fail if file is not accessible)
	typedef Delegate1<bool, const char * /*file name*/> OnSendRequest;

	// Callback when file transfer completes, either with success or failure (check reason parameter)
	typedef Delegate1<void, int /*reason*/> OnSendDone;

	// Return true to accept the file request (may still fail if file is not accessible)
	typedef Delegate1<bool, const char * /*file name*/> OnRecvRequest;

	// Callback when file transfer completes, either with success or failure (check reason parameter)
	typedef Delegate1<void, int /*reason*/> OnRecvDone;

protected:
	static const u32 OVERHEAD = 1 + 1 + 3; // HDR(1) + IOP_HUGE|STREAM(1) + ID(3)
	static const u32 CHUNK_TARGET_LEN = 4000000; // 4 MB

	enum TransferState
	{
		TXS_IDLE,

		TXS_PULLING,
		TXS_PUSHING,
	} _state;

	Transport *_transport;
	u32 _read_bytes;
	u8 _opcode;

	OnSendRequest _on_send_request;
	OnSendDone _on_send_done;
	OnRecvRequest _on_recv_request;
	OnRecvDone _on_recv_done;

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

	volatile u32 _load_stream;	// Indicates which stream is currently pending on a file read
	u32 _dom_stream;	// Indicates which stream is dominant on the network
	u32 _num_streams;	// Number of streams that can be run in parallel

	bool Setup();
	void Cleanup();

	bool PostPart(u32 stream_id, BatchSet &buffers, u32 &count);

	CAT_INLINE bool StartRead(u32 stream, u64 offset, u32 bytes)
	{
		return _file->Read(&_streams[stream].read_buffer_object, offset, _streams[stream].read_buffer, bytes);
	}

	void OnFileRead(ThreadLocalStorage &tls, const BatchSet &set);

	bool PostPushRequest(const char *file_path);
	bool PostPullRequest(const char *file_path);
	bool PostPullGo(u64 file_bytes, int stream_count);
	bool PostDeny(int reason);
	bool PostStart(u64 file_offset, u32 chunk_decompressed_size, u32 chunk_compressed_size, int block_bytes, int stream_id);
	bool PostStartAck(int stream_id);
	bool PostRate(u32 rate_counter);
	bool PostRequest(int stream_id, int request_count);
	bool PostClose(int reason);

	void OnPushRequest(const char *file_path);
	void OnPullRequest(const char *file_path);
	void OnPullGo(u64 file_bytes, int stream_count);
	void OnDeny(int reason);
	void OnStart(u64 file_offset, u32 chunk_decompressed_size, u32 chunk_compressed_size, int block_bytes, int stream_id);
	void OnStartAck(int stream_id);
	void OnRate(u32 rate_counter);
	void OnRequest(int stream_id, int request_count);
	void OnClose(int reason);

protected:
	// Returns true if has more data to send
	bool HasData();

	// Called by Transport layer when more data can be sent
	s32 NextHuge(s32 available, BatchSet &buffers, u32 &count);

	// On IOP_HUGE message arrives
	void OnHuge(u8 *data, u32 bytes);

public:
	FECHugeEndpoint();
	virtual ~FECHugeEndpoint();

	// Initialize the endpoint
	void Initialize(Transport *transport, u8 opcode);

	CAT_INLINE void SetSendCallbacks(const OnSendRequest &on_send_request, const OnSendDone &on_send_done)
	{
		_on_send_request = on_send_request;
		_on_send_done = on_send_done;
	}

	CAT_INLINE void SetRecvCallbacks(const OnRecvRequest &on_recv_request, const OnRecvDone &on_recv_done)
	{
		_on_recv_request = on_recv_request;
		_on_recv_done = on_recv_done;
	}

	// Pass in everything including the message opcode
	void OnControlMessage(u8 *data, u32 bytes);

	// Request a file from the remote host
	// May fail if a transfer is already in progress
	bool Request(const char *file_path);

	// Start sending the specified file
	// May fail if a transfer is already in progress
	bool Send(const char *file_path);

	// Abort an existing file transfer
	CAT_INLINE void Abort(int reason = TXERR_USER_ABORT) { _abort_reason = reason; }
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_FILE_TRANSFER_HPP
