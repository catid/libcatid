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

#include <cat/net/Sockets.hpp>
#include <sstream>
using namespace std;
using namespace cat;


#if defined (CAT_COMPILER_MSVC)
#pragma comment(lib, "ws2_32.lib")
#endif


//// Error Codes

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
        case WSA_INVALID_HANDLE:       return "[Invalid handle]";
        case WSA_INVALID_PARAMETER:    return "[Invalid parameter]";
        case WSAEFAULT:                return "[Fault]";
        case WSAEINTR:                 return "[Interrupted]";
        case WSAEINVAL:                return "[Invalid]";
        case WSAEISCONN:               return "[Is connected]";
        case WSAENETRESET:             return "[Network reset]";
        case WSAENOTSOCK:              return "[Parameter is not a socket]";
        case WSAEOPNOTSUPP:            return "[Operation not supported]";
        case WSAESOCKTNOSUPPORT:       return "[Socket type not supported]";
        case WSAESHUTDOWN:             return "[Shutdown]";
        case WSAEWOULDBLOCK:           return "[Operation would block]";
        case WSAEMSGSIZE:              return "[Message size]";
        case WSAETIMEDOUT:             return "[Operation timed out]";
        case WSAECONNRESET:            return "[Connection reset]";
        case WSAENOTCONN:              return "[Socket not connected]";
        case WSAEDISCON:               return "[Disconnected]";
		case WSAENOBUFS:               return "[No buffer space available]";
        case ERROR_IO_PENDING:         return "[IO operation will complete in IOCP worker thread]";
        case WSA_OPERATION_ABORTED:    return "[Operation aborted]";
        case ERROR_CONNECTION_ABORTED: return "[Connection aborted locally]";
        case ERROR_NETNAME_DELETED:    return "[Socket was already closed]";
        case ERROR_PORT_UNREACHABLE:   return "[Destination port is unreachable]";
        case ERROR_MORE_DATA:          return "[More data is available]";
        };

        ostringstream oss;
        oss << "[Error code: " << code << " (0x" << hex << code << ")]";
        return oss.str();
    }

	static bool IsWindowsVistaOrNewer()
	{
		DWORD dwVersion = 0; 
		DWORD dwMajorVersion = 0;

		dwVersion = GetVersion();
		dwMajorVersion = (DWORD)(LOBYTE(LOWORD(dwVersion)));

		// 5: 2000(.0), XP(.1), 2003(.2)
		// 6: Vista(.0), 7(.1)
		return (dwMajorVersion >= 6);
	}

	bool CreateSocket(int type, int protocol, bool SupportIPv4, Socket &out_s, bool &out_OnlyIPv4)
	{
		// Under Windows 2003 or earlier, when a server binds to an IPv6 address it
		// cannot be contacted by IPv4 clients, which is currently a very bad thing,
		// so just do IPv4 under Windows 2003 or earlier.
		if (IsWindowsVistaOrNewer())
		{
			// Attempt to create an IPv6 socket
			Socket s = WSASocket(AF_INET6, type, protocol, 0, 0, WSA_FLAG_OVERLAPPED);

			// If the socket was created,
			if (s != CAT_SOCKET_INVALID)
			{
				// If supporting IPv4 as well,
				if (SupportIPv4)
				{
					// Turn off IPV6_V6ONLY so that IPv4 is able to communicate with the socket also
					int on = 0;
					if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&on, sizeof(on)))
					{
						CloseSocket(s);
						return false;
					}
				}

				out_OnlyIPv4 = false;
				out_s = s;
				return true;
			}
		}

		// Attempt to create an IPv4 socket
		Socket s = WSASocket(AF_INET, type, protocol, 0, 0, WSA_FLAG_OVERLAPPED);

		// If the socket was created,
		if (s != CAT_SOCKET_INVALID)
		{
			out_OnlyIPv4 = true;
			out_s = s;
			return true;
		}

		return false;
	}

	bool NetBind(Socket s, Port port, bool OnlyIPv4)
	{
		if (s == CAT_SOCKET_ERROR)
			return false;

		// Bind socket to port
		sockaddr_in6 addr;
		int addr_len;

		// If only IPv4 is desired,
		if (OnlyIPv4)
		{
			// Fill in IPv4 sockaddr within IPv6 addr
			sockaddr_in *addr4 = reinterpret_cast<sockaddr_in*>( &addr );

			addr4->sin_family = AF_INET;
			addr4->sin_addr.S_un.S_addr = INADDR_ANY;
			addr4->sin_port = htons(port);
			CAT_OBJCLR(addr4->sin_zero);

			addr_len = sizeof(sockaddr_in);
		}
		else
		{
			// Fill in IPv6 sockaddr
			CAT_OBJCLR(addr);
			addr.sin6_family = AF_INET6;
			addr.sin6_addr = in6addr_any;
			addr.sin6_port = htons(port);

			addr_len = sizeof(sockaddr_in6);
		}

		// Attempt to bind
		return 0 == bind(s, reinterpret_cast<sockaddr*>( &addr ), sizeof(addr));
	}

	Port GetBoundPort(Socket s)
	{
        sockaddr_in6 addr;
        int namelen = sizeof(addr);

		// If socket name cannot be determined,
        if (getsockname(s, reinterpret_cast<sockaddr*>( &addr ), &namelen))
            return 0;

		// Port is placed in the same location for IPv4 and IPv6
        return ntohs(addr.sin6_port);
	}

	// Run startup and cleanup functions needed under some OS
	bool StartupSockets()
	{
#if defined(CAT_MS_SOCKET_API)
		WSADATA wsaData;

		// Request Winsock 2.2
		return NO_ERROR == WSAStartup(MAKEWORD(2,2), &wsaData);
#else
		return true;
#endif
	}

	void CleanupSockets()
	{
#if defined(CAT_MS_SOCKET_API)
		WSACleanup();
#endif
	}
}


