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

#ifndef CAT_SECURE_CLIENT_DEMO_HPP
#define CAT_SECURE_CLIENT_DEMO_HPP

#include "SecureServerDemo.hpp"

namespace cat {


// Secure client demo object
class SecureClientDemo
{
	SecureServerDemo *server_ref;

	Address server_addr, my_addr;
	bool connected;
	KeyAgreementInitiator tun_client;
	AuthenticatedEncryption auth_enc;

protected:
	void OnCookie(u8 *buffer);
	void OnAnswer(u8 *buffer);
	void OnConnect();
	void OnSessionMessage(u8 *buffer, int bytes);

public:
	void Reset(SecureServerDemo *server_ref, const u8 *server_public_key);
	void SendHello();
	void OnPacket(const Address &source, u8 *buffer, int bytes);

	Address GetAddress() { return my_addr; }

	bool success;
};


} // namespace cat

#endif // CAT_SECURE_CLIENT_DEMO_HPP
