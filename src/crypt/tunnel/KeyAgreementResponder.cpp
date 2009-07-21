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

#include <cat/crypt/tunnel/KeyAgreementResponder.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/rand/Fortuna.hpp>
#include <cat/port/AlignedAlloc.hpp>
using namespace cat;

bool KeyAgreementResponder::AllocateMemory()
{
    FreeMemory();

    b = Aligned::New<Leg>(KeyLegs * 9);
    B = b + KeyLegs;
    G = B + KeyLegs * 4;

    return !!b;
}

void KeyAgreementResponder::FreeMemory()
{
    if (b)
    {
        memset(b, 0, KeyBytes);
        Aligned::Delete(b);
        b = 0;
    }

    Aligned::Delete(G_MultPrecomp);
    G_MultPrecomp = 0;
}

KeyAgreementResponder::KeyAgreementResponder()
{
    b = 0;
    G_MultPrecomp = 0;
}

KeyAgreementResponder::~KeyAgreementResponder()
{
    FreeMemory();
}

bool KeyAgreementResponder::Initialize(int bits, const u8 *responder_public_key, int public_bytes, const u8 *responder_private_key, int private_bytes)
{
    // Validate and accept number of bits
    if (!KeyAgreementCommon::Initialize(bits))
        return false;

    // Allocate memory space for the responder's key pair and generator point
    if (!AllocateMemory())
        return false;

    // Verify that inputs are of the correct length
    if (private_bytes != KeyBytes) return false;
    if (public_bytes != KeyBytes*4) return false;

    // Create a math object
    BigTwistedEdward *math = GetLocalMath();
    if (!math) return false;

    // Unpack the responder's key pair and generator point
    math->Load(responder_private_key, KeyBytes, b);
    if (!math->LoadVerifyAffineXY(responder_public_key, responder_public_key+KeyBytes, G))
        return false;
    if (!math->LoadVerifyAffineXY(responder_public_key+KeyBytes*2, responder_public_key+KeyBytes*3, B))
        return false;
    math->PtUnpack(G);
    math->PtUnpack(B);

    G_MultPrecomp = math->PtMultiplyPrecompAlloc(G, 8);
    if (!G_MultPrecomp) return false;

    return true;
}

bool KeyAgreementResponder::ProcessChallenge(const u8 *initiator_challenge, int challenge_bytes,
                                             u8 *responder_answer, int answer_bytes,
                                             AuthenticatedEncryption *encryption)
{
    // Verify that inputs are of the correct length
    if (challenge_bytes != KeyBytes*4 || answer_bytes != KeyBytes*3)
        return false;

    // Create a math object
    BigTwistedEdward *math = GetLocalMath();
    if (!math) return false;

    Leg *A = math->Get(0);
    Leg *y = math->Get(4);
    Leg *Y = math->Get(5);
    Leg *S = math->Get(9);
    Leg *T = math->Get(13);
    Leg *hA = math->Get(17);

    // Unpack the initiator's A into registers
    if (!math->LoadVerifyAffineXY(initiator_challenge, initiator_challenge + KeyBytes, A))
        return false;

    // hA = h * A for small subgroup attack resistance
    math->PtDoubleZ1(A, hA);
    math->PtEDouble(hA, hA);

    // Create a PRNG
    FortunaOutput *csprng = FortunaFactory::GetLocalOutput();
    if (!csprng) return false;

    // y = ephemeral key
    do csprng->Generate(y, KeyBytes);
    while (math->LegsUsed(y) < math->Legs());

    // Y = y * G
    math->PtMultiply(G_MultPrecomp, 8, y, 0, Y);
    math->PtNormalize(Y, Y);

    // S = hA * (b + y)
    u8 msb = math->Add(b, y, T);
    math->PtMultiply(hA, T, msb, S);

    // T = affine X coordinate in endian-neutral form
    math->SaveAffineX(S, T);

#if defined(CAT_ENDIAN_LITTLE)

    // k = H(T,A,B,Y)
    encryption->SetKey(KeyBytes, T, A, B, Y, false);

#else // defined(CAT_ENDIAN_LITTLE)

    u8 A_big[MAX_BYTES*2], B_big[MAX_BYTES*2], Y_big[MAX_BYTES*2];
    math->SaveProjectiveXY(A, A_big, A_big + KeyBytes);
    math->SaveProjectiveXY(B, B_big, B_big + KeyBytes);
    math->SaveProjectiveXY(Y, Y_big, Y_big + KeyBytes);

    // k = H(T,A,B,Y)
    encryption->SetKey(KeyBytes, T, A_big, B_big, Y_big, false);

#endif // defined(CAT_ENDIAN_LITTLE)

    // Write response
    math->SaveAffineXY(Y, responder_answer, responder_answer + KeyBytes);
    return encryption->GenerateProof(responder_answer + KeyBytes*2, KeyBytes);
}