NetAddr::NetAddr(const char *ip_str, Port port)
{
	// Invoke SetFromString(), ignoring the return value because
	// it will leave the object in an invalid state if needed.
	SetFromString(ip_str, port);
}
NetAddr::NetAddr(const sockaddr_in6 &addr)
{
	Wrap(addr);
}
NetAddr::NetAddr(const sockaddr_in &addr)
{
	Wrap(addr);
}
NetAddr::NetAddr(const sockaddr *addr)
{
	Wrap(addr);
}

NetAddr::NetAddr(const NetAddr &addr)
{
	_valid = addr._valid;
	_ip.v6[0] = addr._ip.v6[0];
	_ip.v6[1] = addr._ip.v6[1];
}
NetAddr &NetAddr::operator=(const NetAddr &addr)
{
	_valid = addr._valid;
	_ip.v6[0] = addr._ip.v6[0];
	_ip.v6[1] = addr._ip.v6[1];
	return *this;
}

bool NetAddr::Wrap(const sockaddr_in6 &addr)
{
	// May be IPv4 that has been stuffed into an IPv6 sockaddr
	return Wrap(reinterpret_cast<const sockaddr*>( &addr ));
}
bool NetAddr::Wrap(const sockaddr_in &addr)
{
	// Can only fit IPv4 in this address structure
	if (addr.sin_family == AF_INET)
	{
		_family = AF_INET;
		_port = ntohs(addr.sin_port);
		_ip.v4 = addr.sin_addr.S_un.S_addr;
		return true;
	}
	else
	{
		_valid = 0;
		return false;
	}
}
bool NetAddr::Wrap(const sockaddr *addr)
{
	u16 family = addr->sa_family;

	// Based on the family of the sockaddr,
	if (family == AF_INET)
	{
		const sockaddr_in *addr4 = reinterpret_cast<const sockaddr_in*>( addr );

		_family = AF_INET;
		_port = ntohs(addr4->sin_port);
		_ip.v4 = addr4->sin_addr.S_un.S_addr;
		return true;
	}
	else if (family == AF_INET6)
	{
		const sockaddr_in6 *addr6 = reinterpret_cast<const sockaddr_in6*>( addr );

		_family = AF_INET6;
		_port = ntohs(addr6->sin6_port);
		memcpy(_ip.v6, &addr6->sin6_addr, sizeof(_ip.v6));
		return true;
	}
	else
	{
		// Other address families not supported, so make object invalid
		_valid = 0;
		return false;
	}
}

