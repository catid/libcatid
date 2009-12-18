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
	// Initialize from IPv6 address
	_family = AF_INET6;
	_port = ntohs(addr.sin6_port);
	memcpy(_ip.v6, &addr.sin6_addr, sizeof(_ip.v6));
	return true;
}
bool NetAddr::Wrap(const sockaddr_in &addr)
{
	// Initialize from IPv4 address
	_family = AF_INET;
	_port = ntohs(addr.sin_port);
	_ip.v4 = addr.sin_addr.S_un.S_addr;
	return true;
}
bool NetAddr::Wrap(const sockaddr *addr)
{
	u16 family = addr->sa_family;

	// Based on the family of the sockaddr,
	if (family == AF_INET)
	{
		const sockaddr_in *addr4 = reinterpret_cast<const sockaddr_in*>( addr );
		return Wrap(*addr4);
	}
	else if (family == AF_INET6)
	{
		const sockaddr_in *addr6 = reinterpret_cast<const sockaddr_in*>( addr );
		return Wrap(*addr6);
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
	if (!Valid() || addr.Valid())
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
		if (SOCKET_ERROR == WSAAddressToString((sockaddr*)&addr6, sizeof(addr6),
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
		if (SOCKET_ERROR == WSAAddressToString((sockaddr*)&addr4, sizeof(addr4),
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

bool NetAddr::Unwrap(SockAddr &addr, int &addr_len) const
{
	if (_family == AF_INET)
	{
		sockaddr_in *addr4 = reinterpret_cast<sockaddr_in*>( &addr );

		addr4->sin_family = AF_INET;
		addr4->sin_port = htons(_port);
		addr4->sin_addr.S_un.S_addr = _ip.v4;
		CAT_OBJCLR(addr4->sin_zero);

		addr_len = sizeof(sockaddr_in);

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
