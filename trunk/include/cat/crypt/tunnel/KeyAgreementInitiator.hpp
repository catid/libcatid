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

#ifndef CAT_KEY_AGREEMENT_INITIATOR_HPP
#define CAT_KEY_AGREEMENT_INITIATOR_HPP

#include <cat/crypt/tunnel/KeyAgreement.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>

namespace cat {


class KeyAgreementInitiator : public KeyAgreementCommon
{
    Leg *B; // Responder's public key (pre-shared with initiator)
    Leg *a; // Initiator's private key (kept secret)
    Leg *A; // Initiator's public key (shared with responder in Challenge message)
    Leg *hB; // h*B
    Leg *G_MultPrecomp; // 8-bit table for multiplication
    Leg *B_MultPrecomp; // 8-bit table for multiplication

    bool AllocateMemory();
    void FreeMemory();

public:
    KeyAgreementInitiator();
    ~KeyAgreementInitiator();

    bool Initialize(BigTwistedEdwards *math,
					const u8 *responder_public_key, int public_bytes);

public:
    bool GenerateChallenge(BigTwistedEdwards *math, FortunaOutput *csprng,
						   u8 *initiator_challenge, int challenge_bytes);

    bool ProcessAnswer(BigTwistedEdwards *math,
					   const u8 *responder_answer, int answer_bytes,
                       AuthenticatedEncryption *encryption);

public:
	bool Verify(BigTwistedEdwards *math, FortunaOutput *csprng,
				const u8 *message, int message_bytes,
				const u8 *signature, int signature_bytes);
};


} // namespace cat

#endif // CAT_KEY_AGREEMENT_INITIATOR_HPP
