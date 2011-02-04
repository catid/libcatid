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
using namespace std;
using namespace cat;

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
	return true;
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

	_iothreads = iothreads;

	// Associate with IOThreads
	if (!iothreads->Associate(this))
	{
		FATAL("UDPEndpoint") << "Unable to associate with IOThreads";
		CloseSocket(s);
		_socket = SOCKET_ERROR;
		return false;
	}

	// Add references for all the reads assuming they will succeed
	AddRef(SIMULTANEOUS_READS);
	_buffers_posted = SIMULTANEOUS_READS;

	// Post reads for this socket
	u32 read_count = PostReads(SIMULTANEOUS_READS);

	// There are now other threads working on this object before we return!
	// TODO: Do not allow object to be deleted before we return...

	// Release references that were held for unsuccessful reads
	u32 read_fails = SIMULTANEOUS_READS - read_count;
	if ((s32)read_fails > 0)
	{
		ReleaseRef(read_fails);
		Atomic::Add(&_buffers_posted, 0 - read_fails);
		WARN("UDPEndpoint") << "Only able to launch " << read_count << " reads out of " << SIMULTANEOUS_READS;
	}

	// If no reads could be posted,
	if (read_count == 0)
	{
		FATAL("UDPEndpoint") << "No reads could be launched";
		CloseSocket(s);
		_socket = SOCKET_ERROR;
		return false;
	}

    _port = port;

    INFO("UDPEndpoint") << "Open on port " << GetPort();

    return true;
}


//// Begin Event

bool UDPEndpoint::PostRead(RecvBuffer *buffer)
{
	CAT_OBJCLR(buffer->iocp.ov);
	buffer->iocp.io_type = IOTYPE_UDP_RECV;
	buffer->iocp.addr_len = sizeof(buffer->iocp.addr);

	WSABUF wsabuf;
	wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(buffer) );
	wsabuf.len = IOTHREADS_BUFFER_DATA_BYTES;

	// Queue up a WSARecvFrom()
	DWORD flags = 0, bytes;
	int result = WSARecvFrom(_socket, &wsabuf, 1, &bytes, &flags,
		reinterpret_cast<sockaddr*>( &buffer->iocp.addr ),
		&buffer->iocp.addr_len, &buffer->iocp.ov, 0); 

	// This overlapped operation will always complete unless
	// we get an error code other than ERROR_IO_PENDING.
	if (result && WSAGetLastError() != ERROR_IO_PENDING)
	{
		FATAL("UDPEndpoint") << "WSARecvFrom error: " << SocketGetLastErrorString();
		return false;
	}

	return true;
}

u32 UDPEndpoint::PostReads(u32 count)
{
	if (IsShutdown())
		return 0;

	IAllocator *allocator = _iothreads->GetAllocator();

	BatchSet set;
	allocator->AcquireBatch(set, count, IOTHREADS_BUFFER_TOTAL_BYTES);
	u32 read_count = 0;

	for (BatchHead *node = set.head; node; node = node->batch_next, ++read_count)
	{
		if (!PostRead(reinterpret_cast<RecvBuffer*>( node )))
		{
			// Give up and release this buffer and the remaining ones
			set.head = node;
			allocator->ReleaseBatch(set);
			break;
		}
	}

	return read_count;
}