bool NetAddr::EqualsIPOnly(const NetAddr &addr) const
{
	// If either address is invalid,
	if (!Valid() || !addr.Valid())
		return false; // "not equal"

	// If one is IPv4 and the other is IPv6,
	if (_family != addr._family)
		return false; // "not equal"

	// Compare IP addresses based on address family:

	if (_family == AF_INET)
	{
		// Compare 32-bit IPv4 addresses
		return _ip.v4 == addr._ip.v4;
	}
	else if (_family == AF_INET6)
	{
		// Compare 128-bit IPv6 addresses
		return 0 == ((_ip.v6[0] ^ addr._ip.v6[0]) |
					 (_ip.v6[1] ^ addr._ip.v6[1]));
	}
	else
	{
		return false; // "not equal"
	}
}
bool NetAddr::operator==(const NetAddr &addr) const
{
	// Check port
	if (addr._port != _port)
		return false; // "not equal"

	// Tail call IP checking function
	return EqualsIPOnly(addr);
}
bool NetAddr::operator!=(const NetAddr &addr) const
{
	return !(*this == addr);
}

bool NetAddr::SetFromString(const char *ip_str, Port port)
{
	// Try to convert from IPv6 address first
	sockaddr_in6 addr6;
	int out_addr_len6 = sizeof(addr6);

	if (!WSAStringToAddress((char*)ip_str, AF_INET6, 0,
							(sockaddr*)&addr6, &out_addr_len6))
	{
		// Copy address from temporary object
		_family = AF_INET6;
		_port = port;
		memcpy(_ip.v6, &addr6.sin6_addr, sizeof(_ip.v6));
		return true;
	}
	else
	{
		// Try to convert from IPv4 address if that failed
		sockaddr_in addr4;
		int out_addr_len4 = sizeof(addr4);

		if (!WSAStringToAddress((char*)ip_str, AF_INET, 0,
								(sockaddr*)&addr4, &out_addr_len4))
		{
			// Copy address from temporary object
			_family = AF_INET;
			_port = port;
			_ip.v4 = addr4.sin_addr.S_un.S_addr;
			return true;
		}
		else
		{
			// Otherwise mark address as invalid and return false
			_valid = 0;
			return false;
		}
	}
}
std::string NetAddr::IPToString() const
{
	if (_family == AF_INET6)
	{
		// Construct an IPv6 sockaddr, with port = 0
		sockaddr_in6 addr6;
		CAT_OBJCLR(addr6);
		addr6.sin6_family = _family;
		memcpy(&addr6.sin6_addr, _ip.v6, sizeof(_ip.v6));

		// Allocate space for address string
		char addr_str6[INET6_ADDRSTRLEN + 32];
		DWORD str_len6 = sizeof(addr_str6);

		// Because inet_ntop() is not supported in Windows XP, only Vista+
		if (CAT_SOCKET_ERROR == WSAAddressToString((sockaddr*)&addr6, sizeof(addr6),
												   0, addr_str6, &str_len6))
			return SocketGetLastErrorString();

		return addr_str6;
	}
	else if (_family == AF_INET)
	{
		// Construct an IPv4 sockaddr, with port = 0
		sockaddr_in addr4;
		CAT_OBJCLR(addr4);
		addr4.sin_family = _family;
		addr4.sin_addr.S_un.S_addr = _ip.v4;

		// Allocate space for address string
		char addr_str4[INET_ADDRSTRLEN + 32];
		DWORD str_len4 = sizeof(addr_str4);

		// Because inet_ntop() is not supported in Windows XP, only Vista+
		if (CAT_SOCKET_ERROR == WSAAddressToString((sockaddr*)&addr4, sizeof(addr4),
												   0, addr_str4, &str_len4))
			return SocketGetLastErrorString();

		return addr_str4;
	}
	else
	{
		// If protocol family is unrecognized,
		return "[Invalid]";
	}
}

