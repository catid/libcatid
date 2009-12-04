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
#include <cat/threads/ThreadPool.hpp>
#include <cat/io/Logging.hpp>
#include <cat/time/Clock.hpp>
#include <cat/io/Settings.hpp>
#include <cat/threads/RegionAllocator.hpp>
#include <cat/threads/Atomic.hpp>
#include <process.h>
#include <algorithm>
using namespace std;
using namespace cat;


#if defined (CAT_COMPILER_MSVC)
#pragma comment(lib, "ws2_32.lib")
#endif


// Amount of data to receive overlapped, tuned to exactly fit a
// 2048-byte buffer in the region allocator.
static const int RECV_DATA_SIZE = 2048 - sizeof(TypedOverlapped) - 8; // -8 for rebroadcast inflation

static const int RECVFROM_DATA_SIZE = 2048 - sizeof(RecvFromOverlapped) - 8;


//// Overlapped types

void AcceptExOverlapped::Set(SOCKET s)
{
    TypedOverlapped::Set(OVOP_ACCEPT_EX);
    acceptSocket = s;
}

void RecvFromOverlapped::Reset()
{
    CAT_OBJCLR(ov);
    addrLen = sizeof(addr);
}


//// Sockets

namespace cat
{
    std::string SocketGetLastErrorString()
    {
        return SocketGetErrorString(WSAGetLastError());
    }

    std::string SocketGetErrorString(int code)
    {
        switch (code)
        {
        case WSAEADDRNOTAVAIL:         return "[Address not available]";
        case WSAEADDRINUSE:            return "[Address is in use]";
        case WSANOTINITIALISED:        return "[Winsock not initialized]";
        case WSAENETDOWN:              return "[Network is down]";
        case WSAEINPROGRESS:           return "[Operation in progress]";
        case WSA_NOT_ENOUGH_MEMORY:    return "[Out of memory]";
        case WSA_INVALID_HANDLE:       return "[Invalid handle (programming error)]";
        case WSA_INVALID_PARAMETER:    return "[Invalid parameter (programming error)]";
        case WSAEFAULT:                return "[Fault]";
        case WSAEINTR:                 return "[WSAEINTR]";
        case WSAEINVAL:                return "[WSAEINVAL]";
        case WSAEISCONN:               return "[WSAEISCONN]";
        case WSAENETRESET:             return "[Network reset]";
        case WSAENOTSOCK:              return "[Parameter is not a socket (programming error)]";
        case WSAEOPNOTSUPP:            return "[Operation not supported (programming error)]";
        case WSAESOCKTNOSUPPORT:       return "[Socket type not supported]";
        case WSAESHUTDOWN:             return "[WSAESHUTDOWN]";
        case WSAEWOULDBLOCK:           return "[Operation would block (programming error)]";
        case WSAEMSGSIZE:              return "[WSAEMSGSIZE]";
        case WSAETIMEDOUT:             return "[Operation timed out]";
        case WSAECONNRESET:            return "[Connection reset (programming error)]";
        case WSAENOTCONN:              return "[Socket not connected (programming error)]";
        case WSAEDISCON:               return "[WSAEDISCON]";
        case ERROR_IO_PENDING:         return "[IO operation will complete in IOCP worker thread (programming error)]";
        case WSA_OPERATION_ABORTED:    return "[WSA_OPERATION_ABORTED]";
        case ERROR_CONNECTION_ABORTED: return "[Connection aborted locally (programming error)]";
        case ERROR_NETNAME_DELETED:    return "[Socket was already closed (programming error)]";
        case ERROR_PORT_UNREACHABLE:   return "[Destination port is unreachable]";
        case ERROR_MORE_DATA:          return "[More data is available]";
        };

        ostringstream oss;
        oss << "[Error code: " << code << " (0x" << hex << code << ")]";
        return oss.str();
    }

    string IPToString(IP ip)
    {
        in_addr addr;
        addr.S_un.S_addr = ip;
        return inet_ntoa(addr);
    }

    std::string IPToString(const sockaddr_in &addr)
    {
        return inet_ntoa(addr.sin_addr);
    }

