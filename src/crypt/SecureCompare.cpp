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

#include <cat/crypt/SecureCompare.hpp>

namespace cat {

bool SecureEqual(const u8 *A, const u8 *B, int bytes)
{
	u64 fail = 0;

	// Accumulate failures, 8 bytes at a time
	int qwords = bytes >> 3;

	if (qwords)
	{
		const u64 *A64 = (const u64 *)A;
		const u64 *B64 = (const u64 *)B;

		for (int ii = 0; ii < qwords; ++ii)
			fail |= A64[ii] ^ B64[ii];

		A = (const u8 *)&A64[qwords];
		B = (const u8 *)&B64[qwords];
	}

	// Accumulate failures, bytes at a time
	bytes &= 7;

	switch (bytes)
	{
	case 7: fail |= A[6] ^ B[6];
	case 6: fail |= A[5] ^ B[5];
	case 5: fail |= A[4] ^ B[4];
	case 4: fail |= *(const u32 *)A ^ *(const u32 *)B;
		break;
	case 3: fail |= A[2] ^ B[2];
	case 2: fail |= A[1] ^ B[1];
	case 1: fail |= A[0] ^ B[0];
	}

	return fail == 0;
}

} // namespace cat
