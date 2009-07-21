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

#ifndef CAT_SECURE_SERVER_DEMO_HPP
#define CAT_SECURE_SERVER_DEMO_HPP

#include <cat/AllTunnel.hpp>
#include <map>

namespace cat {

class Address;
class Connection;
class SecureClientDemo;
class SecureServerDemo;


#define CAT_C2S_HELLO_BYTES 4
#define CAT_S2C_COOKIE_BYTES 4


// ip,port address pair
class Address
{
public:
    u32 ip;
    u16 port;

    Address()
    {
    }

    Address(const Address &addr)
    {
        ip = addr.ip;
        port = addr.port;
    }

    Address(u32 cip, u16 cport)
    {
        ip = cip;
        port = cport;
    }

    Address &operator=(const Address &rhs)
    {
        ip = rhs.ip;
        port = rhs.port;
        return *this;
    }

    bool operator<(const Address &rhs) const
    {
        if (ip < rhs.ip) return true;
        if (ip > rhs.ip) return false;
        return port < rhs.port;
    }

    bool operator==(const Address &rhs) const
    {
        return ip == rhs.ip && port == rhs.port;
    }

    bool operator!=(const Address &rhs) const
    {
        return ip != rhs.ip || port != rhs.port;
    }
};


// Connection context for a connected client
class Connection
{
    friend class SecureServerDemo;

    Address client_addr;
    AuthenticatedEncryption auth_enc;
    bool seen_proof;

public:
    Connection(const Address &caddr)
        : client_addr(caddr)
    {
        seen_proof = false;
    }
};


// Secure server demo object
class SecureServerDemo
{
    SecureClientDemo *client_ref;
    CookieJar cookie_jar;

    KeyAgreementResponder tun_server;
    std::map<Address, Connection*> connections;
    Address my_addr;

protected:
    void OnHello(const Address &source, u8 *buffer);
    void OnChallenge(const Address &source, u8 *buffer);

    void OnSessionMessage(Connection *client, u8 *buffer, int bytes);

public:
    void Reset(SecureClientDemo *client_ref, const u8 *server_public_key, const u8 *server_private_key);
    void OnPacket(const Address &source, u8 *buffer, int bytes);

    Address GetAddress() { return my_addr; }
};


} // namespace cat

#endif // CAT_SECURE_SERVER_DEMO_HPP
