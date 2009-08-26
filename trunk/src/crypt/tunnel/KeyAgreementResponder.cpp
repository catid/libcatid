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
#include <cat/time/Clock.hpp>
#include <cat/threads/Atomic.hpp>
using namespace cat;

bool KeyAgreementResponder::AllocateMemory()
{
    FreeMemory();

    b = new (Aligned::ii) Leg[KeyLegs * 15];
    B = b + KeyLegs;
	B_neutral = B + KeyLegs*2;
	y[0] = B_neutral + KeyLegs*2;
	y[1] = y[0] + KeyLegs;
	Y_neutral[0] = y[1] + KeyLegs;
	Y_neutral[1] = Y_neutral[0] + KeyLegs*4;

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

#include <iostream>
using namespace std;

void KeyAgreementResponder::Rekey(BigTwistedEdwards *math, FortunaOutput *csprng)
{
	u32 NextY = ActiveY ^ 1;

	// y = ephemeral key
	GenerateKey(math, csprng, y[NextY]);

	// Y = y * G
	Leg *Y = Y_neutral[NextY];
	math->PtMultiply(G_MultPrecomp, 8, y[NextY], 0, Y);
	math->SaveAffineXY(Y, Y, Y + KeyLegs);

	ActiveY = NextY;

	Atomic::Set(&ChallengeCount, 0);
}

bool KeyAgreementResponder::Initialize(BigTwistedEdwards *math, FortunaOutput *csprng,
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
	G_MultPrecomp = math->PtMultiplyPrecompAlloc(8);
    if (!G_MultPrecomp) return false;
    math->PtMultiplyPrecomp(math->GetGenerator(), 8, G_MultPrecomp);

    // Unpack the responder's key pair and generator point
    math->Load(responder_private_key, KeyBytes, b);
    if (!math->LoadVerifyAffineXY(responder_public_key, responder_public_key+KeyBytes, B))
        return false;
    math->PtUnpack(B);

	// Store a copy of the endian-neutral version of B for later
	memcpy(B_neutral, responder_public_key, KeyBytes*2);

	// Initialize re-keying
	ChallengeCount = 0;
	ActiveY = 0;
	Rekey(math, csprng);

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
    Leg *Y = math->Get(4);
    Leg *S = math->Get(8);
    Leg *T = math->Get(12);
    Leg *hA = math->Get(16);

    // Unpack the initiator's A into registers
    if (!math->LoadVerifyAffineXY(initiator_challenge, initiator_challenge + KeyBytes, A))
        return false;

	// Verify the point is not the additive identity (should never happen unless being attacked)
	if (math->IsAffineIdentity(A))
		return false;

    // hA = h * A for small subgroup attack resistance
    math->PtDoubleZ1(A, hA);
    math->PtEDouble(hA, hA);

	// Check if it is time to rekey
	if (Atomic::Add(&ChallengeCount, 1) == 100)
		Rekey(math, csprng);

	// Copy the current endian neutral Y to the responder answer
	u32 ThisY = ActiveY;
	memcpy(responder_answer, Y_neutral[ThisY], KeyBytes*2);

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

		// Repeat while S is small
	} while (math->LessX(S, 1000));

	// T = S*y + b (mod q)
	math->MulMod(S, y[ThisY], math->GetCurveQ(), T);
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

	return true;
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
