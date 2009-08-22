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

#include <cat/crypt/tunnel/KeyAgreementInitiator.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/port/AlignedAlloc.hpp>
using namespace cat;

bool KeyAgreementInitiator::AllocateMemory()
{
    FreeMemory();

    B = new (Aligned::ii) Leg[KeyLegs * 13];
    a = B + KeyLegs*4;
    A = a + KeyLegs;
    hB = A + KeyLegs*4;

    return !!B;
}

void KeyAgreementInitiator::FreeMemory()
{
    if (B)
    {
        memset(a, 0, KeyBytes);
        Aligned::Delete(B);
        B = 0;
    }

	if (G_MultPrecomp)
	{
		Aligned::Delete(G_MultPrecomp);
		G_MultPrecomp = 0;
	}
}

KeyAgreementInitiator::KeyAgreementInitiator()
{
    B = 0;
    G_MultPrecomp = 0;
}

KeyAgreementInitiator::~KeyAgreementInitiator()
{
    FreeMemory();
}

bool KeyAgreementInitiator::Initialize(BigTwistedEdward *math, const u8 *responder_public_key, int public_bytes)
{
	if (!math) return false;
	int bits = math->RegBytes() * 8;

    // Validate and accept number of bits
    if (!KeyAgreementCommon::Initialize(bits))
        return false;

    // Allocate memory space for the responder's key pair and generator point
    if (!AllocateMemory())
        return false;

    // Verify that inputs are of the correct length
    if (public_bytes != KeyBytes*2) return false;

	// Precompute an 8-bit table for multiplication
	G_MultPrecomp = math->PtMultiplyPrecompAlloc(math->GetGenerator(), 8);
    if (!G_MultPrecomp) return false;

    // Unpack the responder's key pair and generator point
    if (!math->LoadVerifyAffineXY(responder_public_key, responder_public_key + KeyBytes, B))
        return false;

    // hB = h * B for small subgroup attack resistance
    math->PtDoubleZ1(B, hB);
    math->PtEDouble(hB, hB);

    return true;
}

bool KeyAgreementInitiator::GenerateChallenge(BigTwistedEdward *math, FortunaOutput *csprng,
											  u8 *initiator_challenge, int challenge_bytes)
{
    // Verify that inputs are of the correct length
    if (challenge_bytes != KeyBytes*2) return false;

    // a = initiator private key
	GenerateKey(math, csprng, a);

    // A = a * G
    math->PtMultiply(G_MultPrecomp, 8, a, 0, A);
    math->PtNormalize(A, A);

    math->SaveAffineXY(A, initiator_challenge, initiator_challenge + KeyBytes);

    return true;
}

bool KeyAgreementInitiator::ProcessAnswer(BigTwistedEdward *math,
										  const u8 *responder_answer, int answer_bytes,
                                          AuthenticatedEncryption *encryption)
{
    // Verify that inputs are of the correct length
    if (answer_bytes != KeyBytes*3) return false;

    Leg *Y = math->Get(0);
    Leg *S = math->Get(4);
    Leg *T = math->Get(8);
    Leg *hY = math->Get(12);

    // Unpack the responder's h*Y into registers
    if (!math->LoadVerifyAffineXY(responder_answer, responder_answer + KeyBytes, Y))
        return false;

	// Verify the point is not the additive identity (should never happen unless being attacked)
	if (math->IsAffineIdentity(Y))
		return false;

    // hY = h * Y for small subgroup attack resistance
    math->PtDoubleZ1(Y, hY);
    math->PtEDouble(hY, hY);

    // S = a * (hB + hY)
    math->PtEAdd(hB, hY, T);
    math->PtMultiply(T, a, 0, S);

    // T = affine X coordinate in endian-neutral form
    math->SaveAffineX(S, T);

#if defined(CAT_ENDIAN_LITTLE)

    // k = H(T,A,B,Y)
    encryption->SetKey(KeyBytes, T, A, B, Y, true);

#else // !defined(CAT_ENDIAN_LITTLE)

    u8 A_big[MAX_BYTES*2], B_big[MAX_BYTES*2], Y_big[MAX_BYTES*2];
    math->SaveProjectiveXY(A, A_big, A_big + KeyBytes);
    math->SaveProjectiveXY(B, B_big, B_big + KeyBytes);
    math->SaveProjectiveXY(Y, Y_big, Y_big + KeyBytes);

    // k = H(T,A,B,Y)
    encryption->SetKey(KeyBytes, T, A_big, B_big, Y_big, true);

#endif

    // Validate the server proof of key
    return encryption->ValidateProof(responder_answer + KeyBytes*2, KeyBytes);
}
