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

// FIXME: Half-open connections can easily DoS the TCP server right now.

// FIXME: The class public interfaces actually don't mention IOCP at all, so
// we could rewrite this library for several different APIs and just include
// the best one for the target.

#ifndef THREAD_POOL_SOCKETS_HPP
#define THREAD_POOL_SOCKETS_HPP

#include <cat/threads/ThreadPool.hpp>
#include <string>

#if defined(CAT_OS_WINDOWS)
# include <winsock2.h>
# include <mswsock.h>
# include <cat/port/WindowsInclude.hpp>
#endif

namespace cat {


/*
    IOCPSockets library

    Provides a framework for rapidly developing TCP server and client objects
    that make use of the high performance IO Completion Ports API for Windows.

    All operations are performed asynchronously except ResolveHostname().

    All network events are processed by a thread pool managed by ThreadPool.

    Example library usage:

        ThreadPool::ref()->Startup();

        Create and use TCPServer and TCPClient objects here

        ThreadPool::ref()->Shutdown();
*/

class TCPServer;
class TCPServerConnection;
class TCPClient;
class UDPEndpoint;


//// Sockets

typedef u32 IP;
typedef u16 Port;

// Returns a string describing the last error from Winsock2 API
std::string SocketGetLastErrorString();
std::string SocketGetErrorString(int code);

// Convert from sockaddr_in to a string containing the IP/port
std::string IPToString(const sockaddr_in &addr);
std::string PortToString(const sockaddr_in &addr);

// Convert an IP address to a string
std::string IPToString(IP ip);

// Resolve host name to an IP address (blocking)
IP ResolveHostname(const char *hostname);

// Generate a buffer to pass to Post()
u8 *GetPostBuffer(u32 bytes);

void *ResizePostBuffer(void *buffer, u32 newBytes);

// Release a buffer provided by GetPostBuffer()
// Note: Once the buffer is submitted to Post() this is unnecessary
void ReleasePostBuffer(void *buffer);


//// Overlapped Sockets

// AcceptEx() OVERLAPPED structure
struct AcceptExOverlapped
{
	TypedOverlapped tov;
    SOCKET acceptSocket;

    // Space pre-allocated to receive addresses
    struct
    {
        sockaddr_in address[2];
        u8 padding[2*16];
    } addresses;

    void Set(SOCKET s);
};

// WSARecvFrom() OVERLAPPED structure
struct RecvFromOverlapped
{
	TypedOverlapped tov;
    int addrLen;
    sockaddr_in addr;

	// data follows...

    void Reset();
};


/*
    class TCPServer

    Object that represents a TCP server bound to a single port

    Overload InstantiateServerConnection() to subclass connections with the server
*/
class TCPServer : public ThreadRefObject
{
    friend class TCPServerConnection;
    friend class ThreadPool;

public:
    TCPServer();
    virtual ~TCPServer();

    bool ValidServer();
    Port GetPort();

    bool Bind(Port port = 0);
    void Close();

protected:
    virtual TCPServerConnection *InstantiateServerConnection() = 0;

private:
    SOCKET _socket;
    LPFN_ACCEPTEX _lpfnAcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS _lpfnGetAcceptExSockAddrs;
    LPFN_DISCONNECTEX _lpfnDisconnectEx;
    Port _port;

private:
    bool QueueAcceptEx();
    bool QueueAccepts();

    void OnAcceptExComplete(int error, AcceptExOverlapped *overlapped);
};


/*
    class TCPServerConnection

    Object that represents a TCPServer's connection from a TCPClient

    Object is instantiated just before accepting a connection

    DisconnectClient()      : Disconnect the client
    PostToClient()          : Send a message to the client
    ValidServerConnection() : Returns true iff the connection is valid

    OnConnectFromClient()   : Return false to deny this connection
    OnReadFromClient()      : Return false to disconnect the client in response to a message
    OnWriteToClient()       : Informs the derived class that data has been sent
    OnDisconectFromClient() : Informs the derived class that the client has disconnected
*/
class TCPServerConnection : public ThreadRefObject
{
    friend class TCPServer;
    friend class ThreadPool;

public:
    TCPServerConnection();
    virtual ~TCPServerConnection();

    bool ValidServerConnection();

