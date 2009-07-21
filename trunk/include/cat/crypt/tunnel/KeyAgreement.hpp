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

// 07/20/2009 working!
// 07/18/2009 began

#ifndef CAT_KEY_AGREEMENT_HPP
#define CAT_KEY_AGREEMENT_HPP

#include <cat/math/BigTwistedEdward.hpp>

namespace cat {


/*
	Tunnel Key Agreement "Tabby" protocol:

	Provides perfect forward secrecy if server private key is revealed
	Using Elliptic Curve Cryptography over finite field Fq, cofactor h

	Here the protocol initiator is the (c)lient, and the responder is the (s)erver:

		s: private key b, public key B=b*G

		256-bit security: B = 64 bytes for public key,  b = 32 bytes for private key
		384-bit security: B = 96 bytes for public key,  b = 48 bytes for private key
		512-bit security: B = 128 bytes for public key, b = 64 bytes for private key

		c: Client already knows the server's public key B before Key Agreement
		c: ephemeral private key a, ephemeral public key A=a*G

	Initiator Challenge: c2s A

		256-bit security: A = 64 bytes
		384-bit security: A = 96 bytes
		512-bit security: A = 128 bytes

		s: validate A, ignore invalid

		s: ephemeral key y
		s: Y = y * G
		s: T = (b + y) * h*A
		s: k = H(T,A,B,Y)

	Responder Answer: s2c Y || MAC(k) {"responder proof"}

		256-bit security: Y(64by)  MAC(32by) = 96 bytes
		384-bit security: Y(96by)  MAC(48by) = 144 bytes
		512-bit security: Y(128by) MAC(64by) = 192 bytes

		c: validate Y, ignore invalid

		c: T = a * (h*B + h*Y)
		c: k = H(T,A,B,Y)

		c: validate MAC, ignore invalid

	Initiator Proof: c2s MAC(k) {"initiator proof"}

		This packet can also include the client's first encrypted message

		256-bit security: MAC(32by) = 32 bytes
		384-bit security: MAC(48by) = 48 bytes
		512-bit security: MAC(64by) = 64 bytes

		s: validate MAC, ignore invalid
*/


//// KeyAgreementCommon

class KeyAgreementCommon
{
public:
	// Math library register usage
	static const int ECC_REG_OVERHEAD = 31;

	// C: field prime modulus (2^bits - C)
	// D: curve (yy-xx=1+Dxxyy)
	static const int EDWARD_C_256 = 189;
	static const int EDWARD_D_256 = 321;
	static const int EDWARD_C_384 = 317;
	static const int EDWARD_D_384 = 2857;
	static const int EDWARD_C_512 = 569;
	static const int EDWARD_D_512 = 3042;

	// Limits on field prime
	static const int MAX_BITS = 512;
	static const int MAX_BYTES = MAX_BITS / 8;
	static const int MAX_LEGS = MAX_BYTES / sizeof(Leg);

protected:
	int KeyBits, KeyBytes, KeyLegs;

	bool Initialize(int bits);
	BigTwistedEdward *InstantiateMath();
	BigTwistedEdward *GetLocalMath();

public:
	static void DeleteLocalMath();
};


} // namespace cat

#endif // CAT_KEY_AGREEMENT_HPP
