#include <cat/crypt/publickey/TwistedEdward.hpp>
#include <cat/crypt/rand/Fortuna.hpp>
using namespace cat;


//// Common stuff

bool TwistedEdwardCommon::Initialize(int bits)
{
	// Restrict the bits to pre-defined security levels
	switch (bits)
	{
	case 256:
	case 384:
	case 512:
		KeyBits = bits;
		KeyBytes = KeyBits / 8;
		KeyLegs = KeyBytes / sizeof(Leg);
		return true;
	}

	return false;
}

BigTwistedEdward *TwistedEdwardCommon::InstantiateMath(int bits)
{
	switch (bits)
	{
	case 256:	return new BigTwistedEdward(ECC_OVERHEAD, bits, EDWARD_C_256, EDWARD_D_256);
	case 384:	return new BigTwistedEdward(ECC_OVERHEAD, bits, EDWARD_C_384, EDWARD_D_384);
	case 512:	return new BigTwistedEdward(ECC_OVERHEAD, bits, EDWARD_C_512, EDWARD_D_512);
	default:	return 0;
	}
}

static CAT_TLS BigTwistedEdward *TLS_MathLib = 0;

BigTwistedEdward *TwistedEdwardCommon::GetThreadLocalMath(int bits)
{
	if (TLS_MathLib) return TLS_MathLib;
	return TLS_MathLib = InstantiateMath(bits);
}

void TwistedEdwardCommon::DeleteThreadLocalMath()
{
	if (TLS_MathLib)
	{
		delete TLS_MathLib;
		TLS_MathLib = 0;
	}
}


//// Server-specific stuff

TwistedEdwardServer::~TwistedEdwardServer()
{
	CAT_OBJCLR(PrivateKey);
}

void TwistedEdwardServer::GenerateOfflineStuff(int bits, u8 *ServerPrivateKey, u8 *ServerPublicKey)
{
	BigTwistedEdward *math = TwistedEdwardCommon::GetThreadLocalMath(bits);
	FortunaOutput *csprng = FortunaFactory::GetLocalOutput();

	int key_bytes = bits / 8;

	Leg *a = math->Get(0);
	Leg *G = math->Get(1);
	Leg *A = math->Get(5);

	// Generate a safe generator point on the curve
	math->PtGenerate(csprng, G);

	// Generate random private key
	do csprng->Generate(a, key_bytes);
	while (math->LegsUsed(a) < math->Legs());

	// Compute A = aG (slow!)
	math->PtMultiply(G, a, A);

	// Convert A and G to affine coordinates for 50% compression
	math->Save(a, ServerPrivateKey, key_bytes);
	math->SaveAffineXY(G, ServerPublicKey, ServerPublicKey + key_bytes);
	math->SaveAffineXY(A, ServerPublicKey + key_bytes*2, ServerPublicKey + key_bytes*3);
}

void TwistedEdwardServer::SetPrivateKey(const u8 *ServerPrivateKey)
{
	BigTwistedEdward *math = TwistedEdwardCommon::GetThreadLocalMath(KeyBits);

	math->Load(ServerPrivateKey, KeyBytes, PrivateKey);
}

bool TwistedEdwardServer::ComputeSharedSecret(const u8 *ClientPublicKey, u8 *SharedSecret)
{
	BigTwistedEdward *math = TwistedEdwardCommon::GetThreadLocalMath(KeyBits);

	Leg *B = math->Get(0);
	Leg *SS = math->Get(4);

	// Load and verify the point is on the curve (relatively fast)
	if (!math->LoadVerifyAffineXY(ClientPublicKey, ClientPublicKey + KeyBytes, B))
		return false;

	// B = hB, h=4
	math->PtDoubleZ1(B, B);
	math->PtEDouble(B, B);

	// Compute SS = aB (slow!)
	math->PtMultiply(B, PrivateKey, SS);

	// Get the affine x coordinate of the product; should be the same one computed by the client
	// Convert it into an endian-neutral byte array
	math->SaveAffineX(SS, SharedSecret);

	return true;
}


//// Client-specific stuff

bool TwistedEdwardClient::ComputeSharedSecret(const u8 *ServerPublicKey,
											  u8 *ClientPublicKey,
											  u8 *SharedSecret)
{
	BigTwistedEdward *math = TwistedEdwardCommon::GetThreadLocalMath(KeyBits);

	if (KeyLegs != math->Legs())
		return false;

	Leg *b = math->Get(0);
	Leg *A = math->Get(1);
	Leg *G = math->Get(5);
	Leg *B = math->Get(9);
	Leg *SS = math->Get(13);

	if (!math->LoadVerifyAffineXY(ServerPublicKey, ServerPublicKey + KeyBytes, G))
		return false;
	if (!math->LoadVerifyAffineXY(ServerPublicKey + KeyBytes*2, ServerPublicKey + KeyBytes*3, A))
		return false;

	math->PtUnpack(G);

	// A = hA, h=4
	math->PtDoubleZ1(A, A);
	math->PtEDouble(A, A);

	FortunaOutput *csprng = FortunaFactory::GetLocalOutput();
	do csprng->Generate(b, KeyBytes);
	while (math->LegsUsed(b) < math->Legs());

	math->PtMultiply(G, b, B);
	math->PtMultiply(A, b, SS);

	math->SaveAffineXY(B, ClientPublicKey, ClientPublicKey + KeyBytes);
	math->SaveAffineX(SS, SharedSecret);

	return true;
}