    std::string PortToString(const sockaddr_in &addr)
    {
        ostringstream oss;
        oss << htons(addr.sin_port);
        return oss.str();
    }

    IP ResolveHostname(const char *hostname)
    {
        IP ip = inet_addr(hostname);

        if (ip == SOCKET_ERROR)
        {
            hostent *host = gethostbyname(hostname);

            if (host)
                ip = *((IP*)*(host->h_addr_list));
        }

        return ip;
    }

    u8 *GetPostBuffer(u32 bytes)
    {
        TypedOverlapped *sendOv = reinterpret_cast<TypedOverlapped*>(
			RegionAllocator::ii->Acquire(sizeof(TypedOverlapped) + bytes) );

		if (!sendOv)
        {
            FATAL("IOCPSockets") << "Unable to allocate a send buffer: Out of memory";
            return 0;
        }

        return GetTrailingBytes(sendOv);
    }

    void *ResizePostBuffer(void *buffer, u32 newBytes)
    {
        TypedOverlapped *sendOv = reinterpret_cast<TypedOverlapped*>(
			RegionAllocator::ii->Resize(
				reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped),
				sizeof(TypedOverlapped) + newBytes) );

        if (!sendOv)
        {
            FATAL("IOCPSockets") << "Unable to resize a send buffer: Out of memory";
            return 0;
        }

        return GetTrailingBytes(sendOv);
    }

    void ReleasePostBuffer(void *buffer)
    {
        RegionAllocator::ii->Release(
			reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped));
    }
}


//// TCPServer

TCPServer::TCPServer()
{
    _socket = SOCKET_ERROR;
}

TCPServer::~TCPServer()
{
    Close();
}

