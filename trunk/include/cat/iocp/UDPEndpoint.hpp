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

#ifndef CAT_IOCP_UDP_ENDPOINT_HPP
#define CAT_IOCP_UDP_ENDPOINT_HPP

#include <cat/net/Sockets.hpp>
#include <cat/lang/RefObject.hpp>
#include <cat/mem/IAllocator.hpp>

/*
	To get maximum performance from the UDP sockets, be sure to adjust your
	registry settings:

	HKLM\System\CurrentControlSet\Services\Afd\Parameters
	ADD REG_DWORD KEY FastSendDatagramThreshold
	Set it to 1500
*/

/*
	ICMP Unreachable

	When an ICMP unreachable message arrives, it will cause a read to
	complete with zero data bytes.

	This is indistinguishable from a socket close event.  A server
	will not want to accept unreachable messages.
*/

// TODO: Disabled this for now to simplify a bug hunt
//#define CAT_NOBUFFER_FAILSAFE /* Attempt to post more reads if there are none */

namespace cat {


class IOLayer;
struct RecvBuffer;
struct SendBuffer;


// Number of IO outstanding on a UDP endpoint
static const u32 UDP_SIMULTANEOUS_READS = 128;
static const u32 UDP_READ_POST_LIMIT = 8;


// Object that represents a UDP endpoint bound to a single port
class CAT_EXPORT UDPEndpoint : public RefObject, public IOThreadsAssociator, public UDPSocket
{
	friend class IOThread;

	volatile u32 _buffers_posted; // Number of buffers posted to the socket waiting for data

	IOThreadPool *_pool;

	bool PostRead(RecvBuffer *buffer);
	u32 PostReads(s32 limit, s32 reuse_count = 0, BatchSet set = BatchSet(0, 0));

	void OnRecvCompletion(const BatchSet &buffers, u32 count);

public:
    UDPEndpoint();
    virtual ~UDPEndpoint();

	static const u32 RefObjectGUID = 0x00060001; // Global Unique IDentifier for acquiring RefObject singletons
	CAT_INLINE const char *GetRefObjectName() { return "UDPEndpoint"; }

	bool Initialize(Port port = 0, bool ignoreUnreachable = true, bool RequestIPv6 = true, bool RequireIPv4 = true, int kernelReceiveBufferBytes = 0);

	// If Is6() == true, the address must be promoted to IPv6
	// before calling using addr.PromoteTo6()
	// This function takes approximately 1 ms per ~20 buffers
	bool Write(const BatchSet &buffers, u32 count, const NetAddr &addr);

	CAT_INLINE bool Write(u8 *data, u32 data_bytes, const NetAddr &addr);

	// When done with read buffers, call this function to add them back to the available pool
	void ReleaseRecvBuffers(BatchSet buffers, u32 count);

protected:
	CAT_INLINE void SetRemoteAddress(RecvBuffer *buffer);

	virtual bool OnRefObjectInitialize();
	virtual void OnRefObjectDestroy();
	virtual bool OnRefObjectFinalize();

	virtual void OnRecvRouting(const BatchSet &buffers) = 0;
};


} // namespace cat

#endif // CAT_IOCP_UDP_ENDPOINT_HPP
