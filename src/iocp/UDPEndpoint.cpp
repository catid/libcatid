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

#include <cat/iocp/UDPEndpoint.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/Settings.hpp>
#include <cat/io/IOLayer.hpp>
#include <cat/io/Buffers.hpp>
#include <MSWSock.h>
using namespace std;
using namespace cat;

// Add missing definition for MinGW
#if !defined(SIO_UDP_CONNRESET)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
#endif

void UDPEndpoint::OnShutdownRequest()
{
	if (_socket != SOCKET_ERROR)
	{
		CloseSocket(_socket);
		_socket = SOCKET_ERROR;
	}
}

bool UDPEndpoint::OnZeroReferences()
{
	IOThreadPools::ref()->DissociatePrivate(_pool);

	return true;
}

UDPEndpoint::UDPEndpoint()
{
    _port = 0;
    _socket = SOCKET_ERROR;
	_pool = 0;
}

UDPEndpoint::~UDPEndpoint()
{
    if (_socket != SOCKET_ERROR)
        CloseSocket(_socket);
}

Port UDPEndpoint::GetPort()
{
	// Get bound port if it was random
	if (_port == 0)
	{
		_port = GetBoundPort(_socket);

		if (!_port)
		{
			CAT_WARN("UDPEndpoint") << "Unable to get own address: " << SocketGetLastErrorString();
			return 0;
		}
	}

	return _port;
}

bool UDPEndpoint::IgnoreUnreachable()
{
    // FALSE = Disable behavior where, after receiving an ICMP Unreachable message,
    // WSARecvFrom() will fail.  Disables ICMP completely; normally this is good.
    // But when you're writing a client endpoint, you probably want to listen to
    // ICMP Port Unreachable or other failures until you get the first packet.
    // After that call IgnoreUnreachable() to avoid spoofed ICMP exploits.

	if (_socket == SOCKET_ERROR)
		return false;

	DWORD dwBytesReturned = 0;
    BOOL bNewBehavior = FALSE;
    if (WSAIoctl(_socket, SIO_UDP_CONNRESET, &bNewBehavior,
				 sizeof(bNewBehavior), 0, 0, &dwBytesReturned, 0, 0) == SOCKET_ERROR)
	{
		CAT_WARN("UDPEndpoint") << "Unable to ignore ICMP Unreachable: " << SocketGetLastErrorString();
		return false;
	}

	return true;
}

bool UDPEndpoint::DontFragment(bool df)
{
	if (_socket == SOCKET_ERROR)
		return false;

	DWORD bNewBehavior = df ? TRUE : FALSE;
	if (setsockopt(_socket, IPPROTO_IP, IP_DONTFRAGMENT, (const char*)&bNewBehavior, sizeof(bNewBehavior)))
	{
		CAT_WARN("UDPEndpoint") << "Unable to change don't fragment bit: " << SocketGetLastErrorString();
		return false;
	}

	return true;
}