bool TCPServer::Bind(Port port)
{
    // Create an unbound, overlapped TCP socket for the listen port
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

    if (s == INVALID_SOCKET)
    {
        FATAL("TCPServer") << "Unable to create a TCP socket: " << SocketGetLastErrorString();
        return false;
    }

    // Set SO_SNDBUF to zero for a zero-copy network stack (we maintain the buffers)
    int buffsize = 0;
    if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&buffsize, sizeof(buffsize)))
    {
        FATAL("TCPServer") << "Unable to zero the send buffer: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Do not allow other applications to bind over us with SO_REUSEADDR
    int exclusive = TRUE;
    if (setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&exclusive, sizeof(exclusive)))
    {
        FATAL("TCPServer") << "Unable to get exclusive port: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Get AcceptEx() interface
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    DWORD copied;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx),
                 &_lpfnAcceptEx, sizeof(_lpfnAcceptEx), &copied, 0, 0))
    {
        FATAL("TCPServer") << "Unable to get AcceptEx interface: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Get GetAcceptExSockaddrs() interface
    GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs,
				 sizeof(GuidGetAcceptExSockAddrs), &_lpfnGetAcceptExSockAddrs,
				 sizeof(_lpfnGetAcceptExSockAddrs), &copied, 0, 0))
    {
        FATAL("TCPServer") << "Unable to get GetAcceptExSockAddrs interface: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Get DisconnectEx() interface
    GUID GuidDisconnectEx = WSAID_DISCONNECTEX;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidDisconnectEx,
				 sizeof(GuidDisconnectEx), &_lpfnDisconnectEx, sizeof(_lpfnDisconnectEx),
				 &copied, 0, 0))
    {
        FATAL("TCPServer") << "Unable to get DisconnectEx interface: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Bind socket to port
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s, (sockaddr*)&addr, sizeof(addr)))
    {
        FATAL("TCPServer") << "Unable to bind to port " << port << ": " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Listen on this socket
    if (listen(s, SOMAXCONN))
    {
        FATAL("TCPServer") << "Unable to listen on socket: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    _socket = s;

    // Prepare to receive completions in the worker threads
    // Queue a bunch of AcceptEx() calls
    if (!ThreadPool::ref()->Associate((HANDLE)s, this) ||
        !QueueAccepts())
    {
        Close();
        return false;
    }

    _port = port;

    INFO("TCPServer") << "Listening on port " << GetPort();

    return true;
}

bool TCPServer::ValidServer()
{
    return _socket != SOCKET_ERROR;
}

Port TCPServer::GetPort()
{
    // Get bound port if it was random
    if (_port == 0)
    {
        sockaddr_in addr;
        int namelen = sizeof(addr);
        if (getsockname(_socket, (sockaddr*)&addr, &namelen))
        {
            FATAL("TCPServer") << "Unable to get own address: " << SocketGetLastErrorString();
            return 0;
        }

        _port = ntohs(addr.sin_port);
    }

    return _port;
}

void TCPServer::Close()
{
    if (_socket != SOCKET_ERROR)
    {
        closesocket(_socket);
        _socket = SOCKET_ERROR;
    }
}


bool TCPServer::QueueAcceptEx()
{
    // Create an unbound overlapped TCP socket for AcceptEx()
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

    if (s == INVALID_SOCKET)
    {
        WARN("TCPServer") << "Unable to create an accept socket: " << SocketGetLastErrorString();
        return false;
    }

    // Create a new AcceptExOverlapped structure
    AcceptExOverlapped *overlapped = reinterpret_cast<AcceptExOverlapped*>(
		RegionAllocator::ii->Acquire(sizeof(AcceptExOverlapped)) );
    if (!overlapped)
    {
        WARN("TCPServer") << "Unable to allocate AcceptEx overlapped structure: Out of memory.";
        closesocket(s);
        return false;
    }
    overlapped->Set(s);

    // Queue up an AcceptEx()
    // AcceptEx will complete on the listen socket, not the socket
    // created above that accepts the connection.
    DWORD received;

    AddRef();

    BOOL result = _lpfnAcceptEx(_socket, s, &overlapped->addresses, 0,
							   sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
							   &received, &overlapped->ov);

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (!result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        WARN("TCPServer") << "AcceptEx error: " << SocketGetLastErrorString();
        closesocket(s);
        RegionAllocator::ii->Release(overlapped);
        ReleaseRef();
        return false;
    }

    return true;
}

bool TCPServer::QueueAccepts()
{
    u32 queueSize = Settings::ref()->getInt("TCPServer.AcceptQueueSize", 8);

    if (queueSize > 1000) queueSize = 1000;

    u32 queued = queueSize;
    while (QueueAcceptEx() && queueSize--);
    queued -= queueSize;

    if (!queued)
    {
        FATAL("TCPServer") << "error to pre-accept any connections: Server cannot accept connections";
        return false;
    }

    INFO("TCPServer") << "Queued " << queued << " pre-accepted connections";
    return true;
}

void TCPServer::OnAcceptExComplete(int error, AcceptExOverlapped *overlapped)
{
    if (error)
    {
        // ERROR_SEM_TIMEOUT     : This means a half-open connection has reset
        // ERROR_NETNAME_DELETED : This means a three-way handshake reset before completion
        if (error == ERROR_SEM_TIMEOUT || error == ERROR_NETNAME_DELETED)
        {
            // Queue up another AcceptEx to fill in for this one
            QueueAcceptEx();
        }

        return;
    }

    // Get local and remote socket addresses
    int localLen = 0, remoteLen = 0;
    sockaddr_in *local, *remote;
    _lpfnGetAcceptExSockAddrs(&overlapped->addresses, 0, sizeof(sockaddr_in)+16,
							 sizeof(sockaddr_in)+16, (sockaddr**)&local, &localLen,
							 (sockaddr**)&remote, &remoteLen);

    // Instantiate a server connection
    TCPServerConnection *conn = InstantiateServerConnection();
    if (!conn) return;

    // Pass the connection parameters to the connection instance for acceptance
    if (!conn->AcceptConnection(_socket, overlapped->acceptSocket, _lpfnDisconnectEx, local, remote))
        conn->ReleaseRef();

    // Queue up another AcceptEx to fill in for this one
    QueueAcceptEx();
}


//// TCPServerConnection

TCPServerConnection::TCPServerConnection()
{
    // Initialize to an invalid state.
    // Connection is invalid until AcceptConnection() runs successfully.
    _socket = SOCKET_ERROR;
    _recvOv = 0;
    _disconnecting = 0;
}

TCPServerConnection::~TCPServerConnection()
{
    if (_socket != SOCKET_ERROR)
        closesocket(_socket);

    // Release memory for the overlapped structure
    if (_recvOv)
        RegionAllocator::ii->Release(_recvOv);
}

bool TCPServerConnection::ValidServerConnection()
{
    return _socket != SOCKET_ERROR;
}

void TCPServerConnection::DisconnectClient()
{
    // Only allow disconnect to run once
    if (Atomic::Add(&_disconnecting, 1) == 0)
    {
        OnDisconnectFromClient();

        if (!QueueDisconnectEx())
        {
            // Release self-reference; will delete this object if no other
            // objects are maintaining a reference to this one.
            ReleaseRef();
        }
    }
}

bool TCPServerConnection::PostToClient(void *buffer, u32 bytes)
{
    // Recover the full overlapped structure from data pointer
    TypedOverlapped *sendOv = reinterpret_cast<TypedOverlapped*>(
		reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped) );

    sendOv->Set(OVOP_SERVER_SEND);

    if (!QueueWSASend(sendOv, bytes))
    {
        RegionAllocator::ii->Release(sendOv);
        return false;
    }

    return true;
}


