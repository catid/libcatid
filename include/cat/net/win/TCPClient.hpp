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

#ifndef CAT_TCP_CLIENT_HPP
#define CAT_TCP_CLIENT_HPP

/*
	Windows version of thread pool sockets with IO Completion Ports

	Included from <cat/net/ThreadPoolSockets.hpp>
	Do not include directly
*/

namespace cat {


/*
    class TCPClient

    Object that represents a TCPClient bound to a single port

    ValidClient()      : Returns true iff the client socket is valid

    Connect()          : Connects to the given address
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

    bool Connect(const NetAddr &remoteServerAddress);
    void DisconnectServer();
    bool PostToServer(void *buffer, u32 bytes);

protected:
    virtual void OnConnectToServer() = 0;
    virtual bool OnReadFromServer(u8 *data, u32 bytes) = 0; // false = disconnect
    virtual void OnWriteToServer(u32 bytes) = 0;
    virtual void OnDisconnectFromServer() = 0;

private:
    Socket _socket;
    TypedOverlapped *_recvOv; // Preallocated space for receive buffer

	volatile u32 _disconnecting; // Disconnect flag

	// Remembers if socket is IPv6 so that user-provided
	// addresses can be promoted if necessary.
	bool _ipv6;

private:
    bool QueueConnectEx(const NetAddr &remoteServerAddress);
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


} // namespace cat

#endif // CAT_TCP_CLIENT_HPP
