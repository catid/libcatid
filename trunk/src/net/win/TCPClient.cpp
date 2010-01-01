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

#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/Settings.hpp>
#include <cat/threads/RegionAllocator.hpp>
#include <cat/threads/Atomic.hpp>
using namespace std;
using namespace cat;


//// TCP Client

TCPClient::TCPClient(int priorityLevel)
	: ThreadRefObject(priorityLevel)
{
    // Initialize to invalid socket
    _recvOv = 0;
    _socket = SOCKET_ERROR;
    _disconnecting = 0;
}

TCPClient::~TCPClient()
{
    if (_socket != SOCKET_ERROR)
        CloseSocket(_socket);

    // Release memory for the overlapped structure
    if (_recvOv)
        RegionAllocator::ii->Release(_recvOv);
}

bool TCPClient::ValidClient()
{
    return _socket != SOCKET_ERROR;
}

bool TCPClient::Connect(const NetAddr &remoteServerAddress)
{
    // Create an unbound, overlapped TCP socket for the listen port
	Socket s;
	bool ipv4;
	if (!CreateSocket(SOCK_STREAM, IPPROTO_TCP, true, s, ipv4))
	{
		FATAL("TCPClient") << "Unable to create a TCP socket: " << SocketGetLastErrorString();
		return false;
    }
	_ipv6 = !ipv4;

    // Set SO_SNDBUF to zero for a zero-copy network stack (we maintain the buffers)
    int buffsize = 0;
    if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&buffsize, sizeof(buffsize)))
    {
        FATAL("TCPClient") << "Unable to zero the send buffer: " << SocketGetLastErrorString();
        CloseSocket(s);
        return false;
    }

    // Bind the socket to a random port as required by ConnectEx()
    if (!NetBind(s, 0, ipv4))
    {
        FATAL("TCPClient") << "Unable to bind to port: " << SocketGetLastErrorString();
        CloseSocket(s);
        return false;
    }

    _socket = s;

    // Prepare to receive completions in the worker threads
    // Connect to server asynchronously
    if (!ThreadPool::ref()->Associate((HANDLE)s, this) ||
        !QueueConnectEx(remoteServerAddress))
    {
        CloseSocket(s);
        _socket = SOCKET_ERROR;
        return false;
    }

    return true;
}

void TCPClient::DisconnectServer()
{
    // Only allow disconnect to run once
    if (Atomic::Add(&_disconnecting, 1) == 0)
    {
        OnDisconnectFromServer();

        if (!QueueDisconnectEx())
            ReleaseRef();
    }
}

bool TCPClient::PostToServer(void *buffer, u32 bytes)
{
    // Recover the full overlapped structure from data pointer
    TypedOverlapped *sendOv = reinterpret_cast<TypedOverlapped*>(
		reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped) );

    sendOv->Set(OVOP_CLIENT_SEND);

    if (!QueueWSASend(sendOv, bytes))
    {
        RegionAllocator::ii->Release(sendOv);
        return false;
    }

    return true;
}


bool TCPClient::QueueConnectEx(const NetAddr &remoteServerAddress)
{
    // Get ConnectEx() interface
    GUID GuidConnectEx = WSAID_CONNECTEX;
    LPFN_CONNECTEX lpfnConnectEx;
    DWORD copied;

    if (WSAIoctl(_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx,
                 sizeof(GuidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx), &copied, 0, 0))
    {
        FATAL("TCPClient") << "Unable to get ConnectEx interface: " << SocketGetLastErrorString();
        return false;
    }

	// Unwrap NetAddr
	NetAddr::SockAddr addr_out;
	int addr_len;
	if (!remoteServerAddress.Unwrap(addr_out, addr_len, _ipv6))
	{
		FATAL("TCPClient") << "Unable to execute ConnectEx: Server address invalid";
		return false;
	}

    // Create a new overlapped structure for receiving
    TypedOverlapped *overlapped = AcquireBuffer<TypedOverlapped>();
    if (!overlapped)
    {
		FATAL("TCPClient") << "Unable to allocate a ConnectEx overlapped structure: Out of memory";
        return false;
    }
    overlapped->Set(OVOP_CONNECT_EX);

    AddRef();

    // Queue up a ConnectEx()
    BOOL result = lpfnConnectEx(_socket, reinterpret_cast<sockaddr*>( &addr_out ),
                                addr_len, 0, 0, 0, &overlapped->ov); 

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (!result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        FATAL("TCPClient") << "ConnectEx error: " << SocketGetLastErrorString();
        RegionAllocator::ii->Release(overlapped);
        ReleaseRef();
        return false;
    }

    return true;
}

void TCPClient::OnConnectExComplete(int error)
{
    if (_disconnecting)
        return;

    if (error)
    {
        DisconnectServer();
        return;
    }

    // Finish socket creation by updating the connection context
    if (setsockopt(_socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, 0, 0))
    {
        WARN("TCPClient") << "Unable to update connect socket context: " << SocketGetLastErrorString();
        DisconnectServer();
        return;
    }

    // Create a new overlapped structure for receiving
    _recvOv = AcquireBuffer<TypedOverlapped>(RECV_DATA_SIZE);
    if (!_recvOv)
    {
        FATAL("TCPClient") << "Unable to allocate a receive buffer: Out of memory";
        DisconnectServer();
        return;
    }
    _recvOv->Set(OVOP_CLIENT_RECV);

    // Notify the derived class that we connected
    OnConnectToServer();

    // Queue up a receive
    if (!QueueWSARecv())
        DisconnectServer();
}


