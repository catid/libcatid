/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cat/net/IOCPSockets.hpp>
#include <cat/io/Logging.hpp>
#include <cat/time/Clock.hpp>
#include <cat/io/Settings.hpp>
#include <cat/threads/RegionAllocator.hpp>
#include <process.h>
#include <algorithm>
using namespace std;
using namespace cat;


#if defined (CAT_COMPILER_MSVC)
# pragma comment(lib, "ws2_32.lib")
#endif


// Amount of data to receive overlapped, tuned to exactly fit a
// 2048-byte buffer in the region allocator.
static const int RECV_DATA_SIZE = 2048 - sizeof(TypedOverlapped) - 8; // -8 for rebroadcast inflation

static const int RECVFROM_DATA_SIZE = 2048 - sizeof(RecvFromOverlapped) - 1 - 8; // -1 for the data[1] array


//// TypedOverlapped

void TypedOverlapped::Set(int opcode)
{
    CAT_OBJCLR(ov);
    this->opcode = opcode;
}

void TypedOverlapped::Reset()
{
    CAT_OBJCLR(ov);
}

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
        // Create a new DataOverlapped structure for sending
        DataOverlapped *sendOv = (DataOverlapped *)RegionAllocator::ii->Acquire(sizeof(TypedOverlapped) + bytes);
        if (!sendOv)
        {
            FATAL("IOCPSockets") << "Unable to allocate a send buffer: Out of memory.";
            return 0;
        }

        return sendOv->data;
    }

    void *ResizePostBuffer(void *buffer, u32 newBytes)
    {
        DataOverlapped *sendOv = (DataOverlapped *)RegionAllocator::ii->Resize((u8*)buffer - sizeof(TypedOverlapped), sizeof(TypedOverlapped) + newBytes);
        if (!sendOv)
        {
            FATAL("IOCPSockets") << "Unable to resize a send buffer: Out of memory.";
            return 0;
        }

        return sendOv->data;
    }

    void ReleasePostBuffer(void *buffer)
    {
        RegionAllocator::ii->Release((u8*)buffer - sizeof(TypedOverlapped));
    }
}


//// SocketRefObject

SocketRefObject::SocketRefObject()
{
    refCount = 1;

    SocketManager::ref()->TrackSocket(this);
}

void SocketRefObject::AddRef()
{
    InterlockedIncrement(&refCount);
}

void SocketRefObject::ReleaseRef()
{
    if (InterlockedDecrement(&refCount) == 0)
    {
        SocketManager::ref()->UntrackSocket(this);
        delete this;
    }
}


//// TCPServer

TCPServer::TCPServer()
{
    listenSocket = SOCKET_ERROR;
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
                    &lpfnAcceptEx, sizeof(lpfnAcceptEx), &copied, 0, 0))
    {
        FATAL("TCPServer") << "Unable to get AcceptEx interface: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Get GetAcceptExSockaddrs() interface
    GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs, sizeof(GuidGetAcceptExSockAddrs),
                    &lpfnGetAcceptExSockAddrs, sizeof(lpfnGetAcceptExSockAddrs), &copied, 0, 0))
    {
        FATAL("TCPServer") << "Unable to get GetAcceptExSockAddrs interface: " << SocketGetLastErrorString();
        closesocket(s);
        return false;
    }

    // Get DisconnectEx() interface
    GUID GuidDisconnectEx = WSAID_DISCONNECTEX;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidDisconnectEx, sizeof(GuidDisconnectEx),
                    &lpfnDisconnectEx, sizeof(lpfnDisconnectEx), &copied, 0, 0))
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

    listenSocket = s;

    // Prepare to receive completions in the worker threads
    // Queue a bunch of AcceptEx() calls
    if (!SocketManager::ref()->Associate(s, this) ||
        !QueueAccepts())
    {
        Close();
        return false;
    }

    this->port = port;

    INFO("TCPServer") << "Listening on port " << GetPort();

    return true;
}

bool TCPServer::ValidServer()
{
    return listenSocket != SOCKET_ERROR;
}