    void DisconnectClient();
    bool PostToClient(void *buffer, u32 bytes);

protected:
    virtual bool OnConnectFromClient(const sockaddr_in &remoteClientAddress) = 0; // false = disconnect
    virtual bool OnReadFromClient(u8 *data, u32 bytes) = 0; // false = disconnect
    virtual void OnWriteToClient(u32 bytes) = 0;
    virtual void OnDisconnectFromClient() = 0;

private:
    SOCKET _socket;
    LPFN_DISCONNECTEX _lpfnDisconnectEx;
    TypedOverlapped *_recvOv;
    volatile u32 _disconnecting;

private:
    bool AcceptConnection(SOCKET listenSocket, SOCKET acceptSocket,
                LPFN_DISCONNECTEX lpfnDisconnectEx, sockaddr_in *acceptAddress,
                sockaddr_in *remoteClientAddress);

    bool QueueWSARecv();
    void OnWSARecvComplete(int error, u32 bytes);

    bool QueueWSASend(TypedOverlapped *sendOv, u32 bytes);
    void OnWSASendComplete(int error, u32 bytes);

    bool QueueDisconnectEx();
    void OnDisconnectExComplete(int error);
};


/*
    class TCPClient

    Object that represents a TCPClient bound to a single port

    ValidClient()      : Returns true iff the client socket is valid

    ConnectToServer()  : Connects to the given address
    DisconnectServer() : Disconnects from the server
    PostToServer()     : Send a message to the server (will fail if not connected)

    OnConnectToServer()      : Called when connection is accepted
    OnReadFromServer()       : Return false to disconnect the server in response to data
    OnWriteToServer()        : Informs the derived class that data has been sent
    OnDisconnectFromServer() : Informs the derived class that the server has disconnected
*/
class TCPClient : public ThreadRefObject
{
    friend class ThreadPool;

public:
    TCPClient();
    virtual ~TCPClient();

    bool ValidClient();

    bool ConnectToServer(const sockaddr_in &remoteServerAddress);
    void DisconnectServer();
    bool PostToServer(void *buffer, u32 bytes);

protected:
    virtual void OnConnectToServer() = 0;
    virtual bool OnReadFromServer(u8 *data, u32 bytes) = 0; // false = disconnect
    virtual void OnWriteToServer(u32 bytes) = 0;
    virtual void OnDisconnectFromServer() = 0;

private:
    SOCKET _socket;
    TypedOverlapped *_recvOv;
    volatile u32 _disconnecting;

private:
    bool QueueConnectEx(const sockaddr_in &remoteServerAddress);
    void OnConnectExComplete(int error);

    bool QueueWSARecv();
    void OnWSARecvComplete(int error, u32 bytes);

    bool QueueWSASend(TypedOverlapped *sendOv, u32 bytes);
    void OnWSASendComplete(int error, u32 bytes);

    bool QueueDisconnectEx();
    void OnDisconnectExComplete(int error);
};


/*
    class TCPClientQueued

    Base class for a TCP client that needs to queue up data for sending before
    a connection has been established.  e.g. Uplink for a proxy server.

    PostQueuedToServer() : Call in OnConnectToServer() to post the queued messages.
*/
class TCPClientQueued : public TCPClient
{
private:
    volatile bool _queuing;

    Mutex _queueLock;
    void *_queueBuffer;
    u32 _queueBytes;

protected:
    void PostQueuedToServer();

public:
    TCPClientQueued();
    virtual ~TCPClientQueued();

    bool PostToServer(void *buffer, u32 bytes);
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

    // For servers: Bind() with ignoreUnreachable = true ((default))
    // For clients: Bind() with ignoreUnreachable = false and call this
    //              after the first packet from the server is received.
    bool IgnoreUnreachable();

    void Close(); // Invalidates this object
    bool Bind(Port port = 0, bool ignoreUnreachable = true);
    bool Post(IP ip, Port port, void *data, u32 bytes);
    bool QueueWSARecvFrom();

protected:
    virtual void OnRead(IP srcIP, Port srcPort, u8 *data, u32 bytes) = 0; // false = close
    virtual void OnWrite(u32 bytes) = 0;
    virtual void OnClose() = 0;
    virtual void OnUnreachable(IP srcIP) {}

private:
    SOCKET _socket;
    Port _port;
    volatile u32 _closing;

private:
    bool QueueWSARecvFrom(RecvFromOverlapped *recvOv);
    void OnWSARecvFromComplete(int error, RecvFromOverlapped *recvOv, u32 bytes);

    bool QueueWSASendTo(IP ip, Port port, TypedOverlapped *sendOv, u32 bytes);
    void OnWSASendToComplete(int error, u32 bytes);
};


} // namespace cat

#endif // THREAD_POOL_SOCKETS_HPP