bool UDPEndpoint::Bind(bool onlySupportIPv4, Port port, bool ignoreUnreachable, int rcv_buffsize)
{
	// Create an unbound, overlapped UDP socket for the endpoint
    Socket s;
	if (!CreateSocket(SOCK_DGRAM, IPPROTO_UDP, true, s, onlySupportIPv4))
	{
		CAT_FATAL("UDPEndpoint") << "Unable to create a UDP socket: " << SocketGetLastErrorString();
		return false;
    }
	_ipv6 = !onlySupportIPv4;

	// Set SO_SNDBUF to zero for a zero-copy network stack (we maintain the buffers)
	int snd_buffsize = 0;
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&snd_buffsize, sizeof(snd_buffsize)))
	{
		CAT_WARN("UDPEndpoint") << "Unable to zero the send buffer: " << SocketGetLastErrorString();
		CloseSocket(s);
		return false;
	}

	// Set SO_RCVBUF as requested (often defaults are far too low for UDP servers or UDP file transfer clients)
	if (rcv_buffsize < 64000) rcv_buffsize = 64000;
	if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&rcv_buffsize, sizeof(rcv_buffsize)))
	{
		CAT_WARN("UDPEndpoint") << "Unable to setsockopt SO_RCVBUF " << rcv_buffsize << ": " << SocketGetLastErrorString();
		CloseSocket(s);
		return false;
	}

	_socket = s;

	// Ignore ICMP Unreachable
    if (ignoreUnreachable) IgnoreUnreachable();

    // Bind the socket to a given port
    if (!NetBind(s, port, onlySupportIPv4))
    {
        CAT_FATAL("UDPEndpoint") << "Unable to bind to port: " << SocketGetLastErrorString();
        CloseSocket(s);
        _socket = SOCKET_ERROR;
        return false;
    }

	_port = port;
	AddRef();
	_buffers_posted = 0;

	// Associate with IOThreadPools
	_pool = IOThreadPools::ref()->AssociatePrivate(this);
	if (!_pool)
	{
		CAT_FATAL("UDPEndpoint") << "Unable to associate with IOThreadPools";
		CloseSocket(s);
		_socket = SOCKET_ERROR;
		ReleaseRef(); // Release temporary references keeping the object alive until function returns
		return false;
	}

	// Now that we're in the IO layer, start watching the object for shutdown
	RefObjectWatcher::ref()->Watch(this);

	// If no reads could be posted,
	if (PostReads(UDP_SIMULTANEOUS_READS) == 0)
	{
		CAT_FATAL("UDPEndpoint") << "No reads could be launched";
		CloseSocket(s);
		_socket = SOCKET_ERROR;
		ReleaseRef(); // Release temporary reference keeping the object alive until function returns
		return false;
	}

    CAT_INFO("UDPEndpoint") << "Open on port " << GetPort();

	ReleaseRef(); // Release temporary reference keeping the object alive until function returns
    return true;
}


//// Begin Event

bool UDPEndpoint::PostRead(RecvBuffer *buffer)
{
	CAT_OBJCLR(buffer->iointernal.ov);
	buffer->iointernal.io_type = IOTYPE_UDP_RECV;
	buffer->iointernal.addr_len = sizeof(buffer->iointernal.addr);

	WSABUF wsabuf;
	wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(buffer) );
	wsabuf.len = IOTHREADS_BUFFER_READ_BYTES;

	// Queue up a WSARecvFrom()
	DWORD flags = 0, bytes;
	int result = WSARecvFrom(_socket, &wsabuf, 1, &bytes, &flags,
		reinterpret_cast<sockaddr*>( &buffer->iointernal.addr ),
		&buffer->iointernal.addr_len, &buffer->iointernal.ov, 0); 

	// This overlapped operation will always complete unless
	// we get an error code other than ERROR_IO_PENDING.
	if (result && WSAGetLastError() != ERROR_IO_PENDING)
	{
		CAT_FATAL("UDPEndpoint") << "WSARecvFrom error: " << SocketGetLastErrorString();
		return false;
	}

	return true;
}

u32 UDPEndpoint::PostReads(u32 limit, u32 reuse_count, BatchSet set)
{
	if (IsShutdown())
		return 0;

	// Check if there is a deficiency
	s32 count = (s32)(UDP_SIMULTANEOUS_READS - _buffers_posted);

	// Obey the read limit
	if (count > limit)
		count = limit;

	// If there is no deficiency,
	if (count <= 0)
		return 0;

	// If reuse count is more than needed,
	u32 acquire_count = 0, posted_reads = 0;

	if (reuse_count < count)
	{
		IAllocator *allocator = IOThreadPools::ref()->GetRecvAllocator();
		BatchSet allocated;

		// Acquire a batch of buffers
		u32 request_count = count - reuse_count;
		acquire_count = allocator->AcquireBatch(allocated, request_count);

		if (acquire_count != request_count)
		{
			CAT_WARN("UDPEndpoint") << "Only able to acquire " << acquire_count << " of " << request_count << " buffers";
		}

		set.PushBack(allocated);
	}

	// For each buffer,
	BatchHead *node;
	for (node = set.head; node && posted_reads < count; node = node->batch_next, ++posted_reads)
		if (!PostRead(reinterpret_cast<RecvBuffer*>( node )))
			break;

	// Increment the buffer posted count
	if (posted_reads > 0) Atomic::Add(&_buffers_posted, posted_reads);

	// If not all posts succeeded,
	if (posted_reads < count) CAT_WARN("UDPEndpoint") << "Not all read posts succeeded: " << posted_reads << " of " << count;

	// If nodes were unused,
	if (node)
	{
		IAllocator *allocator = IOThreadPools::ref()->GetRecvAllocator();

		set.head = node;
		allocator->ReleaseBatch(set);
	}

	// If posted reads exceed the re-use count,
	if (posted_reads > reuse_count)
	{
		// Add more references so the total matches the number of outstanding reads
		AddRef(posted_reads - reuse_count);
	}
	else if (reuse_count > posted_reads)
	{
		// Release references to subtract the deficit
		ReleaseRef(reuse_count - posted_reads);
	}

	return posted_reads;
}