Port TCPServer::GetPort()
{
    // Get bound port if it was random
    if (port == 0)
    {
        sockaddr_in addr;
        int namelen = sizeof(addr);
        if (getsockname(listenSocket, (sockaddr*)&addr, &namelen))
        {
            FATAL("TCPServer") << "Unable to get own address: " << SocketGetLastErrorString();
            return 0;
        }

        port = ntohs(addr.sin_port);
    }

    return port;
}

void TCPServer::Close()
{
    if (listenSocket != SOCKET_ERROR)
    {
        closesocket(listenSocket);
        listenSocket = SOCKET_ERROR;
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
    AcceptExOverlapped *overlapped = (AcceptExOverlapped *)RegionAllocator::ii->Acquire(sizeof(AcceptExOverlapped));
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

    BOOL result = lpfnAcceptEx(listenSocket, s, &overlapped->addresses, 0, sizeof(sockaddr_in)+16,
                                sizeof(sockaddr_in)+16, &received, &overlapped->ov);

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
        FATAL("TCPServer") << "error to pre-accept any connections: Server cannot accept connections.";
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
    lpfnGetAcceptExSockAddrs(&overlapped->addresses, 0, sizeof(sockaddr_in)+16, sizeof(sockaddr_in)+16,
                                (sockaddr**)&local, &localLen, (sockaddr**)&remote, &remoteLen);

    // Instantiate a server connection
    TCPServerConnection *conn = InstantiateServerConnection();
    if (!conn) return;

    // Pass the connection parameters to the connection instance for acceptance
    if (!conn->AcceptConnection(listenSocket, overlapped->acceptSocket, lpfnDisconnectEx, local, remote))
        conn->ReleaseRef();

    // Queue up another AcceptEx to fill in for this one
    QueueAcceptEx();
}


//// TCPServerConnection

TCPServerConnection::TCPServerConnection()
{
    // Initialize to an invalid state.
    // Connection is invalid until AcceptConnection() runs successfully.
    acceptSocket = SOCKET_ERROR;
    recvOv = 0;
    disconnecting = 0;
}

TCPServerConnection::~TCPServerConnection()
{
    if (acceptSocket != SOCKET_ERROR)
        closesocket(acceptSocket);

    // Release memory for the overlapped structure
    if (recvOv)
        RegionAllocator::ii->Release(recvOv);
}

bool TCPServerConnection::ValidServerConnection()
{
    return acceptSocket != SOCKET_ERROR;
}

void TCPServerConnection::DisconnectClient()
{
    // Only allow disconnect to run once
    if (InterlockedIncrement(&disconnecting) == 1)
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
    DataOverlapped *sendOv = (DataOverlapped *)((u8*)buffer - sizeof(TypedOverlapped));

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
    this->acceptSocket = acceptSocket;
    this->lpfnDisconnectEx = lpfnDisconnectEx;

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

    // Create a new DataOverlapped structure for receiving
    recvOv = (DataOverlapped *)RegionAllocator::ii->Acquire(sizeof(TypedOverlapped) + RECV_DATA_SIZE);
    if (!recvOv)
    {
        FATAL("TCPServerConnection") << "Unable to allocate a receive buffer: Out of memory.";
        return false;
    }
    recvOv->Set(OVOP_SERVER_RECV);

    // Prepare to receive completions in the worker threads.
    // Do this first so that if the server will send data immediately it
    // won't block or leak memory or fail or whatever would happen.
    if (!SocketManager::ref()->Associate(acceptSocket, this))
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
    if (disconnecting)
        return false;

    WSABUF wsabuf;
    wsabuf.buf = (char*)&recvOv->data[0];
    wsabuf.len = RECV_DATA_SIZE;

    AddRef();

    // Queue up a WSARecv()
    DWORD flags = 0, bytes;
    int result = WSARecv(acceptSocket, &wsabuf, 1, &bytes, &flags, &recvOv->ov, 0); 

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
    if (disconnecting)
        return;

    if (error)
    {
        DisconnectClient();
        return;
    }

    // When WSARecv completes with no data, it indicates a graceful disconnect.
    if (!bytes || !OnReadFromClient(recvOv->data, bytes))
        DisconnectClient();
    else
    {
        // Reset the overlapped structure so it can be re-used
        recvOv->Reset();

        // Queue up the next receive
        if (!QueueWSARecv())
            DisconnectClient();
    }
}


bool TCPServerConnection::QueueWSASend(DataOverlapped *sendOv, u32 bytes)
{
    if (disconnecting)
        return false;

    WSABUF wsabuf;
    wsabuf.buf = (char*)&sendOv->data[0];
    wsabuf.len = bytes;

    AddRef();

    // Fire off a WSASend() and forget about it
    int result = WSASend(acceptSocket, &wsabuf, 1, 0, 0, &sendOv->ov, 0);

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
    if (disconnecting)
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
    // Create a new DataOverlapped structure for receiving
    TypedOverlapped *overlapped = (TypedOverlapped *)RegionAllocator::ii->Acquire(sizeof(TypedOverlapped));
    if (!overlapped)
    {
        FATAL("TCPServerConnection") << "Unable to allocate a DisconnectEx overlapped structure: Out of memory.";
        return false;
    }
    overlapped->Set(OVOP_SERVER_CLOSE);

    AddRef();

    // Queue up a DisconnectEx()
    BOOL result = lpfnDisconnectEx(acceptSocket, &overlapped->ov, 0, 0); 

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
    recvOv = 0;
    connectSocket = SOCKET_ERROR;
    disconnecting = 0;
}

TCPClient::~TCPClient()
{
    if (connectSocket != SOCKET_ERROR)
        closesocket(connectSocket);

    // Release memory for the overlapped structure
    if (recvOv)
        RegionAllocator::ii->Release(recvOv);
}

bool TCPClient::ValidClient()
{
    return connectSocket != SOCKET_ERROR;
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

    connectSocket = s;

    // Prepare to receive completions in the worker threads
    // Connect to server asynchronously
    if (!SocketManager::ref()->Associate(s, this) ||
        !QueueConnectEx(remoteServerAddress))
    {
        closesocket(s);
        connectSocket = SOCKET_ERROR;
        return false;
    }

    return true;
}

void TCPClient::DisconnectServer()
{
    // Only allow disconnect to run once
    if (InterlockedIncrement(&disconnecting) == 1)
    {
        OnDisconnectFromServer();

        if (!QueueDisconnectEx())
            ReleaseRef();
    }
}

bool TCPClient::PostToServer(void *buffer, u32 bytes)
{
    // Recover the full overlapped structure from data pointer
    DataOverlapped *sendOv = (DataOverlapped *)((u8*)buffer - sizeof(TypedOverlapped));

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

    if (WSAIoctl(connectSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx,
                 sizeof(GuidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx), &copied, 0, 0))
    {
        FATAL("TCPClient") << "Unable to get ConnectEx interface: " << SocketGetLastErrorString();
        return false;
    }

    // Create a new DataOverlapped structure for receiving
    TypedOverlapped *overlapped = (TypedOverlapped *)RegionAllocator::ii->Acquire(sizeof(TypedOverlapped));
    if (!overlapped)
    {
        FATAL("TCPClient") << "Unable to allocate a ConnectEx overlapped structure: Out of memory.";
        return false;
    }
    overlapped->Set(OVOP_CONNECT_EX);

    AddRef();

    // Queue up a ConnectEx()
    BOOL result = lpfnConnectEx(connectSocket, (sockaddr*)&remoteServerAddress,
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
    if (disconnecting)
        return;

    if (error)
    {
        DisconnectServer();
        return;
    }

    // Finish socket creation by updating the connection context
    if (setsockopt(connectSocket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, 0, 0))
    {
        WARN("TCPClient") << "Unable to update connect socket context: " << SocketGetLastErrorString();
        DisconnectServer();
        return;
    }

    // Create a new DataOverlapped structure for receiving
    recvOv = (DataOverlapped *)RegionAllocator::ii->Acquire(sizeof(TypedOverlapped) + RECV_DATA_SIZE);
    if (!recvOv)
    {
        FATAL("TCPClient") << "Unable to allocate a receive buffer: Out of memory.";
        DisconnectServer();
        return;
    }
    recvOv->Set(OVOP_CLIENT_RECV);

    // Notify the derived class that we connected
    OnConnectToServer();

    // Queue up a receive
    if (!QueueWSARecv())
        DisconnectServer();
}


bool TCPClient::QueueWSARecv()
{
    if (disconnecting)
    {
        WARN("TCPClient") << "WSARecv ignored while disconnecting.";
        return false;
    }

    WSABUF wsabuf;
    wsabuf.buf = (char*)&recvOv->data[0];
    wsabuf.len = RECV_DATA_SIZE;

    AddRef();

    // Queue up a WSARecv()
    DWORD flags = 0, bytes;
    int result = WSARecv(connectSocket, &wsabuf, 1, &bytes, &flags, &recvOv->ov, 0); 

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
    if (disconnecting)
        return;

    if (error)
    {
        DisconnectServer();
        return;
    }

    // When WSARecv completes with no data, it indicates a graceful disconnect.
    if (!bytes || !OnReadFromServer(recvOv->data, bytes))
        DisconnectServer();
    else
    {
        // Reset the overlapped structure so it can be re-used
        recvOv->Reset();

        // Queue up the next receive
        if (!QueueWSARecv())
            DisconnectServer();
    }
}


bool TCPClient::QueueWSASend(DataOverlapped *sendOv, u32 bytes)
{
    if (disconnecting)
        return false;

    WSABUF wsabuf;
    wsabuf.buf = (char*)&sendOv->data[0];
    wsabuf.len = bytes;

    AddRef();

    // Fire off a WSASend() and forget about it
    int result = WSASend(connectSocket, &wsabuf, 1, 0, 0, &sendOv->ov, 0);

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
    if (disconnecting)
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

    if (WSAIoctl(connectSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidDisconnectEx,
                 sizeof(GuidDisconnectEx), &lpfnDisconnectEx, sizeof(lpfnDisconnectEx), &copied, 0, 0))
    {
        FATAL("TCPClient") << "Unable to get DisconnectEx interface: " << SocketGetLastErrorString();
        return false;
    }

    // Create a new DataOverlapped structure for receiving
    TypedOverlapped *overlapped = (TypedOverlapped *)RegionAllocator::ii->Acquire(sizeof(TypedOverlapped));
    if (!overlapped)
    {
        FATAL("TCPClient") << "Unable to allocate a DisconnectEx overlapped structure: Out of memory.";
        return false;
    }
    overlapped->Set(OVOP_CLIENT_CLOSE);

    AddRef();

    // Queue up a DisconnectEx()
    BOOL result = lpfnDisconnectEx(connectSocket, &overlapped->ov, 0, 0); 

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
    queueBuffer = 0;
    //queueBytes = 0;
    queuing = true;
}

TCPClientQueued::~TCPClientQueued()
{
    if (queueBuffer)
        ReleasePostBuffer(queueBuffer);
}

bool TCPClientQueued::PostToServer(void *buffer, u32 bytes)
{
    // Try not to hold a lock if we can help it
    if (!queuing)
        return TCPClient::PostToServer(buffer, bytes);

    AutoMutex lock(queueLock);

    // Check to make sure we're still queuing
    if (!queuing)
    {
        lock.Release();
        return TCPClient::PostToServer(buffer, bytes);
    }

    if (queueBuffer)
    {
        queueBuffer = ResizePostBuffer(queueBuffer, queueBytes + bytes);
        memcpy((u8*)queueBuffer + queueBytes, buffer, bytes);
        queueBytes += bytes;
        ReleasePostBuffer(buffer);
    }
    else
    {
        queueBuffer = buffer;
        queueBytes = bytes;
    }

    return true;
}

void TCPClientQueued::PostQueuedToServer()
{
    AutoMutex lock(queueLock);

    if (queueBuffer)
    {
        TCPClient::PostToServer(queueBuffer, queueBytes);
        queueBuffer = 0;
    }

    queuing = false;
}


//// UDP Endpoint

UDPEndpoint::UDPEndpoint()
{
    port = 0;
    closing = 0;
    endpointSocket = SOCKET_ERROR;
}

UDPEndpoint::~UDPEndpoint()
{
    if (endpointSocket != SOCKET_ERROR)
        closesocket(endpointSocket);
}

void UDPEndpoint::Close()
{
    // Only allow close to run once
    if (InterlockedIncrement(&closing) == 1)
    {
        if (endpointSocket != SOCKET_ERROR)
        {
            closesocket(endpointSocket);
            endpointSocket = SOCKET_ERROR;
        }

        OnClose();

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

	if (endpointSocket == SOCKET_ERROR)
		return false;

	DWORD dwBytesReturned = 0;
    BOOL bNewBehavior = FALSE;
    if (WSAIoctl(endpointSocket, SIO_UDP_CONNRESET, &bNewBehavior,
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

    endpointSocket = s;

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
        endpointSocket = SOCKET_ERROR;
        return false;
    }

    // Prepare to receive completions in the worker threads
    if (!SocketManager::ref()->Associate(s, this) ||
        !QueueWSARecvFrom())
    {
        closesocket(s);
        endpointSocket = SOCKET_ERROR;
        return false;
    }

    this->port = port;

    INFO("UDPEndpoint") << "Open on port " << GetPort();

    return true;
}

bool UDPEndpoint::Valid()
{
    return endpointSocket != SOCKET_ERROR;
}

Port UDPEndpoint::GetPort()
{
    // Get bound port if it was random
    if (port == 0)
    {
        sockaddr_in addr;
        int namelen = sizeof(addr);
        if (getsockname(endpointSocket, (sockaddr*)&addr, &namelen))
        {
            FATAL("UDPEndpoint") << "Unable to get own address: " << SocketGetLastErrorString();
            return 0;
        }

        port = ntohs(addr.sin_port);
    }

    return port;
}

bool UDPEndpoint::Post(IP ip, Port port, void *buffer, u32 bytes)
{
    // Recover the full overlapped structure from data pointer
    DataOverlapped *sendOv = (DataOverlapped *)((u8*)buffer - sizeof(TypedOverlapped));

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
    recvOv->Reset();

    WSABUF wsabuf;
    wsabuf.buf = (char*)&recvOv->data[0];
    wsabuf.len = RECVFROM_DATA_SIZE;

    AddRef();

    // Queue up a WSARecvFrom()
    DWORD flags = 0, bytes;
    int result = WSARecvFrom(endpointSocket, &wsabuf, 1, &bytes, &flags, (sockaddr*)&recvOv->addr, &recvOv->addrLen, &recvOv->ov, 0); 

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
    if (closing)
        return false;

    // Create a new RecvFromOverlapped structure for receiving
    RecvFromOverlapped *recvOv = (RecvFromOverlapped*)RegionAllocator::ii->Acquire(sizeof(RecvFromOverlapped) - 1 + RECVFROM_DATA_SIZE);
    if (!recvOv)
    {
        FATAL("UDPEndpoint") << "Unable to allocate a receive buffer: Out of memory.";
        return false;
    }
    recvOv->opcode = OVOP_RECVFROM;

    return QueueWSARecvFrom(recvOv);
}

void UDPEndpoint::OnWSARecvFromComplete(int error, RecvFromOverlapped *recvOv, u32 bytes)
{
    switch (error)
    {
    case 0:
    case ERROR_MORE_DATA: // Truncated packet
        OnRead(recvOv->addr.sin_addr.S_un.S_addr, ntohs(recvOv->addr.sin_port), recvOv->data, bytes);
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

bool UDPEndpoint::QueueWSASendTo(IP ip, Port port, DataOverlapped *sendOv, u32 bytes)
{
    if (closing)
        return false;

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.S_un.S_addr = ip;
    addr.sin_port = htons(port);

    WSABUF wsabuf;
    wsabuf.buf = (char*)&sendOv->data[0];
    wsabuf.len = bytes;

    AddRef();

    // Fire off a WSASendTo() and forget about it
    int result = WSASendTo(endpointSocket, &wsabuf, 1, 0, 0, (const sockaddr*)&addr, sizeof(addr), &sendOv->ov, 0);

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
    if (closing)
        return;

    if (error)
    {
        Close();
        return;
    }

    OnWrite(bytes);
}


//// Thread Pool

SocketManager::SocketManager()
{
    port = 0;

    socketRefHead = 0;
}


bool SocketManager::SpawnThread()
{
    HANDLE thread = (HANDLE)_beginthreadex(0, 0, CompletionThread, port, 0, 0);

    if (thread == (HANDLE)-1)
    {
        FATAL("SocketManager") << "CreateThread() error: " << GetLastError();
        return false;
    }

    threads.push_back(thread);
    return true;
}

bool SocketManager::SpawnThreads()
{
    ULONG_PTR ulpProcessAffinityMask, ulpSystemAffinityMask;

    GetProcessAffinityMask(GetCurrentProcess(), &ulpProcessAffinityMask, &ulpSystemAffinityMask);

    while (ulpProcessAffinityMask)
    {
        if (ulpProcessAffinityMask & 1)
        {
            SpawnThread();
            SpawnThread();
        }

        ulpProcessAffinityMask >>= 1;
    }

    if (threads.size() <= 0)
    {
        FATAL("SocketManager") << "error to spawn any threads.";
        return false;
    }

    INFO("SocketManager") << "Spawned " << (u32)threads.size() << " worker threads";
    return true;
}

bool SocketManager::Associate(SOCKET s, void *key)
{
    HANDLE result = CreateIoCompletionPort((HANDLE)s, port, (ULONG_PTR)key, 0);

    if (!result)
    {
        FATAL("SocketManager") << "Unable to create completion port: " << SocketGetLastErrorString();
        return false;
    }

    port = result;

    if (threads.size() <= 0 && !SpawnThreads())
    {
        CloseHandle(port);
        port = 0;
        return false;
    }

    return true;
}


void SocketManager::TrackSocket(SocketRefObject *object)
{
    object->last = 0;

    AutoMutex lock(socketLock);

    // Add to the head of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    object->next = socketRefHead;
    if (socketRefHead) socketRefHead->last = object;
    socketRefHead = object;
}

void SocketManager::UntrackSocket(SocketRefObject *object)
{
    AutoMutex lock(socketLock);

    // Remove from the middle of a doubly-linked list of tracked sockets,
    // used for releasing sockets during termination.
    SocketRefObject *last = object->last, *next = object->next;
    if (last) last->next = next;
    else socketRefHead = next;
    if (next) next->last = last;
}

void SocketManager::Startup()
{
    WSADATA wsaData;

    // Request Winsock 2.2
    if (NO_ERROR != WSAStartup(MAKEWORD(2,2), &wsaData))
    {
        FATAL("SocketManager") << "WSAStartup error: " << SocketGetLastErrorString();
        return;
    }
}

void SocketManager::Shutdown()
{
    INFO("SocketManager") << "Terminating the thread pool...";

    u32 count = (u32)threads.size();

    if (count)
    {
        INFO("SocketManager") << "Shutdown task (1/4): Stopping threads...";

        if (port)
        while (count--)
        {
            if (!PostQueuedCompletionStatus(port, 0, 0, 0))
            {
                FATAL("SocketManager") << "Post error: " << GetLastError();
                return;
            }
        }

        if (WAIT_FAILED == WaitForMultipleObjects((DWORD)threads.size(), &threads[0], TRUE, INFINITE))
        {
            FATAL("SocketManager") << "error waiting for thread termination: " << GetLastError();
            return;
        }

        for_each(threads.begin(), threads.end(), CloseHandle);

        threads.clear();
    }

    INFO("SocketManager") << "Shutdown task (2/4): Deleting managed sockets...";

    SocketRefObject *kill, *object = socketRefHead;
    while (object)
    {
        kill = object;
        object = object->next;
        delete kill;
    }

    INFO("SocketManager") << "Shutdown task (3/4): Closing IOCP port...";

    if (port)
    {
        CloseHandle(port);
        port = 0;
    }

    INFO("SocketManager") << "Shutdown task (4/4): WSACleanup()...";

    WSACleanup();

    INFO("SocketManager") << "...Termination complete.";

    delete this;
}


unsigned int WINAPI SocketManager::CompletionThread(void *port)
{
    DWORD bytes;
    void *key = 0;
    TypedOverlapped *ov = 0;
    int error;

    for (;;)
    {
        if (GetQueuedCompletionStatus((HANDLE)port, &bytes, (PULONG_PTR)&key, (OVERLAPPED**)&ov, INFINITE))
            error = 0;
        else
        {
            error = WSAGetLastError();

            switch (error)
            {
            case WSA_OPERATION_ABORTED:
            case ERROR_CONNECTION_ABORTED:
            case ERROR_NETNAME_DELETED:  // Operation on closed socket failed
            case ERROR_MORE_DATA:        // UDP buffer not large enough for whole packet
            case ERROR_PORT_UNREACHABLE: // Got an ICMP response back that the destination port is unreachable
            case ERROR_SEM_TIMEOUT:      // Half-open TCP AcceptEx() has reset
                // Operation failure codes (we don't differentiate between them)
                break;

            default:
                // Report other errors this library hasn't been designed to handle yet
                FATAL("WorkerThread") << SocketGetLastErrorString() << " (key=" << key << ", ov="
                    << ov << ", opcode=" << (ov ? ov->opcode : -1) << ", bytes=" << bytes << ")";
                break;
            }
        }

        // Terminate thread when we receive a zeroed completion packet
        if (!bytes && !key && !ov)
            return 0;

        switch (ov->opcode)
        {
        case OVOP_ACCEPT_EX:
            ( (TCPServer*)key )->OnAcceptExComplete( error, (AcceptExOverlapped*)ov );
            ( (TCPServer*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_SERVER_RECV:
            ( (TCPServerConnection*)key )->OnWSARecvComplete( error, bytes );
            ( (TCPServerConnection*)key )->ReleaseRef();
            // TCPServer tracks the overlapped buffer lifetime
            break;

        case OVOP_SERVER_SEND:
            ( (TCPServerConnection*)key )->OnWSASendComplete( error, bytes );
            ( (TCPServerConnection*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_SERVER_CLOSE:
            ( (TCPServerConnection*)key )->OnDisconnectExComplete( error );
            ( (TCPServerConnection*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_CONNECT_EX:
            ( (TCPClient*)key )->OnConnectExComplete( error );
            ( (TCPClient*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_CLIENT_RECV:
            ( (TCPClient*)key )->OnWSARecvComplete( error, bytes );
            ( (TCPClient*)key )->ReleaseRef();
            // TCPClient tracks the overlapped buffer lifetime
            break;

        case OVOP_CLIENT_SEND:
            ( (TCPClient*)key )->OnWSASendComplete( error, bytes );
            ( (TCPClient*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_CLIENT_CLOSE:
            ( (TCPClient*)key )->OnDisconnectExComplete( error );
            ( (TCPClient*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;

        case OVOP_RECVFROM:
            ( (UDPEndpoint*)key )->OnWSARecvFromComplete( error, (RecvFromOverlapped*)ov, bytes );
            ( (UDPEndpoint*)key )->ReleaseRef();
            // UDPEndpoint tracks the overlapped buffer lifetime
            break;

        case OVOP_SENDTO:
            ( (UDPEndpoint*)key )->OnWSASendToComplete( error, bytes );
            ( (UDPEndpoint*)key )->ReleaseRef();
            RegionAllocator::ii->Release(ov);
            break;
        }
    }
}
