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

#include <cat/crypt/tunnel/SchnorrSignature.hpp>
using namespace cat;

bool SchnorrSignature::Sign(BigTwistedEdward *math, FortunaOutput *csprng,
							const u8 *public_key, int public_bytes, const u8 *private_key, int private_bytes,
							const u8 *message, int message_bytes, u8 *signature, int signature_bytes)
{
	if (!math) return false;
	int bits = math->RegBytes() * 8;

    // Validate and accept number of bits
    if (!KeyAgreementCommon::Initialize(bits))
        return false;

    // Verify that inputs are of the correct length
    if (private_bytes != KeyBytes) return false;
    if (public_bytes != KeyBytes*4) return false;
    if (signature_bytes != KeyBytes*2) return false;

    Leg *x = math->Get(0);
    Leg *G = math->Get(1);
    Leg *X = math->Get(5);
    if (!math->LoadVerifyAffineXY(public_key, public_key + KeyBytes, G))
        return false;
    if (!math->LoadVerifyAffineXY(public_key + KeyBytes*2, public_key + KeyBytes*3, X))
        return false;
	math->Load(private_key, KeyBytes, x);

	// secret random 0 < k < q
    Leg *k = math->Get(0);
    Leg *K = math->Get(9);

    do csprng->Generate(k, KeyBytes);
    while (math->LegsUsed(k) < math->Legs());

	math->PtMultiply(G, k, 0, K);
	math->SaveAffineX(K, K);

	// e = H(M || kG)
    Leg *e = math->Get(13);
	Skein H;
	if (!H.BeginKey(bits)) return false;
	H.Crunch(message, message_bytes);
	H.Crunch(K, KeyBytes);
	H.End();
	H.Generate(signature, KeyBytes);
	math->Load(signature, KeyBytes, e);

	// s = k - xe (mod q)
    Leg *s = math->Get(13);
    Leg *xe = math->Get(14);
	math->Multiply(x, e, xe);
}

bool SchnorrSignature::Verify(BigTwistedEdward *math, FortunaOutput *csprng,
							  const u8 *public_key, int public_bytes,
							  const u8 *message, int message_bytes, const u8 *signature, int signature_bytes)
{
	return false;
}