bool TCPServerConnection::AcceptConnection(SOCKET listenSocket, SOCKET acceptSocket,
                                LPFN_DISCONNECTEX lpfnDisconnectEx,
                                sockaddr_in *acceptAddress, sockaddr_in *remoteClientAddress)
{
    // If we return false here this object will be deleted.

    // Store parameters
    _socket = acceptSocket;
    _lpfnDisconnectEx = lpfnDisconnectEx;

    // Finalize the accept socket context
    if (setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                    (char *)&listenSocket, sizeof(listenSocket)))
    {
        WARN("TCPServerConnection")
            << "Unable to update accept socket context: " << SocketGetLastErrorString();
        return false;
    }

    // Set SO_SNDBUF to zero for a zero-copy network stack (we maintain the buffers)
    int bufsize = 0;
    if (setsockopt(acceptSocket, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(bufsize)))
    {
        FATAL("TCPServerConnection") << "Unable to zero the send buffer: " << SocketGetLastErrorString();
        return false;
    }

    // Create a new overlapped structure for receiving
    _recvOv = reinterpret_cast<TypedOverlapped*>(
		RegionAllocator::ii->Acquire(sizeof(TypedOverlapped) + RECV_DATA_SIZE) );
    if (!_recvOv)
    {
        FATAL("TCPServerConnection") << "Unable to allocate a receive buffer: Out of memory";
        return false;
    }
    _recvOv->Set(OVOP_SERVER_RECV);

    // Prepare to receive completions in the worker threads.
    // Do this first so that if the server will send data immediately it
    // won't block or leak memory or fail or whatever would happen.
    if (!ThreadPool::ref()->Associate((HANDLE)acceptSocket, this))
        return false;

    // Return true past this point so connection object will not be deleted
    // and use DisconnectClient() to abort the connection instead now.

    // Let the derived class determine if the connection should be accepted
    if (!OnConnectFromClient(*remoteClientAddress))
        DisconnectClient();
    else
    {
        // Queue up a WSARecv() to accept data from the client.
        if (!QueueWSARecv())
            DisconnectClient();
    }

    return true;
}


bool TCPServerConnection::QueueWSARecv()
{
    if (_disconnecting)
        return false;

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
        FATAL("TCPServerConnection") << "WSARecv error: " << SocketGetLastErrorString();
        ReleaseRef();
        return false;
    }

    return true;
}

void TCPServerConnection::OnWSARecvComplete(int error, u32 bytes)
{
    if (_disconnecting)
        return;

    if (error)
    {
        DisconnectClient();
        return;
    }

    // When WSARecv completes with no data, it indicates a graceful disconnect.
    if (!bytes || !OnReadFromClient(GetTrailingBytes(_recvOv), bytes))
        DisconnectClient();
    else
    {
        // Reset the overlapped structure so it can be re-used
        _recvOv->Reset();

        // Queue up the next receive
        if (!QueueWSARecv())
            DisconnectClient();
    }
}


