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

#ifndef CAT_KEY_AGREEMENT_RESPONDER_HPP
#define CAT_KEY_AGREEMENT_RESPONDER_HPP

#include <cat/crypt/tunnel/KeyAgreement.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>

namespace cat {


class KeyAgreementResponder : public KeyAgreementCommon
{
    Leg *b; // Responder's private key (kept secret)
    Leg *B; // Responder's public key (pre-shared with initiator)
    Leg *G; // Generator point (pre-shared with initiator as part of public key)

    Leg *G_MultPrecomp;

    bool AllocateMemory();
    void FreeMemory();

public:
    KeyAgreementResponder();
    ~KeyAgreementResponder();

    bool Initialize(int bits, const u8 *responder_public_key, int public_bytes,
                              const u8 *responder_private_key, int private_bytes);

    bool ProcessChallenge(const u8 *initiator_challenge, int challenge_bytes,
                          u8 *responder_answer, int answer_bytes,
                          AuthenticatedEncryption *encryption);
};


} // namespace cat

#endif // CAT_KEY_AGREEMENT_RESPONDER_HPP