bool NetAddr::Unwrap(SockAddr &addr, int &addr_len, bool PromoteToIP6) const
{
	if (_family == AF_INET)
	{
		// If the user wants us to unwrap to an IPv6 address,
		if (PromoteToIP6)
		{
			sockaddr_in6 *addr6 = reinterpret_cast<sockaddr_in6*>( &addr );

			CAT_OBJCLR(*addr6);
			addr6->sin6_family = AF_INET6;
			addr6->sin6_port = htons(_port);

			u32 ipv4 = _ip.v4;

			// If loopback,
			if ((ipv4 & 0x00FFFFFF) == 0x0000007f)
			{
				addr6->sin6_addr.u.Byte[15] = 1;
			}
			else
			{
				addr6->sin6_addr.u.Word[5] = 0xFFFF;
				addr6->sin6_addr.u.Byte[12] = (u8)(ipv4);
				addr6->sin6_addr.u.Byte[13] = (u8)(ipv4 >> 8);
				addr6->sin6_addr.u.Byte[14] = (u8)(ipv4 >> 16);
				addr6->sin6_addr.u.Byte[15] = (u8)(ipv4 >> 24);
			}

			addr_len = sizeof(sockaddr_in6);
		}
		else
		{
			sockaddr_in *addr4 = reinterpret_cast<sockaddr_in*>( &addr );

			addr4->sin_family = AF_INET;
			addr4->sin_port = htons(_port);
			addr4->sin_addr.S_un.S_addr = _ip.v4;
			CAT_OBJCLR(addr4->sin_zero);

			addr_len = sizeof(sockaddr_in);
		}

		return true;
	}
	else if (_family == AF_INET6)
	{
		sockaddr_in6 *addr6 = reinterpret_cast<sockaddr_in6*>( &addr );

		CAT_OBJCLR(*addr6);
		addr6->sin6_family = AF_INET6;
		addr6->sin6_port = htons(_port);
		memcpy(&addr6->sin6_addr, _ip.v6, sizeof(_ip.v6));

		addr_len = sizeof(sockaddr_in6);

		return true;
	}
	else
	{
		return false;
	}
}

// Promote an IPv4 address to an IPv6 address if needed
void NetAddr::PromoteTo6()
{
	if (_family == AF_INET)
	{
		_family = AF_INET6;

		u32 ipv4 = _ip.v4;

		_ip.v6[0] = 0;

		// If loopback,
		if ((ipv4 & 0x00FFFFFF) == 0x0000007f)
		{
			_ip.v6_words[4] = 0;
			_ip.v6_words[5] = 0;
			_ip.v6_bytes[12] = 0;
			_ip.v6_bytes[13] = 0;
			_ip.v6_bytes[14] = 0;
			_ip.v6_bytes[15] = 1;
		}
		else
		{
			_ip.v6_words[4] = 0;
			_ip.v6_words[5] = 0xFFFF;
			_ip.v6_bytes[12] = (u8)(ipv4);
			_ip.v6_bytes[13] = (u8)(ipv4 >> 8);
			_ip.v6_bytes[14] = (u8)(ipv4 >> 16);
			_ip.v6_bytes[15] = (u8)(ipv4 >> 24);
		}
	}
}