bool TCPServerConnection::QueueWSASend(TypedOverlapped *sendOv, u32 bytes)
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
        FATAL("TCPServerConnection") << "WSASend error: " << SocketGetLastErrorString();
        // Does not destroy the buffer on error -- just returns false
        ReleaseRef();
        return false;
    }

    return true;
}

void TCPServerConnection::OnWSASendComplete(int error, u32 bytes)
{
    if (_disconnecting)
        return;

    if (error)
    {
        DisconnectClient();
        return;
    }

    OnWriteToClient(bytes);
}


bool TCPServerConnection::QueueDisconnectEx()
{
    // Create a new overlapped structure for receiving
    TypedOverlapped *overlapped = reinterpret_cast<TypedOverlapped*>(
		RegionAllocator::ii->Acquire(sizeof(TypedOverlapped)) );
    if (!overlapped)
    {
        FATAL("TCPServerConnection") << "Unable to allocate a DisconnectEx overlapped structure: Out of memory";
        return false;
    }
    overlapped->Set(OVOP_SERVER_CLOSE);

    AddRef();

    // Queue up a DisconnectEx()
    BOOL result = _lpfnDisconnectEx(_socket, &overlapped->ov, 0, 0); 

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (!result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        FATAL("TCPServerConnection") << "DisconnectEx error: " << SocketGetLastErrorString();
        RegionAllocator::ii->Release(overlapped);
        ReleaseRef();
        return false;
    }

    return true;
}

void TCPServerConnection::OnDisconnectExComplete(int error)
{
    ReleaseRef();
}


//// TCP Client

TCPClient::TCPClient()
{
    // Initialize to invalid socket
    _recvOv = 0;
    _socket = SOCKET_ERROR;
    _disconnecting = 0;
}

TCPClient::~TCPClient()
{
    if (_socket != SOCKET_ERROR)
        closesocket(_socket);

    // Release memory for the overlapped structure
    if (_recvOv)
        RegionAllocator::ii->Release(_recvOv);
}

bool TCPClient::ValidClient()
{
    return _socket != SOCKET_ERROR;
}