bool UDPEndpoint::Write(const BatchSet &buffers, const NetAddr &addr)
{
	NetAddr::SockAddr out_addr;
	int addr_len;

	// If in the process of shutdown or input invalid,
	if (IsShutdown() || !addr.Unwrap(out_addr, addr_len))
	{
		// Release all the buffers as a batch, since their allocators are all the same
		StdAllocator::ii->ReleaseBatch(buffers);
		return false;
	}

	WSABUF wsabufs[SIMULTANEOUS_SENDS];
	u32 ii = 0;
	BatchHead *node = buffers.head, *first = node;
	bool success = true;

	// While there are more buffers to send,
	for (;;)
	{
		BatchHead *next = node->batch_next;

		// Write node to buffer array
		SendBuffer *send_buf = reinterpret_cast<SendBuffer*>( node );
		wsabufs[ii].buf = reinterpret_cast<CHAR*>( GetTrailingBytes(send_buf) );
		wsabufs[ii].len = send_buf->_data_bytes;

		// If that was the end of what we can fit, or there is no more data,
		if (++ii >= SIMULTANEOUS_SENDS || !next)
		{
			AddRef();

			// Unlink these buffers from the rest of the batch
			node->batch_next = 0;

			SendBuffer *first_buf = reinterpret_cast<SendBuffer*>( first );

			// Fire off a WSASendTo() and forget about it
			int result = WSASendTo(_socket, wsabufs, ii, 0, 0,
								   reinterpret_cast<const sockaddr*>( &out_addr ),
								   addr_len, &first_buf->iocp.ov, 0);

			// This overlapped operation will always complete unless
			// we get an error code other than ERROR_IO_PENDING.
			if (result && WSAGetLastError() != ERROR_IO_PENDING)
			{
				WARN("UDPEndpoint") << "WSASendTo error: " << SocketGetLastErrorString();

				// Relink buffers to the rest of the batch
				node->batch_next = next;

				// Release the rest of the batch
				BatchSet set = { first, buffers.tail };
				StdAllocator::ii->ReleaseBatch(set);

				success = false;
				ReleaseRef();
				break;
			}

			// Stop here if there is no more data to send
			if (!next) break;

			// Start next batch over from here
			first = next;
			ii = 0;
		}

		node = next;
	}

	// If there are no read buffers posted on the socket,
	if (_buffers_posted == 0)
	{
		AddRef();

		// Post just one buffer to keep at least one request out there
		// Posting reads is expensive so only do this if we are desperate
		if (PostReads(1))
		{
			// Increment the number of buffers posted
			Atomic::Add(&_buffers_posted, 1);

			WARN("UDPEndpoint") << "Write noticed reads were dry and posted one";
		}
		else
		{
			ReleaseRef();

			WARN("UDPEndpoint") << "Write noticed reads were dry but could not help";
		}
	}

	return success;
}


//// Event Completion

void UDPEndpoint::ReleaseReadBuffers(BatchSet buffers, u32 count)
{
	if (!buffers.head) return;

	u32 buffers_posted = 0;

	// If there are no buffers posted on the socket,
	if (_buffers_posted == 0 && !IsShutdown())
	{
		BatchHead *next = buffers.head->batch_next;

		RecvBuffer *buffer = reinterpret_cast<RecvBuffer*>( buffers.head );

		// Re-use just one buffer to keep at least one request out there
		// Posting reads is expensive so only do this if we are desperate
		if (PostRead(buffer))
		{
			// Increment the number of buffers posted and pull it out of the count
			Atomic::Add(&_buffers_posted, 1);

			WARN("UDPEndpoint") << "Release noticed reads were dry and reposted one";
		}
		else
		{
			WARN("UDPEndpoint") << "Release noticed reads were dry but could not help";
		}

		if (!next) return;

		buffers.head = next;
	}

	_iothreads->GetAllocator()->ReleaseBatch(buffers);

	// Release one reference for each buffer
	ReleaseRef(count);
}

void UDPEndpoint::OnReadCompletion(const BatchSet &buffers, u32 count)
{
	// If reads completed during shutdown,
	if (IsShutdown())
	{
		// Just release the read buffers
		ReleaseReadBuffers(buffers, count);
		return;
	}

	// Notify derived class about new buffers
	OnReadRouting(buffers);

	// Check if new posts need to be made
	u32 perceived_deficiency = SIMULTANEOUS_READS - _buffers_posted;
	if ((s32)perceived_deficiency > 0)
	{
		// Race to replenish the buffers
		u32 race_posted = Atomic::Add(&_buffers_posted, perceived_deficiency);
 
		// If we lost the race to replenish,
		if (race_posted >= SIMULTANEOUS_READS)
		{
			// Take it back
			Atomic::Add(&_buffers_posted, 0 - perceived_deficiency);
		}
		else
		{
			// Set new post count to the perceived deficiency
			count += perceived_deficiency;

			AddRef(perceived_deficiency);
		}
	}

	// Post enough reads to fill in
	u32 posted_reads = PostReads(count);

	// If not all posts succeeded,
	if (posted_reads < count)
	{
		// Subtract the number that failed
		Atomic::Add(&_buffers_posted, posted_reads - count);

		// Release references for the number that failed
		ReleaseRef(count - posted_reads);
	}
}
