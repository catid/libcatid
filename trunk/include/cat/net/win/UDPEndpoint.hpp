/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_UDP_ENDPOINT_HPP
#define CAT_UDP_ENDPOINT_HPP

/*
	Windows version of thread pool sockets with IO Completion Ports

	Included from <cat/net/ThreadPoolSockets.hpp>
	Do not include directly
*/

namespace cat {


//// Overlapped Sockets

// WSARecvFrom() OVERLAPPED structure
struct RecvFromOverlapped
{
	TypedOverlapped tov;

	// Not necessarily and IPv6 address,
	// but we allocate enough space for one
    int addrLen;
    sockaddr_in6 addr;

	// data follows...

    void Reset();
};


/*
    class UDPEndpoint

    Object that represents a UDP endpoint bound to a single port
*/
class UDPEndpoint : public ThreadRefObject
{
    friend class ThreadPool;

public:
    UDPEndpoint();
    virtual ~UDPEndpoint();

    bool Valid();
    Port GetPort();

	// Is6() result is only valid AFTER Bind()
	CAT_INLINE bool Is6() { return _ipv6; }

    // For servers: Bind() with ignoreUnreachable = true ((default))
    // For clients: Bind() with ignoreUnreachable = false and call this
    //              after the first packet from the server is received.
    bool IgnoreUnreachable();

    void Close(); // Invalidates this object
    bool Bind(Port port = 0, bool ignoreUnreachable = true);
    bool QueueWSARecvFrom();

	// If Is6() == true, the address must be promoted to IPv6
	// before calling Post() with addr.PromoteTo6()
	bool Post(const NetAddr &addr, void *data, u32 bytes);

protected:
    virtual void OnRead(ThreadPoolLocalStorage *tls, const NetAddr &addr, u8 *data, u32 bytes) = 0; // false = close
    virtual void OnWrite(u32 bytes) = 0;
    virtual void OnClose() = 0;
    virtual void OnUnreachable(const NetAddr &addr) {} // Only IP is valid

private:
    Socket _socket;
    Port _port;
    volatile u32 _closing;
	bool _ipv6;

private:
    bool QueueWSARecvFrom(RecvFromOverlapped *recvOv);
    void OnWSARecvFromComplete(ThreadPoolLocalStorage *tls, int error, RecvFromOverlapped *recvOv, u32 bytes);

    bool QueueWSASendTo(const NetAddr &addr, TypedOverlapped *sendOv, u32 bytes);
    void OnWSASendToComplete(int error, u32 bytes);
};


} // namespace cat

#endif // CAT_UDP_ENDPOINT_HPP