bool TCPClient::ConnectToServer(const sockaddr_in &remoteServerAddress)
{
    // Create an unbound, overlapped TCP socket for the listen port
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

    if (s == INVALID_SOCKET)
    {
        FATAL("TCPClient") << "Unable to create a TCP socket: " << SocketGetLastErrorString();
        return false;
    }

    // Set SO_SNDBUF to zero for a zero-copy network stack (we maintain the buffers)
    int buffsize = 0;
    if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&buffsize, sizeof(buffsize)))
    {
        FATAL("TCPClient") << "Unable to zero the send buffer: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Bind the socket to a random port as required by ConnectEx()
    sockaddr_in connectAddress;
    connectAddress.sin_family = AF_INET;
    connectAddress.sin_addr.s_addr = INADDR_ANY;
    connectAddress.sin_port = 0;

    if (bind(s, (sockaddr*)&connectAddress, sizeof(connectAddress)))
    {
        FATAL("TCPClient") << "Unable to bind to port: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    _socket = s;

    // Prepare to receive completions in the worker threads
    // Connect to server asynchronously
    if (!ThreadPool::ref()->Associate((HANDLE)s, this) ||
        !QueueConnectEx(remoteServerAddress))
    {
        closesocket(s);
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


bool TCPClient::QueueConnectEx(const sockaddr_in &remoteServerAddress)
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

    // Create a new overlapped structure for receiving
    TypedOverlapped *overlapped = reinterpret_cast<TypedOverlapped*>(
		RegionAllocator::ii->Acquire(sizeof(TypedOverlapped)) );
    if (!overlapped)
    {
        FATAL("TCPClient") << "Unable to allocate a ConnectEx overlapped structure: Out of memory";
        return false;
    }
    overlapped->Set(OVOP_CONNECT_EX);

    AddRef();

    // Queue up a ConnectEx()
    BOOL result = lpfnConnectEx(_socket, (sockaddr*)&remoteServerAddress,
                                sizeof(remoteServerAddress), 0, 0, 0, &overlapped->ov); 

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
    _recvOv = reinterpret_cast<TypedOverlapped*>(
		RegionAllocator::ii->Acquire(sizeof(TypedOverlapped) + RECV_DATA_SIZE) );
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
    TypedOverlapped *overlapped = reinterpret_cast<TypedOverlapped*>(
		RegionAllocator::ii->Acquire(sizeof(TypedOverlapped)) );
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

TCPClientQueued::TCPClientQueued()
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

    if (_queueBuffer)
    {
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

    if (_queueBuffer)
    {
        TCPClient::PostToServer(_queueBuffer, _queueBytes);
        _queueBuffer = 0;
    }

    _queuing = false;
}


//// UDP Endpoint

UDPEndpoint::UDPEndpoint()
{
    _port = 0;
    _closing = 0;
    _socket = SOCKET_ERROR;
}

UDPEndpoint::~UDPEndpoint()
{
    if (_socket != SOCKET_ERROR)
        closesocket(_socket);
}

void UDPEndpoint::Close()
{
    // Only allow close to run once
    if (Atomic::Add(&_closing, 1) == 0)
    {
        if (_socket != SOCKET_ERROR)
        {
            closesocket(_socket);
            _socket = SOCKET_ERROR;
        }

		// Allow the library user to react to closure sooner than the destructor.
        OnClose();

		// Starts with reference count equal 1; so this puts the object in a state
		// where as soon as all of the references are extinguished it is deleted.
        ReleaseRef();
    }
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

bool UDPEndpoint::Bind(Port port, bool ignoreUnreachable)
{
    // Create an unbound, overlapped UDP socket for the endpoint
    SOCKET s = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0, 0, WSA_FLAG_OVERLAPPED);

    if (s == INVALID_SOCKET)
    {
        FATAL("UDPEndpoint") << "Unable to create a UDP socket: " << SocketGetLastErrorString();
        return false;
    }

    // Set SO_SNDBUF to zero for a zero-copy network stack (we maintain the buffers)
    int buffsize = 0;
    if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&buffsize, sizeof(buffsize)))
    {
        FATAL("UDPEndpoint") << "Unable to zero the send buffer: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    _socket = s;

	// Ignore ICMP Unreachable
    if (ignoreUnreachable) IgnoreUnreachable();

    // Bind the socket to a given port
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s, (sockaddr*)&addr, sizeof(addr)))
    {
        FATAL("UDPEndpoint") << "Unable to bind to port: " << SocketGetLastErrorString();
        closesocket(s);
        _socket = SOCKET_ERROR;
        return false;
    }

    // Prepare to receive completions in the worker threads
    if (!ThreadPool::ref()->Associate((HANDLE)s, this) ||
        !QueueWSARecvFrom())
    {
        closesocket(s);
        _socket = SOCKET_ERROR;
        return false;
    }

    _port = port;

    INFO("UDPEndpoint") << "Open on port " << GetPort();

    return true;
}

bool UDPEndpoint::Valid()
{
    return _socket != SOCKET_ERROR;
}

Port UDPEndpoint::GetPort()
{
    // Get bound port if it was random
    if (_port == 0)
    {
        sockaddr_in addr;
        int namelen = sizeof(addr);
        if (getsockname(_socket, (sockaddr*)&addr, &namelen))
        {
            FATAL("UDPEndpoint") << "Unable to get own address: " << SocketGetLastErrorString();
            return 0;
        }

        _port = ntohs(addr.sin_port);
    }

    return _port;
}

