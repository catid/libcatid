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
#include <cat/port/AlignedAlloc.hpp>
using namespace cat;

bool KeyAgreementResponder::AllocateMemory()
{
    FreeMemory();

    b = new (Aligned::ii) Leg[KeyLegs * 7];
    B = b + KeyLegs;
	B_neutral = B + KeyLegs*2;

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

	if (G_MultPrecomp)
	{
		Aligned::Delete(G_MultPrecomp);
		G_MultPrecomp = 0;
	}
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

bool KeyAgreementResponder::Initialize(BigTwistedEdwards *math,
									   const u8 *responder_public_key, int public_bytes,
									   const u8 *responder_private_key, int private_bytes)
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
    if (private_bytes != KeyBytes) return false;
    if (public_bytes != KeyBytes*2) return false;

	// Precompute an 8-bit table for multiplication
	G_MultPrecomp = math->PtMultiplyPrecompAlloc(math->GetGenerator(), 8);
    if (!G_MultPrecomp) return false;

    // Unpack the responder's key pair and generator point
    math->Load(responder_private_key, KeyBytes, b);
    if (!math->LoadVerifyAffineXY(responder_public_key, responder_public_key+KeyBytes, B))
        return false;
    math->PtUnpack(B);

	// Store a copy of the endian-neutral version of B for later
	memcpy(B_neutral, responder_public_key, KeyBytes*2);

    return true;
}

bool KeyAgreementResponder::ProcessChallenge(BigTwistedEdwards *math, FortunaOutput *csprng,
											 const u8 *initiator_challenge, int challenge_bytes,
                                             u8 *responder_answer, int answer_bytes, Skein *key_hash)
{
    // Verify that inputs are of the correct length
#if defined(DEBUG)
    if (challenge_bytes != KeyBytes*2 || answer_bytes != KeyBytes*3)
        return false;
#endif

    Leg *A = math->Get(0);
    Leg *y = math->Get(4);
    Leg *Y = math->Get(5);
    Leg *S = math->Get(9);
    Leg *T = math->Get(13);
    Leg *hA = math->Get(17);

    // Unpack the initiator's A into registers
    if (!math->LoadVerifyAffineXY(initiator_challenge, initiator_challenge + KeyBytes, A))
        return false;

	// Verify the point is not the additive identity (should never happen unless being attacked)
	if (math->IsAffineIdentity(A))
		return false;

    // hA = h * A for small subgroup attack resistance
    math->PtDoubleZ1(A, hA);
    math->PtEDouble(hA, hA);

    // y = ephemeral key
	GenerateKey(math, csprng, y);

    // Y = y * G
    math->PtMultiply(G_MultPrecomp, 8, y, 0, Y);
	math->SaveAffineXY(Y, responder_answer, responder_answer + KeyBytes);

	do
	{
		// random n-bit number r
		csprng->Generate(responder_answer + KeyBytes*2, KeyBytes);

		// S = H(A,B,Y,r)
		if (!key_hash->BeginKey(KeyBits))
			return false;
		key_hash->Crunch(initiator_challenge, KeyBytes*2); // A
		key_hash->Crunch(B_neutral, KeyBytes*2); // B
		key_hash->Crunch(responder_answer, KeyBytes*3); // Y,r
		key_hash->End();
		key_hash->Generate(S, KeyBytes);
		math->Load(S, KeyBytes, S);

		// Repeat while S is small (rare)
	} while (math->LessX(S, 1000));

	// T = S*y + b (mod q)
	math->MulMod(S, y, math->GetCurveQ(), T);
	if (math->Add(b, T, T))
		math->Subtract(T, math->GetCurveQ(), T);
	while (!math->Less(T, math->GetCurveQ()))
		math->Subtract(T, math->GetCurveQ(), T);

	// T = AffineX(T * hA)
	math->PtMultiply(hA, T, 0, S);
    math->SaveAffineX(S, T);

	// k = H(d,T)
	if (!key_hash->BeginKDF())
		return false;
	key_hash->Crunch(T, KeyBytes);
	key_hash->End();
}

bool KeyAgreementResponder::Sign(BigTwistedEdwards *math, FortunaOutput *csprng,
								 const u8 *message, int message_bytes,
								 u8 *signature, int signature_bytes)
{
    // Verify that inputs are of the correct length
    if (signature_bytes != KeyBytes*2) return false;

    Leg *k = math->Get(0);
    Leg *K = math->Get(1);
    Leg *e = math->Get(5);
    Leg *s = math->Get(6);
    Leg *be = math->Get(7);

    // k = ephemeral key
	GenerateKey(math, csprng, k);

	// K = k * G
	math->PtMultiply(G_MultPrecomp, 8, k, 0, K);
	math->SaveAffineX(K, K);

	do {

		do {
			// e = H(M || K)
			Skein H;

			if (!H.BeginKey(KeyBits)) return false;
			H.Crunch(message, message_bytes);
			H.Crunch(K, KeyBytes);
			H.End();
			H.Generate(signature, KeyBytes);

			math->Load(signature, KeyBytes, e);

			// e = e (mod q), for checking if it is congruent to q
			while (!math->Less(e, math->GetCurveQ()))
				math->Subtract(e, math->GetCurveQ(), e);

		} while (math->IsZero(e));

		// s = b * e (mod q)
		math->MulMod(b, e, math->GetCurveQ(), s);

		// s = -s (mod q)
		if (!math->IsZero(s)) math->Subtract(math->GetCurveQ(), s, s);

		// s = s + k (mod q)
		if (math->Add(s, k, s))
			while (!math->Subtract(s, math->GetCurveQ(), s));
		while (!math->Less(s, math->GetCurveQ()))
			math->Subtract(s, math->GetCurveQ(), s);

	} while (math->IsZero(s));

	math->Save(s, signature + KeyBytes, KeyBytes);

	return true;
}
