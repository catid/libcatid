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

#ifndef CAT_SOCKETS_HPP
#define CAT_SOCKETS_HPP

#include <cat/Platform.hpp>
#include <string>

#if defined(CAT_OS_WINDOWS) || defined(CAT_OS_WINDOWS_CE)
# define CAT_MS_SOCKET_API
# include <WS2tcpip.h>
#else
# define CAT_UNIX_SOCKET_API
#include <unistd.h>
#endif

#define CAT_IP6_LOOPBACK "::1"
#define CAT_IP4_LOOPBACK "127.0.0.1"

namespace cat {


//// Data Types

#if defined(CAT_MS_SOCKET_API)
	typedef SOCKET Socket;
# define CAT_SOCKET_INVALID INVALID_SOCKET
# define CAT_SOCKET_ERROR SOCKET_ERROR
# define CloseSocket(s) (0 == closesocket(s))
#else
	typedef int Socket;
# define CAT_SOCKET_INVALID -1
# define CAT_SOCKET_ERROR -1
# define CloseSocket(s) (0 == close(s))
#endif

typedef u16 Port;

// Wrapper for IPv4 and IPv6 addresses
class NetAddr
{
	union
	{
		u8 v6_bytes[16];
		u16 v6_words[8];
		u64 v6[2];
		struct {
			u32 v4;
			u32 v4_padding[3];
		};
	} _ip; // Network order

	union
	{
		u32 _valid;
		struct {
			Port _port; // Host order
			u16 _family; // Host order
		};
	};

public:
	static const int IP6_BYTES = 16;

	typedef sockaddr_in6 SockAddr;

public:
	CAT_INLINE NetAddr() {}
	NetAddr(const char *ip_str, Port port);
	NetAddr(const sockaddr_in6 &addr);
	NetAddr(const sockaddr_in &addr);
	NetAddr(const sockaddr *addr);

public:
	NetAddr(const NetAddr &addr);
	NetAddr &operator=(const NetAddr &addr);

public:
	bool Wrap(const sockaddr_in6 &addr);
	bool Wrap(const sockaddr_in &addr);
	bool Wrap(const sockaddr *addr);

public:
	// Promote an IPv4 address to an IPv6 address if needed
	void PromoteTo6();

public:
	CAT_INLINE bool Valid() const { return _valid != 0; }
	CAT_INLINE bool Is6() const { return _family == AF_INET6; }

	CAT_INLINE const u32 GetIP4() const { return _ip.v4; }
	CAT_INLINE const u64 *GetIP6() const { return _ip.v6; }

	CAT_INLINE Port GetPort() const { return _port; }
	CAT_INLINE void SetPort(Port port) { _port = port; }

public:
	bool EqualsIPOnly(const NetAddr &addr) const;
	bool operator==(const NetAddr &addr) const;
	bool operator!=(const NetAddr &addr) const;

public:
	bool SetFromString(const char *ip_str, Port port = 0);
	std::string IPToString() const;

public:
	bool Unwrap(SockAddr &addr, int &addr_len, bool PromoteToIP6 = false) const;
};


//// Helper Functions

// Run startup and cleanup functions needed under some OS
bool StartupSockets(); // returns false on error
void CleanupSockets();

// Sets OnlyIPv4 if IPv6 will be unsupported
// Returns true on success
bool CreateSocket(int type, int protocol, bool SupportIPv4, Socket &out_s, bool &out_OnlyIPv4);

// Returns true on success
bool NetBind(Socket s, Port port, bool OnlyIPv4);

// Returns 0 on failure
Port GetBoundPort(Socket s);


//// Error Codes

// Returns a string describing the last error from Winsock2 API
std::string SocketGetLastErrorString();
std::string SocketGetErrorString(int code);


} // namespace cat

#endif // CAT_SOCKETS_HPP