bool UDPEndpoint::Write(const BatchSet &buffers, u32 count, const NetAddr &addr)
{
	NetAddr::SockAddr out_addr;
	int addr_len;

	// If in the process of shutdown or input invalid,
	if (IsShutdown() || !addr.Unwrap(out_addr, addr_len))
	{
		StdAllocator::ii->ReleaseBatch(buffers);
		return false;
	}

	u32 write_count = 0;

	AddRef(count);

	for (BatchHead *next, *node = buffers.head; node; node = next)
	{
		next = node->batch_next;
		SendBuffer *buffer = reinterpret_cast<SendBuffer*>( node );

		WSABUF wsabuf;
		wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(buffer) );
		wsabuf.len = buffer->GetBytes();

		CAT_OBJCLR(buffer->iointernal.ov);
		buffer->iointernal.io_type = IOTYPE_UDP_SEND;

		// Fire off a WSASendTo() and forget about it
		int result = WSASendTo(_socket, &wsabuf, 1, 0, 0,
			reinterpret_cast<const sockaddr*>( &out_addr ),
			addr_len, &buffer->iointernal.ov, 0);

		// This overlapped operation will always complete unless
		// we get an error code other than ERROR_IO_PENDING.
		if (result && WSAGetLastError() != ERROR_IO_PENDING)
		{
			CAT_WARN("UDPEndpoint") << "WSASendTo error: " << SocketGetLastErrorString();

			StdAllocator::ii->Release(node);
			ReleaseRef();
			continue;
		}

		++write_count;
	}

	PostReads(UDP_READ_POST_LIMIT);

	return count == write_count;
}

CAT_INLINE bool UDPEndpoint::Write(u8 *data, u32 data_bytes, const NetAddr &addr)
{
	SendBuffer *buffer = SendBuffer::Promote(data);
	buffer->SetBytes(data_bytes);
	return Write(buffer, 1, addr);
}

CAT_INLINE void UDPEndpoint::SetRemoteAddress(RecvBuffer *buffer)
{
	buffer->addr.Wrap(buffer->iointernal.addr);
}


//// Event Completion

void UDPEndpoint::ReleaseRecvBuffers(BatchSet buffers, u32 count)
{
	if (buffers.head)
		PostReads(UDP_READ_POST_LIMIT, count, buffers);
}

void UDPEndpoint::OnRecvCompletion(const BatchSet &buffers, u32 count)
{
	// Subtract the number of buffers completed from the total posted
	Atomic::Add(&_buffers_posted, 0 - count);

	// If reads completed during shutdown,
	if (IsShutdown())
	{
		IAllocator *allocator = IOThreadPools::ref()->GetRecvAllocator();

		// Just release the read buffers
		allocator->ReleaseBatch(buffers);

		ReleaseRef(count);

		return;
	}

	// Notify derived class about new buffers
	OnRecvRouting(buffers);

	// Post more reads
	PostReads(UDP_SIMULTANEOUS_READS);
}
