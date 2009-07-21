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

// 07/11/09 I realized that I had not done this yet

#ifndef SECURE_COMPARE_HPP
#define SECURE_COMPARE_HPP

#include <cat/Platform.hpp>

namespace cat {


// Binary comparison function that is resistant to side-channel attack
bool SecureEqual(const u8 *A, const u8 *B, int bytes);


} // namespace cat

#endif // SECURE_COMPARE_HPP
