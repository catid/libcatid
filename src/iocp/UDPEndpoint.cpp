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

#include <cat/iocp/UDPEndpoint.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/Settings.hpp>
#include <cat/threads/Atomic.hpp>
#include <cat/iocp/SendBuffer.hpp>
using namespace std;
using namespace cat;


//// UDP Endpoint

void UDPEndpoint::OnShutdownRequest()
{
	if (_socket != SOCKET_ERROR)
	{
		CloseSocket(_socket);
		_socket = SOCKET_ERROR;
	}
}

void UDPEndpoint::OnZeroReferences()
{
	delete this;
}

UDPEndpoint::UDPEndpoint()
{
    _port = 0;
    _socket = SOCKET_ERROR;
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
			WARN("UDPEndpoint") << "Unable to get own address: " << SocketGetLastErrorString();
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
		WARN("UDPEndpoint") << "Unable to ignore ICMP Unreachable: " << SocketGetLastErrorString();
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
		WARN("UDPEndpoint") << "Unable to change don't fragment bit: " << SocketGetLastErrorString();
		return false;
	}

	return true;
}

bool UDPEndpoint::Bind(IOThreads *iothreads, bool onlySupportIPv4, Port port, bool ignoreUnreachable, int rcv_buffsize)
{
	// Create an unbound, overlapped UDP socket for the endpoint
    Socket s;
	if (!CreateSocket(SOCK_DGRAM, IPPROTO_UDP, true, s, onlySupportIPv4))
	{
		FATAL("UDPEndpoint") << "Unable to create a UDP socket: " << SocketGetLastErrorString();
		return false;
    }
	_ipv6 = !onlySupportIPv4;

	// Set SO_SNDBUF to zero for a zero-copy network stack (we maintain the buffers)
	int snd_buffsize = 0;
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&snd_buffsize, sizeof(snd_buffsize)))
	{
		WARN("UDPEndpoint") << "Unable to zero the send buffer: " << SocketGetLastErrorString();
		CloseSocket(s);
		return false;
	}

	// Set SO_RCVBUF as requested (often defaults are far too low for UDP servers or UDP file transfer clients)
	if (rcv_buffsize < 64000) rcv_buffsize = 64000;
	if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&rcv_buffsize, sizeof(rcv_buffsize)))
	{
		WARN("UDPEndpoint") << "Unable to setsockopt SO_RCVBUF " << rcv_buffsize << ": " << SocketGetLastErrorString();
		CloseSocket(s);
		return false;
	}

    _socket = s;

	// Ignore ICMP Unreachable
    if (ignoreUnreachable) IgnoreUnreachable();

    // Bind the socket to a given port
    if (!NetBind(s, port, onlySupportIPv4))
    {
        FATAL("UDPEndpoint") << "Unable to bind to port: " << SocketGetLastErrorString();
        CloseSocket(s);
        _socket = SOCKET_ERROR;
        return false;
    }

	// Associate with IOThreads
	if (!iothreads->Associate(this))
	{
		FATAL("UDPEndpoint") << "Unable to associate with IOThreads";
		CloseSocket(s);
		_socket = SOCKET_ERROR;
		return false;
	}

	// Put together a list of allocators to try
	IAllocator *allocators[2];
	allocators[0] = iothreads->GetAllocator();
	allocators[1] = StdAllocator::ii;

	// Post reads for this socket
	u32 read_count = PostReads(SIMULTANEOUS_READS, allocators, 2);

	// If no reads could be posted,
	if (read_count == 0)
	{
		FATAL("UDPEndpoint") << "No reads could be launched";
		CloseSocket(s);
		_socket = SOCKET_ERROR;
		return false;
	}
	else if (read_count < SIMULTANEOUS_READS)
	{
		WARN("UDPEndpoint") << "Only able to launch " << read_count << " reads out of " << SIMULTANEOUS_READS;
	}

	// Record the buffer deficiency (if any)
	_buffer_deficiency_lock.Enter();
	_buffers_posted = read_count;
	_buffer_deficiency_lock.Leave();

    _port = port;

    INFO("UDPEndpoint") << "Open on port " << GetPort();

    return true;
}


//// Begin Event

bool UDPEndpoint::PostRead(IOCPOverlappedRecvFrom *ov_recvfrom)
{
	CAT_OBJCLR(ov_recvfrom->ov);
	ov_recvfrom->allocator = allocator;
	ov_recvfrom->io_type = IOTYPE_UDP_RECV;
	ov_recvfrom->addr_len = sizeof(ov_recvfrom->addr);

	WSABUF wsabuf;
	wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(ov_recvfrom) );
	wsabuf.len = IOTHREADS_BUFFER_DATA_BYTES;

	// Queue up a WSARecvFrom()
	DWORD flags = 0, bytes;
	int result = WSARecvFrom(_socket, &wsabuf, 1, &bytes, &flags,
		reinterpret_cast<sockaddr*>( &buffer->addr ),
		&buffer->addr_len, &buffer->ov, 0); 

	// This overlapped operation will always complete unless
	// we get an error code other than ERROR_IO_PENDING.
	if (result && WSAGetLastError() != ERROR_IO_PENDING)
	{
		FATAL("UDPEndpoint") << "WSARecvFrom error: " << SocketGetLastErrorString();
		return false;
	}

	return true;
}