bool TCPClient::QueueWSARecv()
{
    if (_disconnecting)
    {
        WARN("TCPClient") << "WSARecv ignored while _disconnecting";
        return false;
    }

    WSABUF wsabuf;
    wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(_recvOv) );
    wsabuf.len = RECV_DATA_SIZE;

    AddRef();

    // Queue up a WSARecv()
    DWORD flags = 0, bytes;
    int result = WSARecv(_socket, &wsabuf, 1, &bytes, &flags, &_recvOv->ov, 0); 

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        FATAL("TCPClient") << "WSARecv error: " << SocketGetLastErrorString();
        ReleaseRef();
        return false;
    }

    return true;
}

void TCPClient::OnWSARecvComplete(int error, u32 bytes)
{
    if (_disconnecting)
        return;

    if (error)
    {
        DisconnectServer();
        return;
    }

    // When WSARecv completes with no data, it indicates a graceful disconnect.
    if (!bytes || !OnReadFromServer(GetTrailingBytes(_recvOv), bytes))
        DisconnectServer();
    else
    {
        // Reset the overlapped structure so it can be re-used
        _recvOv->Reset();

        // Queue up the next receive
        if (!QueueWSARecv())
            DisconnectServer();
    }
}


bool TCPClient::QueueWSASend(TypedOverlapped *sendOv, u32 bytes)
{
    if (_disconnecting)
        return false;

    WSABUF wsabuf;
    wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(sendOv) );
    wsabuf.len = bytes;

    AddRef();

    // Fire off a WSASend() and forget about it
    int result = WSASend(_socket, &wsabuf, 1, 0, 0, &sendOv->ov, 0);

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        FATAL("TCPClient") << "WSASend error: " << SocketGetLastErrorString();
        ReleaseRef();
        // Does not destroy the buffer on error -- just returns false
        return false;
    }

    return true;
}

void TCPClient::OnWSASendComplete(int error, u32 bytes)
{
    if (_disconnecting)
        return;

    if (error)
    {
        DisconnectServer();
        return;
    }

    OnWriteToServer(bytes);
}


bool TCPClient::QueueDisconnectEx()
{
    // Get DisconnectEx() interface
    GUID GuidDisconnectEx = WSAID_DISCONNECTEX;
    LPFN_DISCONNECTEX lpfnDisconnectEx;
    DWORD copied;

    if (WSAIoctl(_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidDisconnectEx,
                 sizeof(GuidDisconnectEx), &lpfnDisconnectEx, sizeof(lpfnDisconnectEx),
				 &copied, 0, 0))
    {
        FATAL("TCPClient") << "Unable to get DisconnectEx interface: " << SocketGetLastErrorString();
        return false;
    }

    // Create a new overlapped structure for receiving
    TypedOverlapped *overlapped = AcquireBuffer<TypedOverlapped>();
    if (!overlapped)
    {
        FATAL("TCPClient") << "Unable to allocate a DisconnectEx overlapped structure: Out of memory";
        return false;
    }
    overlapped->Set(OVOP_CLIENT_CLOSE);

    AddRef();

    // Queue up a DisconnectEx()
    BOOL result = lpfnDisconnectEx(_socket, &overlapped->ov, 0, 0); 

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (!result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        FATAL("TCPClient") << "DisconnectEx error: " << SocketGetLastErrorString();
        RegionAllocator::ii->Release(overlapped);
        ReleaseRef();
        return false;
    }

    return true;
}

void TCPClient::OnDisconnectExComplete(int error)
{
    ReleaseRef();
}


//// TCPClientQueued

TCPClientQueued::TCPClientQueued(int priorityLevel)
	: TCPClient(priorityLevel)
{
    _queueBuffer = 0;
    //_queueBytes = 0;
    _queuing = true;
}

TCPClientQueued::~TCPClientQueued()
{
    if (_queueBuffer)
        ReleasePostBuffer(_queueBuffer);
}

bool TCPClientQueued::PostToServer(void *buffer, u32 bytes)
{
    // Try not to hold a lock if we can help it
    if (!_queuing)
        return TCPClient::PostToServer(buffer, bytes);

    AutoMutex lock(_queueLock);

    // Check to make sure we're still _queuing
    if (!_queuing)
    {
        lock.Release();
        return TCPClient::PostToServer(buffer, bytes);
    }

	// If queue buffer is already created,
    if (_queueBuffer)
    {
		// Attempt to resize it
        _queueBuffer = ResizePostBuffer(_queueBuffer, _queueBytes + bytes);
        memcpy((u8*)_queueBuffer + _queueBytes, buffer, bytes);
        _queueBytes += bytes;
        ReleasePostBuffer(buffer);
    }
    else
    {
        _queueBuffer = buffer;
        _queueBytes = bytes;
    }

    return true;
}

void TCPClientQueued::PostQueuedToServer()
{
    AutoMutex lock(_queueLock);

	// If queue buffer exists,
    if (_queueBuffer)
    {
        TCPClient::PostToServer(_queueBuffer, _queueBytes);
        _queueBuffer = 0;
    }

    _queuing = false;
}
