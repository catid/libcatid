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

// 07/18/2009 began

#ifndef CAT_SCHNORR_SIGNATURE_HPP
#define CAT_SCHNORR_SIGNATURE_HPP

#include <cat/crypt/tunnel/KeyAgreement.hpp>
#include <cat/crypt/rand/Fortuna.hpp>

namespace cat {

/*
	Precomputed public key:
	secret random 0 < x < q
	X = xG

	Ephemeral secret:
	secret random 0 < k < q
	e = H(M || kG)
	s = k - xe (mod q)

	Signature: s || e

		256-bit security: Signature = 64 bytes
		384-bit security: Signature = 96 bytes
		512-bit security: Signature = 128 bytes

	e_v = H(M || sG + eX)

	if (e == e_v) { verified! }
*/

class SchnorrSignature : KeyAgreementCommon
{
public:
    bool Sign(BigTwistedEdward *math, FortunaOutput *csprng,
			  const u8 *public_key, int public_bytes, const u8 *private_key, int private_bytes,
			  const u8 *message, int message_bytes, u8 *signature, int signature_bytes);

	bool Verify(BigTwistedEdward *math, FortunaOutput *csprng,
				const u8 *public_key, int public_bytes,
				const u8 *message, int message_bytes, const u8 *signature, int signature_bytes);
};


} // namespace cat

#endif // CAT_SCHNORR_SIGNATURE_HPP