u32 UDPEndpoint::PostReads(u32 total_request_count, IAllocator *allocators[], u32 allocator_count)
{
	if (IsShutdown())
		return 0;

	// Add references for all the reads assuming it will work out
	AddRef(total_request_count);

	u32 read_count = 0;

	IAllocator *allocator = allocators[0];
	u32 allocator_index = 0;

	IOCPOverlappedRecvFrom *buffers[SIMULTANEOUS_READS];

	// Loop while more buffers are needed
	do 
	{
		u32 request_count = total_request_count;
		if (request_count > SIMULTANEOUS_READS) request_count = SIMULTANEOUS_READS;

		u32 buffer_count = allocator->AcquireBatch((void**)buffers, request_count, IOTHREADS_BUFFER_TOTAL_BYTES);

		// For each allocated buffer,
		for (u32 ii = 0; ii < buffer_count; ++ii)
		{
			// Fill buffer
			ov_recvfrom->allocator = allocator;

			// If unable to post a read,
			if (!PostRead(allocator, buffers[ii]))
			{
				// Give up and release this buffer and the remaining ones
				allocator->ReleaseBatch(buffers + ii, buffer_count - ii);

				break;
			}

			// Another successful read
			++read_count;
		}

		// If this allocator did not return all the requested buffers,
		if (buffer_count < request_count)
		{
			// Try the next allocator
			allocator_index++;

			// If no additional allocators exist,
			if (allocator_index >= allocator_count)
				break;

			// Select next allocator
			allocator = allocators[allocator_index];
		}
	} while (read_count < total_request_count);

	// If not all buffer requests could be filled,
	if (read_count < total_request_count)
	{
		// Release references that were held for them
		ReleaseRef(total_request_count - read_count);
	}

	// Return number of reads that succeeded
	return read_count;
}

bool UDPEndpoint::Write(const NetAddr &addr, u8 *data, u32 data_bytes)
{
	SendBuffer *buffer = SendBuffer::Promote(data);

	if (IsShutdown())
	{
		buffer->Release();
		return false;
	}

	// Unwrap NetAddr object to something sockaddr-compatible
	NetAddr::SockAddr out_addr;
	int addr_len;
	if (!addr.Unwrap(out_addr, addr_len))
	{
		buffer->Release();
		return false;
	}

	// Add a reference to this object until the write completes
	AddRef();

	WSABUF wsabuf;
	wsabuf.buf = reinterpret_cast<CHAR*>( data );
	wsabuf.len = data_bytes;

	buffer->Reset();

	// Fire off a WSASendTo() and forget about it
	int result = WSASendTo(_socket, &wsabuf, 1, 0, 0,
		reinterpret_cast<const sockaddr*>( &out_addr ),
		addr_len, &buffer->ov, 0);

	// This overlapped operation will always complete unless
	// we get an error code other than ERROR_IO_PENDING.
	if (result && WSAGetLastError() != ERROR_IO_PENDING)
	{
		WARN("UDPEndpoint") << "WSASendTo error: " << SocketGetLastErrorString();
		buffer->Release();
		ReleaseRef();
		return false;
	}

	return true;
}


//// Event Completion

void UDPEndpoint::ReturnReadBuffers(IOCPOverlappedRecvFrom *ov_recvfrom[], u32 count)
{
	u32 buffers_posted = 0;

	if (_buffers_posted < SIMULTANEOUS_READS)
	{
		s32 buffers_to_post;

		// Calculate number of buffers to fill in
		_buffer_deficiency_lock.Enter();
		buffers_to_post = SIMULTANEOUS_READS - _buffers_posted;
		if (buffers_to_post > 0)
		{
			if (buffers_to_post > count) buffers_to_post = count;
			_buffers_posted += buffers_to_post;
		}
		_buffer_deficiency_lock.Leave();

		// For each buffer to post,
		for (u32 ii = 0; ii < buffers_to_post; ++ii)
		{
			// If post fails,
			if (!PostRead(ov_recvfrom[ii]))
			{
				break;
			}

			++buffers_posted;
		}
	}

	// TODO: Release refs

	// Release one reference for each buffer
	ReleaseRef(count);

	// TODO: Release buffers here
}

void UDPEndpoint::OnRead(IOTLS *tls, IOCPOverlappedRecvFrom *ov_recvfrom[], u32 bytes[], u32 count, u32 event_time)
{
	// Subtract the number of completed buffers from the post count
	_buffer_deficiency_lock.Enter();
	_buffers_posted -= count;
	_buffer_deficiency_lock.Leave();

	if (IsShutdown())
		return true; // Delete buffer

	// TODO: WorkerThreads interface here

	// TODO: Handle errors here

/*
	switch (error)
	{
	case 0:
	case ERROR_MORE_DATA: // Truncated packet
		OnRead(ov_rf, bytes, event_time);
		break;

	case ERROR_NETWORK_UNREACHABLE:
	case ERROR_HOST_UNREACHABLE:
	case ERROR_PROTOCOL_UNREACHABLE:
	case ERROR_PORT_UNREACHABLE:
		// ICMP Errors:
		// These can be easily spoofed and should never be used to terminate a protocol.
		// This callback should be ignored after the first packet is received from the remote host.
		OnUnreachable(ov_rf->addr);
	}
*/

	// Post enough reads to fill in for the number that just completed
	u32 posted_reads = PostReads(count, tls->allocators, IOTLS_NUM_ALLOCATORS);

	// Replace missing reads with the posted ones
	_buffer_deficiency_lock.Enter();
	_buffers_posted += posted_reads;
	_buffer_deficiency_lock.Leave();
}