bool UDPEndpoint::Post(IP ip, Port port, void *buffer, u32 bytes)
{
	if (_closing)
		return false;

    // Recover the full overlapped structure from data pointer
    TypedOverlapped *sendOv = reinterpret_cast<TypedOverlapped*>(
		reinterpret_cast<u8*>(buffer) - sizeof(TypedOverlapped) );

    sendOv->Set(OVOP_SENDTO);

    if (!QueueWSASendTo(ip, port, sendOv, bytes))
    {
        RegionAllocator::ii->Release(sendOv);
        return false;
    }

    return true;
}

bool UDPEndpoint::QueueWSARecvFrom(RecvFromOverlapped *recvOv)
{
	if (_closing)
		return false;

	AddRef();

    recvOv->Reset();

    WSABUF wsabuf;
    wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(recvOv) );
    wsabuf.len = RECVFROM_DATA_SIZE;

    // Queue up a WSARecvFrom()
    DWORD flags = 0, bytes;
    int result = WSARecvFrom(_socket, &wsabuf, 1, &bytes, &flags,
							 reinterpret_cast<sockaddr*>( &recvOv->addr ),
							 &recvOv->addrLen, &recvOv->ov, 0); 

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        FATAL("UDPEndpoint") << "WSARecvFrom error: " << SocketGetLastErrorString();
		ReleaseRef();
        return false;
    }

    return true;
}

bool UDPEndpoint::QueueWSARecvFrom()
{
    if (_closing)
        return false;

    // Create a new RecvFromOverlapped structure for receiving
    RecvFromOverlapped *recvOv = reinterpret_cast<RecvFromOverlapped*>(
		RegionAllocator::ii->Acquire(sizeof(RecvFromOverlapped) + RECVFROM_DATA_SIZE) );
    if (!recvOv)
    {
        FATAL("UDPEndpoint") << "Unable to allocate a receive buffer: Out of memory";
        return false;
    }
    recvOv->opcode = OVOP_RECVFROM;

    if (!QueueWSARecvFrom(recvOv))
	{
		RegionAllocator::ii->Release(recvOv);
		return false;
	}

	return true;
}

void UDPEndpoint::OnWSARecvFromComplete(int error, RecvFromOverlapped *recvOv, u32 bytes)
{
    switch (error)
    {
    case 0:
    case ERROR_MORE_DATA: // Truncated packet
        OnRead(recvOv->addr.sin_addr.S_un.S_addr, ntohs(recvOv->addr.sin_port),
			   GetTrailingBytes(recvOv), bytes);
        break;

    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_HOST_UNREACHABLE:
    case ERROR_PROTOCOL_UNREACHABLE:
    case ERROR_PORT_UNREACHABLE:
        // ICMP Errors:
        // These can be easily spoofed and should never be used to terminate a protocol.
        // This callback should be ignored after the first packet is received from the remote host.
        OnUnreachable(recvOv->addr.sin_addr.S_un.S_addr);
    }

	if (!QueueWSARecvFrom(recvOv))
    {
        RegionAllocator::ii->Release(recvOv);
        Close();
    }
}

bool UDPEndpoint::QueueWSASendTo(IP ip, Port port, TypedOverlapped *sendOv, u32 bytes)
{
    if (_closing)
        return false;

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.S_un.S_addr = ip;
    addr.sin_port = htons(port);

    WSABUF wsabuf;
    wsabuf.buf = reinterpret_cast<CHAR*>( GetTrailingBytes(sendOv) );
    wsabuf.len = bytes;

    AddRef();

    // Fire off a WSASendTo() and forget about it
    int result = WSASendTo(_socket, &wsabuf, 1, 0, 0,
						   reinterpret_cast<const sockaddr*>( &addr ),
						   sizeof(addr), &sendOv->ov, 0);

    // This overlapped operation will always complete unless
    // we get an error code other than ERROR_IO_PENDING.
    if (result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        FATAL("UDPEndpoint") << "WSASendTo error: " << SocketGetLastErrorString();
        ReleaseRef();
        // Does not destroy the buffer on error -- just returns false
        return false;
    }

    return true;
}

void UDPEndpoint::OnWSASendToComplete(int error, u32 bytes)
{
    if (_closing)
        return;

    if (error)
    {
        Close();
        return;
    }

    OnWrite(bytes);
}
