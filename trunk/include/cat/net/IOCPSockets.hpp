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

// 06/11/09 part of libcat-1.0
// 05/04/09 Add GetPostBuffer() for less copying
// 10/08/08 Add UDPEndpoint::IgnoreUnreachable()

// TODO: Half-open connections can easily DoS the TCP server right now.

// TODO: The class public interfaces actually don't mention IOCP at all, so
// we could rewrite this library for several different APIs and just include
// the best one for the target.

#ifndef IOCP_SOCKETS_HPP
#define IOCP_SOCKETS_HPP

#include <cat/Singleton.hpp>
#include <cat/threads/Mutex.hpp>
#include <vector>
#include <string>

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
# include <winsock2.h>
# include <mswsock.h>
#endif

namespace cat {


/*
    IOCPSockets library

    Provides a framework for rapidly developing TCP server and client objects
    that make use of the high performance IO Completion Ports API for Windows.

    All operations are performed asynchronously except ResolveHostname().

    All network events are processed by a thread pool managed by SocketManager.

    Example library usage:

        SocketManager::ref()->Startup();

        Create and use TCPServer and TCPClient objects here

        SocketManager::ref()->Shutdown();
*/

class TCPServer;
class TCPServerConnection;
class TCPClient;
class UDPEndpoint;


//// Sockets

typedef u32 IP;
typedef u16 Port;


//// Sockets

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

// Overlapped opcodes that describe the purpose of the OVERLAPPED structure
enum OverlappedOpcodes
{
    OVOP_ACCEPT_EX,    // AcceptEx() completion, remote client connected
    OVOP_SERVER_RECV,  // WSARecv() completion for local server
    OVOP_CLIENT_RECV,  // WSARecv() completion, for local client
    OVOP_RECVFROM,     // WSARecvFrom() completion, for local endpoint
    OVOP_CONNECT_EX,   // ConnectEx() completion, local client connected
    OVOP_SERVER_SEND,  // WSASend() completion, local server sent something
    OVOP_CLIENT_SEND,  // WSASend() completion, local client sent something
    OVOP_SENDTO,       // WSASendTo() completion, local endpoint sent something
    OVOP_SERVER_CLOSE, // DisconnectEx() completion, graceful close
    OVOP_CLIENT_CLOSE, // DisconnectEx() completion, graceful close
};

#if defined(CAT_COMPILER_MSVC)
#pragma pack(push)
#pragma pack(1)
#endif

// Base class for any typed OVERLAPPED structure
struct TypedOverlapped
{
    OVERLAPPED ov;
    int opcode;

    void Set(int opcode);

    // Reset after an I/O operation to prepare for the next one
    void Reset();
} CAT_PACKED;

// AcceptEx() OVERLAPPED structure
struct AcceptExOverlapped : public TypedOverlapped
{
    SOCKET acceptSocket;

    // Space pre-allocated to receive addresses
    struct
    {
        sockaddr_in address[2];
        u8 padding[2*16];
    } addresses;

    void Set(SOCKET s);
} CAT_PACKED;

// WSASendTo()/WSASend()/WSARecv() OVERLAPPED structure
struct DataOverlapped : public TypedOverlapped
{
    u8 data[1];
} CAT_PACKED;

// WSARecvFrom() OVERLAPPED structure
struct RecvFromOverlapped : public TypedOverlapped
{
    int addrLen;
    sockaddr_in addr;

    u8 data[1];

    void Reset();
} CAT_PACKED;

#if defined(CAT_COMPILER_MSVC)
#pragma pack(pop)
#endif


/*
    class SocketRefObject

    Base class for any thread-safe reference-counted socket object
*/
class SocketRefObject
{
    friend class SocketManager;
    SocketRefObject *last, *next;

    volatile long refCount;

public:
    SocketRefObject();
    virtual ~SocketRefObject() {}

public:
    void AddRef();
    void ReleaseRef();
};


/*
    class TCPServer

    Object that represents a TCP server bound to a single port

    Overload InstantiateServerConnection() to subclass connections with the server
*/
class TCPServer : public SocketRefObject
{
    friend class TCPServerConnection;
    friend class SocketManager;

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
    SOCKET listenSocket;
    LPFN_ACCEPTEX lpfnAcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs;
    LPFN_DISCONNECTEX lpfnDisconnectEx;
    Port port;

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
class TCPServerConnection : public SocketRefObject
{
    friend class TCPServer;
    friend class SocketManager;

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
    SOCKET acceptSocket;
    LPFN_DISCONNECTEX lpfnDisconnectEx;
    DataOverlapped *recvOv;
    volatile long disconnecting;

private:
    bool AcceptConnection(SOCKET listenSocket, SOCKET acceptSocket,
                LPFN_DISCONNECTEX lpfnDisconnectEx, sockaddr_in *acceptAddress,
                sockaddr_in *remoteClientAddress);

    bool QueueWSARecv();
    void OnWSARecvComplete(int error, u32 bytes);

    bool QueueWSASend(DataOverlapped *sendOv, u32 bytes);
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
class TCPClient : public SocketRefObject
{
    friend class SocketManager;

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
    SOCKET connectSocket;
    DataOverlapped *recvOv;
    volatile long disconnecting;

private:
    bool QueueConnectEx(const sockaddr_in &remoteServerAddress);
    void OnConnectExComplete(int error);

    bool QueueWSARecv();
    void OnWSARecvComplete(int error, u32 bytes);

    bool QueueWSASend(DataOverlapped *sendOv, u32 bytes);
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
    volatile bool queuing;

    Mutex queueLock;
    void *queueBuffer;
    u32 queueBytes;

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
class UDPEndpoint : public SocketRefObject
{
    friend class SocketManager;

public:
    UDPEndpoint();
    virtual ~UDPEndpoint();

    bool Valid();
    Port GetPort();

    // For servers: Bind() with ignoreUnreachable = true ((default))
    // For clients: Bind() with ignoreUnreachable = false and call this
    //              after the first packet from the server is received.
    void IgnoreUnreachable();

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
    SOCKET endpointSocket;
    Port port;
    volatile long closing;

private:
    bool QueueWSARecvFrom(RecvFromOverlapped *recvOv);
    void OnWSARecvFromComplete(int error, RecvFromOverlapped *recvOv, u32 bytes);

    bool QueueWSASendTo(IP ip, Port port, DataOverlapped *sendOv, u32 bytes);
    void OnWSASendToComplete(int error, u32 bytes);
};


/*
    class SocketManager

    Startup()  : Call to start up the thread pool and stuff
    Shutdown() : Call to destroy the thread pool and objects
*/
class SocketManager : public Singleton<SocketManager>
{
    friend class TCPServer;
    friend class TCPServerConnection;
    friend class TCPClient;
    friend class UDPEndpoint;
    static unsigned int WINAPI CompletionThread(void *port);

    CAT_SINGLETON(SocketManager);

    HANDLE port;
    std::vector<HANDLE> threads;

    // Track other sockets for graceful termination
    Mutex socketLock;
    SocketRefObject *socketRefHead;

    friend class SocketRefObject;
    void TrackSocket(SocketRefObject *object);
    void UntrackSocket(SocketRefObject *object);

protected:
    bool SpawnThread();
    bool SpawnThreads();
    bool Associate(SOCKET s, void *key);

public:
    void Startup();
    void Shutdown();
};


} // namespace cat

#endif // IOCP_SOCKETS_HPP
