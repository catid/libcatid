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
// 10/08/08 fix typo

#ifndef CAT_COOKIE_JAR_HPP
#define CAT_COOKIE_JAR_HPP

#include <cat/Platform.hpp>

namespace cat {


class CookieJar
{
	static const int EXPIRE_TIME = 4000; // ms
	static const int BIN_COUNT = 16; // power of 2
	static const int BIN_TIME = EXPIRE_TIME / BIN_COUNT;
	static const int BIN_MASK = BIN_COUNT - 1;

	u32 key[16];

	u32 Salsa6(u32 *x);

	u32 Hash(u32 ip, u16 port, u32 epoch);
	u32 Hash(const void *address_info, int bytes, u32 epoch);

	u32 GetEpoch();
	u32 ReconstructEpoch(u32 cookie);

public:
	// Initialize to a random 512-bit key on startup
	void Initialize();

	// Thread-safe and lock-free
	u32 Generate(u32 ip, u16 port);
	u32 Generate(const void *address_info, int bytes); // bytes <= 48

	// Thread-safe and lock-free
	bool Verify(u32 ip, u16 port, u32 cookie);
	bool Verify(const void *address_info, int bytes, u32 cookie); // bytes <= 48
};


} // namespace cat

#endif // CAT_COOKIE_JAR_HPP
