#include <cat/AllCrypt.hpp>
#include <malloc.h>
using namespace cat;

//// Server-specific stuff

// Zeroes the private key
TwistedEdwardServer::~TwistedEdwardServer()
{
	CAT_OBJCLR(PrivateKey);
}

// Pregenerate the server's private and public keys.  These are shared with clients
// either by being built into the client code or securely delivered via a trusted
// server at runtime.
void TwistedEdwardServer::GenerateOfflineStuff(IRandom *prng,
											   EdPrivateKey *ServerPrivateKey,
											   EdServerPublicKey *ServerPublicKey)
{
	EdPoint BasePoint;

	// Generate affine (x,y) point on the curve
	do {
		// x = random number between 0 and p-1
		prng->Generate(BasePoint.X, sizeof(BasePoint.X));
		SpecialModulus(BasePoint.X, CAT_EDWARD_LIMBS, CAT_EDWARD_C, CAT_EDWARD_LIMBS, BasePoint.X);

		// y = solution of curve from x (very slow!)
		ecc_SolveAffineY(BasePoint.X, BasePoint.Y);
	} while (!ecc_IsValidAffineXY(BasePoint.X, BasePoint.Y));

	// Z = 1
	Set32(BasePoint.Z, CAT_EDWARD_LIMBS, 1);

	// P = 4P
	ecc_DoubleNoT(&BasePoint, &BasePoint); // T unused for this calculation
	ecc_Double(&BasePoint, &BasePoint); // T unused for this one also, but it generates T

	// Generate random private key
	prng->Generate(ServerPrivateKey->Bytes, sizeof(ServerPrivateKey->Bytes));

	// Compute k*P (slow!)
	EdPoint PublicProduct;
	ecc_MultiplyNoT(&BasePoint, ServerPrivateKey->Limbs, &PublicProduct);

	// Convert product and base to affine coordinates for 50% compression
	ecc_GetAffineXY(&PublicProduct, ServerPublicKey->Product_x.Limbs, ServerPublicKey->Product_y.Limbs);
	ecc_GetAffineXY(&BasePoint, ServerPublicKey->BasePoint_x.Limbs, ServerPublicKey->BasePoint_y.Limbs);

	// Convert it all to endian-neutral byte arrays
	SwapLittleEndian(ServerPublicKey->BasePoint_x.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ServerPublicKey->BasePoint_y.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ServerPrivateKey->Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ServerPublicKey->Product_x.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ServerPublicKey->Product_y.Limbs, CAT_EDWARD_LIMBS);
}

// Called on start-up to initialize the object
void TwistedEdwardServer::SetPrivateKey(const EdPrivateKey *ServerPrivateKey)
{
	SwapLittleEndian(PrivateKey, ServerPrivateKey->Limbs, CAT_EDWARD_LIMBS);
}

// Compute a shared secret from a client's public key.  Thread-safe.  False on invalid input.
bool TwistedEdwardServer::ComputeSharedSecret(const EdClientPublicKey *ClientPublicKey, EdSharedSecret *SharedSecret)
{
	// Decompress client's public point
	EdPoint ClientPublicPoint;

	SwapLittleEndian(ClientPublicPoint.X, ClientPublicKey->Product_x.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ClientPublicPoint.Y, ClientPublicKey->Product_y.Limbs, CAT_EDWARD_LIMBS);

	// Verify the point is on the curve (relatively fast)
	if (!ecc_IsValidAffineXY(ClientPublicPoint.X, ClientPublicPoint.Y))
		return false;

	// T = XY
	u32 p[CAT_EDWARD_LIMBS*2];
	Multiply(CAT_EDWARD_LIMBS, p, ClientPublicPoint.X, ClientPublicPoint.Y);
	SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, ClientPublicPoint.T);

	// Z = 1
	Set32(ClientPublicPoint.Z, CAT_EDWARD_LIMBS, 1);

	// Compute k*P (slow!)
	EdPoint SharedProduct;
	ecc_MultiplyNoT(&ClientPublicPoint, PrivateKey, &SharedProduct);

	// Get the affine x coordinate of the product; should be the same one computed by the client
	ecc_GetAffineX(&SharedProduct, SharedSecret->Limbs);

	// Convert it into an endian-neutral byte array
	SwapLittleEndian(SharedSecret->Limbs, CAT_EDWARD_LIMBS);

	return true;
}


//// Client-specific stuff

// Called on start-up to initialize the object
void TwistedEdwardClient::SetServerPublicKey(const EdServerPublicKey *ServerPublicKey)
{
	// Decompress server's public point and the base point
	SwapLittleEndian(BasePoint.X, ServerPublicKey->BasePoint_x.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(BasePoint.Y, ServerPublicKey->BasePoint_y.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ServerPublicPoint.X, ServerPublicKey->Product_x.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ServerPublicPoint.Y, ServerPublicKey->Product_y.Limbs, CAT_EDWARD_LIMBS);

	// T = XY
	u32 p[CAT_EDWARD_LIMBS*2];
	Multiply(CAT_EDWARD_LIMBS, p, BasePoint.X, BasePoint.Y);
	SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, BasePoint.T);
	Multiply(CAT_EDWARD_LIMBS, p, ServerPublicPoint.X, ServerPublicPoint.Y);
	SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, ServerPublicPoint.T);

	// Z = 1
	Set32(BasePoint.Z, CAT_EDWARD_LIMBS, 1);
	Set32(ServerPublicPoint.Z, CAT_EDWARD_LIMBS, 1);
}

// Compute a shared secret and client's public key
void TwistedEdwardClient::ComputeSharedSecret(IRandom *prng,
											  EdClientPublicKey *ClientPublicKey,
											  EdSharedSecret *SharedSecret)
{
	// Generate a private key
	u32 PrivateKey[CAT_EDWARD_LIMBS];
	prng->Generate(PrivateKey, sizeof(PrivateKey));

	// Compute "private key * base point = client public key" (slow!)
	EdPoint ClientProduct;
	ecc_MultiplyNoT(&BasePoint, PrivateKey, &ClientProduct);

	// Get the affine (x,y) product point
	ecc_GetAffineXY(&ClientProduct, ClientPublicKey->Product_x.Limbs, ClientPublicKey->Product_y.Limbs);

	// Convert it into an endian-neutral byte array
	SwapLittleEndian(ClientPublicKey->Product_x.Limbs, CAT_EDWARD_LIMBS);
	SwapLittleEndian(ClientPublicKey->Product_y.Limbs, CAT_EDWARD_LIMBS);

	// Compute "private key * server public = shared secret point" (slow!)
	EdPoint SharedProduct;
	ecc_MultiplyNoT(&ServerPublicPoint, PrivateKey, &SharedProduct);

	// Get the affine x coordinate of the product; should be the same one computed by the server
	ecc_GetAffineX(&SharedProduct, SharedSecret->Limbs);

	// Convert it into an endian-neutral byte array
	SwapLittleEndian(SharedSecret->Limbs, CAT_EDWARD_LIMBS);
}


//// Math

namespace cat
{
	// Returns x = X/Z
	void ecc_GetAffineX(
		const EdPoint *p1,
		u32 *x)
	{
		u32 p[CAT_EDWARD_LIMBS*2], m[CAT_EDWARD_LIMBS], A[CAT_EDWARD_LIMBS];

		// m = modulus
		Set32(m, CAT_EDWARD_LIMBS, 0);
		Subtract32(m, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// A = 1/Z (mod m)
		InvMod(p1->Z, CAT_EDWARD_LIMBS, m, CAT_EDWARD_LIMBS, A);

		// x = X * A
		Multiply(CAT_EDWARD_LIMBS, p, p1->X, A);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, x);
	}

	// Returns (x,y) = (X/Z,Y/Z)
	void ecc_GetAffineXY(
		const EdPoint *p1,
		u32 *x,
		u32 *y)
	{
		u32 p[CAT_EDWARD_LIMBS*2], m[CAT_EDWARD_LIMBS], A[CAT_EDWARD_LIMBS];

		// m = modulus
		Set32(m, CAT_EDWARD_LIMBS, 0);
		Subtract32(m, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// A = 1/Z (mod m)
		InvMod(p1->Z, CAT_EDWARD_LIMBS, m, CAT_EDWARD_LIMBS, A);

		// x = X * A
		Multiply(CAT_EDWARD_LIMBS, p, p1->X, A);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, x);

		// y = Y * A
		Multiply(CAT_EDWARD_LIMBS, p, p1->Y, A);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, y);
	}

	// Solve for Y given the X point on a curve
	void ecc_SolveAffineY(
		const u32 *x,
		u32 *y)
	{
		u32 p[CAT_EDWARD_LIMBS*2], m[CAT_EDWARD_LIMBS];
		u32 A[CAT_EDWARD_LIMBS], B[CAT_EDWARD_LIMBS], C[CAT_EDWARD_LIMBS], D[CAT_EDWARD_LIMBS], E[CAT_EDWARD_LIMBS];

		// m = modulus
		Set32(m, CAT_EDWARD_LIMBS, 0);
		Subtract32(m, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// A = x^2
		Square(CAT_EDWARD_LIMBS, p, x);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, A);

		// B = A + 1
		Set(B, CAT_EDWARD_LIMBS, A);
		Add32(B, CAT_EDWARD_LIMBS, 1);

		// C = d * A
		p[CAT_EDWARD_LIMBS] = Multiply32(CAT_EDWARD_LIMBS, p, A, CAT_EDWARD_D);
		SpecialModulus(p, CAT_EDWARD_LIMBS+1, CAT_EDWARD_C, CAT_EDWARD_LIMBS, C);

		// C = -C
		Negate(CAT_EDWARD_LIMBS, C, C);
		Subtract32(C, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// C = C + 1
		Add32(C, CAT_EDWARD_LIMBS, 1);

		// D = 1/C (mod m)
		InvMod(C, CAT_EDWARD_LIMBS, m, CAT_EDWARD_LIMBS, D);

		// E = B * D
		Multiply(CAT_EDWARD_LIMBS, p, B, D);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, E);

		// return square root
		SpecialSquareRoot(CAT_EDWARD_LIMBS, E, CAT_EDWARD_C, y);
	}

	// Verify that the point (x,y) exists on the given curve
	bool ecc_IsValidAffineXY(
		const u32 *x,
		const u32 *y)
	{
		// 0 = 1 + d*x^2*y^2 + x^2 - y^2
		// 0 = 1 + dAB + A - B
		// 0 = 1 + Z + A - B
		u32 p[CAT_EDWARD_LIMBS*2+1];
		u32 A[CAT_EDWARD_LIMBS], B[CAT_EDWARD_LIMBS], Z[CAT_EDWARD_LIMBS];

		// A = x^2, B = y^2
		Square(CAT_EDWARD_LIMBS, p, x);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, A);
		Square(CAT_EDWARD_LIMBS, p, y);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, B);

		// Z = dAB
		Multiply(CAT_EDWARD_LIMBS, p, A, B);
		p[CAT_EDWARD_LIMBS*2] = Multiply32(CAT_EDWARD_LIMBS*2, p, p, CAT_EDWARD_D);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2+1, CAT_EDWARD_C, CAT_EDWARD_LIMBS, Z);

		// Z += A
		if (Add(Z, CAT_EDWARD_LIMBS, A, CAT_EDWARD_LIMBS))
			Add32(Z, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// Z += 1
		if (Add32(Z, CAT_EDWARD_LIMBS, 1))
			Add32(Z, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// Z -= B
		if (Subtract(Z, CAT_EDWARD_LIMBS, B, CAT_EDWARD_LIMBS))
			Subtract32(Z, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// if Z = 0, success
		if (!LimbDegree(Z, CAT_EDWARD_LIMBS))
			return true;

		// if Z = modulus, success
		Add32(Z, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (!LimbDegree(Z, CAT_EDWARD_LIMBS))
			return true;

		// otherwise it is not on the curve
		return false;
	}

	// Extended Twisted Edwards Negation Formula in 2a
	void ecc_Negate(
	   const EdPoint *p1,
	   EdPoint *p3)
	{
		// -(X : Y : T : Z) = (-X : Y : -T : Z)

		// -X
		Negate(CAT_EDWARD_LIMBS, p3->X, p1->X);
		Subtract32(p3->X, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// Y
		Set(p3->Y, CAT_EDWARD_LIMBS, p1->Y);

		// -T
		Negate(CAT_EDWARD_LIMBS, p3->T, p1->T);
		Subtract32(p3->T, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// Z
		Set(p3->Z, CAT_EDWARD_LIMBS, p1->Z);
	}

	// Extended Twisted Edwards Unified Addition Formula (works when both inputs are the same) in 8M 1d 8a
	void ecc_Add(
	   const EdPoint *p1,
	   const EdPoint *p2,
	   EdPoint *p3)
	{
		u32 A[CAT_EDWARD_LIMBS], B[CAT_EDWARD_LIMBS], C[CAT_EDWARD_LIMBS], D[CAT_EDWARD_LIMBS];
		u32 E[CAT_EDWARD_LIMBS], F[CAT_EDWARD_LIMBS], G[CAT_EDWARD_LIMBS], H[CAT_EDWARD_LIMBS];
		u32 p[CAT_EDWARD_LIMBS*2+1];

		// A = (Y1 - X1) * (Y2 - X2)
		if (Subtract(C, p1->Y, CAT_EDWARD_LIMBS, p1->X, CAT_EDWARD_LIMBS))
			Subtract32(C, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(D, p2->Y, CAT_EDWARD_LIMBS, p2->X, CAT_EDWARD_LIMBS))
			Subtract32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		Multiply(CAT_EDWARD_LIMBS, p, C, D);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, A);

		// B = (Y1 + X1) * (Y2 + X2)
		if (Add(C, p1->Y, CAT_EDWARD_LIMBS, p1->X, CAT_EDWARD_LIMBS))
			Add32(C, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Add(D, p2->Y, CAT_EDWARD_LIMBS, p2->X, CAT_EDWARD_LIMBS))
			Add32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		Multiply(CAT_EDWARD_LIMBS, p, C, D);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, B);

		// C = 2 * d' * T1 * T2 (can remove multiplication by d' if inputs are known to be different)
		Multiply(CAT_EDWARD_LIMBS, p, p1->T, p2->T);
		p[CAT_EDWARD_LIMBS*2] = Multiply32(CAT_EDWARD_LIMBS*2, p, p, CAT_EDWARD_D*2);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2+1, CAT_EDWARD_C, CAT_EDWARD_LIMBS, C);

		// D = 2 * Z1 * Z2
		Multiply(CAT_EDWARD_LIMBS, p, p1->Z, p2->Z);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, D);
		if (ShiftLeft(CAT_EDWARD_LIMBS, D, D, 1))
			Add32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// E = B - A, F = D - C, G = D + C, H = B + A
		if (Subtract(E, B, CAT_EDWARD_LIMBS, A, CAT_EDWARD_LIMBS))
			Subtract32(E, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(F, D, CAT_EDWARD_LIMBS, C, CAT_EDWARD_LIMBS))
			Subtract32(F, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Add(G, D, CAT_EDWARD_LIMBS, C, CAT_EDWARD_LIMBS))
			Add32(G, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Add(H, B, CAT_EDWARD_LIMBS, A, CAT_EDWARD_LIMBS))
			Add32(H, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// X3 = E * F
		Multiply(CAT_EDWARD_LIMBS, p, E, F);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->X);

		// Y3 = G * H
		Multiply(CAT_EDWARD_LIMBS, p, G, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Y);

		// T3 = E * H
		Multiply(CAT_EDWARD_LIMBS, p, E, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->T);

		// Z3 = F * G
		Multiply(CAT_EDWARD_LIMBS, p, F, G);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Z);
	}

	// Extended Twisted Edwards Unified Addition Formula (works when both inputs are the same) in 7M 1d 8a
	void ecc_AddNoT(
	   const EdPoint *p1,
	   const EdPoint *p2,
	   EdPoint *p3)
	{
		u32 A[CAT_EDWARD_LIMBS], B[CAT_EDWARD_LIMBS], C[CAT_EDWARD_LIMBS], D[CAT_EDWARD_LIMBS];
		u32 E[CAT_EDWARD_LIMBS], F[CAT_EDWARD_LIMBS], G[CAT_EDWARD_LIMBS], H[CAT_EDWARD_LIMBS];
		u32 p[CAT_EDWARD_LIMBS*2+1];

		// A = (Y1 - X1) * (Y2 - X2)
		if (Subtract(C, p1->Y, CAT_EDWARD_LIMBS, p1->X, CAT_EDWARD_LIMBS))
			Subtract32(C, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(D, p2->Y, CAT_EDWARD_LIMBS, p2->X, CAT_EDWARD_LIMBS))
			Subtract32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		Multiply(CAT_EDWARD_LIMBS, p, C, D);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, A);

		// B = (Y1 + X1) * (Y2 + X2)
		if (Add(C, p1->Y, CAT_EDWARD_LIMBS, p1->X, CAT_EDWARD_LIMBS))
			Add32(C, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Add(D, p2->Y, CAT_EDWARD_LIMBS, p2->X, CAT_EDWARD_LIMBS))
			Add32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		Multiply(CAT_EDWARD_LIMBS, p, C, D);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, B);

		// C = 2 * d' * T1 * T2 (can remove multiplication by d' if inputs are known to be different)
		Multiply(CAT_EDWARD_LIMBS, p, p1->T, p2->T);
		p[CAT_EDWARD_LIMBS*2] = Multiply32(CAT_EDWARD_LIMBS*2, p, p, CAT_EDWARD_D*2);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2+1, CAT_EDWARD_C, CAT_EDWARD_LIMBS, C);

		// D = 2 * Z1 * Z2
		Multiply(CAT_EDWARD_LIMBS, p, p1->Z, p2->Z);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, D);
		if (ShiftLeft(CAT_EDWARD_LIMBS, D, D, 1))
			Add32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// E = B - A, F = D - C, G = D + C, H = B + A
		if (Subtract(E, B, CAT_EDWARD_LIMBS, A, CAT_EDWARD_LIMBS))
			Subtract32(E, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(F, D, CAT_EDWARD_LIMBS, C, CAT_EDWARD_LIMBS))
			Subtract32(F, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Add(G, D, CAT_EDWARD_LIMBS, C, CAT_EDWARD_LIMBS))
			Add32(G, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Add(H, B, CAT_EDWARD_LIMBS, A, CAT_EDWARD_LIMBS))
			Add32(H, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// X3 = E * F
		Multiply(CAT_EDWARD_LIMBS, p, E, F);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->X);

		// Y3 = G * H
		Multiply(CAT_EDWARD_LIMBS, p, G, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Y);
	/*
		// T3 = E * H
		Multiply(CAT_EDWARD_LIMBS, p, E, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->T);
	*/
		// Z3 = F * G
		Multiply(CAT_EDWARD_LIMBS, p, F, G);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Z);
	}

	// Extended Twisted Edwards Dedicated Doubling Formula in 4M 4S 5a
	void ecc_Double(
	   const EdPoint *p1,
	   EdPoint *p3)
	{
		u32 A[CAT_EDWARD_LIMBS], B[CAT_EDWARD_LIMBS], C[CAT_EDWARD_LIMBS], D[CAT_EDWARD_LIMBS];
		u32 E[CAT_EDWARD_LIMBS], F[CAT_EDWARD_LIMBS], G[CAT_EDWARD_LIMBS], H[CAT_EDWARD_LIMBS];
		u32 p[CAT_EDWARD_LIMBS*2];

		// A = X1^2
		Square(CAT_EDWARD_LIMBS, p, p1->X);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, A);

		// B = Y1^2
		Square(CAT_EDWARD_LIMBS, p, p1->Y);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, B);

		// C = 2 * Z1^2
		Square(CAT_EDWARD_LIMBS, p, p1->Z);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, C);
		if (ShiftLeft(CAT_EDWARD_LIMBS, C, C, 1))
			Add32(C, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// D = -A
		Negate(CAT_EDWARD_LIMBS, D, A);
		Subtract32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// G = D + B, F = G - C, H = D - B = -A - B
		if (Add(G, D, CAT_EDWARD_LIMBS, B, CAT_EDWARD_LIMBS))
			Add32(G, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(F, G, CAT_EDWARD_LIMBS, C, CAT_EDWARD_LIMBS))
			Subtract32(F, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(H, D, CAT_EDWARD_LIMBS, B, CAT_EDWARD_LIMBS))
			Subtract32(H, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// E = (X1 + Y1)^2 - A - B = (X1 + Y1)^2 + H
		if (Add(E, p1->X, CAT_EDWARD_LIMBS, p1->Y, CAT_EDWARD_LIMBS))
			Add32(E, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		Square(CAT_EDWARD_LIMBS, p, E);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, E);
		if (Add(E, CAT_EDWARD_LIMBS, H, CAT_EDWARD_LIMBS))
			Add32(E, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// X3 = E * F
		Multiply(CAT_EDWARD_LIMBS, p, E, F);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->X);

		// Y3 = G * H
		Multiply(CAT_EDWARD_LIMBS, p, G, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Y);

		// T3 = E * H
		Multiply(CAT_EDWARD_LIMBS, p, E, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->T);

		// Z3 = F * G
		Multiply(CAT_EDWARD_LIMBS, p, F, G);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Z);
	}

	// Extended Twisted Edwards Dedicated Doubling Formula in 3M 4S 5a
	void ecc_DoubleNoT(
	   const EdPoint *p1,
	   EdPoint *p3)
	{
		u32 A[CAT_EDWARD_LIMBS], B[CAT_EDWARD_LIMBS], C[CAT_EDWARD_LIMBS], D[CAT_EDWARD_LIMBS];
		u32 E[CAT_EDWARD_LIMBS], F[CAT_EDWARD_LIMBS], G[CAT_EDWARD_LIMBS], H[CAT_EDWARD_LIMBS];
		u32 p[CAT_EDWARD_LIMBS*2];

		// A = X1^2
		Square(CAT_EDWARD_LIMBS, p, p1->X);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, A);

		// B = Y1^2
		Square(CAT_EDWARD_LIMBS, p, p1->Y);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, B);

		// C = 2 * Z1^2
		Square(CAT_EDWARD_LIMBS, p, p1->Z);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, C);
		if (ShiftLeft(CAT_EDWARD_LIMBS, C, C, 1))
			Add32(C, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// D = -A
		Negate(CAT_EDWARD_LIMBS, D, A);
		Subtract32(D, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// G = D + B, F = G - C, H = D - B = -A - B
		if (Add(G, D, CAT_EDWARD_LIMBS, B, CAT_EDWARD_LIMBS))
			Add32(G, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(F, G, CAT_EDWARD_LIMBS, C, CAT_EDWARD_LIMBS))
			Subtract32(F, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		if (Subtract(H, D, CAT_EDWARD_LIMBS, B, CAT_EDWARD_LIMBS))
			Subtract32(H, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// E = (X1 + Y1)^2 - A - B = (X1 + Y1)^2 + H
		if (Add(E, p1->X, CAT_EDWARD_LIMBS, p1->Y, CAT_EDWARD_LIMBS))
			Add32(E, CAT_EDWARD_LIMBS, CAT_EDWARD_C);
		Square(CAT_EDWARD_LIMBS, p, E);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, E);
		if (Add(E, CAT_EDWARD_LIMBS, H, CAT_EDWARD_LIMBS))
			Add32(E, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

		// X3 = E * F
		Multiply(CAT_EDWARD_LIMBS, p, E, F);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->X);

		// Y3 = G * H
		Multiply(CAT_EDWARD_LIMBS, p, G, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Y);
	/*
		// T3 = E * H
		Multiply(CAT_EDWARD_LIMBS, p, E, H);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->T);
	*/
		// Z3 = F * G
		Multiply(CAT_EDWARD_LIMBS, p, F, G);
		SpecialModulus(p, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, p3->Z);
	}

#if defined(CAT_WMOF_TABLE_CODE)
	struct {
		u8 add_index; // nth odd number to add: 0=0,1=1,2=3,3=5,4=7,...
		u8 doubles_after; // number of doubles to perform after add
	} MOF_LUT[128] = {
	{0,0},{0,1},{1,0},{0,2},{2,0},{1,1},{3,0},{0,3},
	{4,0},{2,1},{5,0},{1,2},{6,0},{3,1},{7,0},{0,4},
	{8,0},{4,1},{9,0},{2,2},{10,0},{5,1},{11,0},{1,3},
	{12,0},{6,1},{13,0},{3,2},{14,0},{7,1},{15,0},{0,5},
	{16,0},{8,1},{17,0},{4,2},{18,0},{9,1},{19,0},{2,3},
	{20,0},{10,1},{21,0},{5,2},{22,0},{11,1},{23,0},{1,4},
	{24,0},{12,1},{25,0},{6,2},{26,0},{13,1},{27,0},{3,3},
	{28,0},{14,1},{29,0},{7,2},{30,0},{15,1},{31,0},{0,6},
	{32,0},{16,1},{33,0},{8,2},{34,0},{17,1},{35,0},{4,3},
	{36,0},{18,1},{37,0},{9,2},{38,0},{19,1},{39,0},{2,4},
	{40,0},{20,1},{41,0},{10,2},{42,0},{21,1},{43,0},{5,3},
	{44,0},{22,1},{45,0},{11,2},{46,0},{23,1},{47,0},{1,5},
	{48,0},{24,1},{49,0},{12,2},{50,0},{25,1},{51,0},{6,3},
	{52,0},{26,1},{53,0},{13,2},{54,0},{27,1},{55,0},{3,4},
	{56,0},{28,1},{57,0},{14,2},{58,0},{29,1},{59,0},{7,3},
	{60,0},{30,1},{61,0},{15,2},{62,0},{31,1},{63,0},{0,7}
	};
#endif

	// Extended Edwards Scalar Multiplication: product = k*P
	void ecc_MultiplyNoT(
		const EdPoint *P,	// Base point
		const u32 *k,		// Scalar multiplicand
		EdPoint *product)	// Result
	{
		const int w = 6;

		// Precompute the table
		int precomp_size = 1 << (w - 2);
		EdPoint *precomp = (EdPoint*)alloca(2 * precomp_size * sizeof(EdPoint));

		// Precompute P and -P
		memcpy(precomp, P, sizeof(EdPoint));
		ecc_Negate(precomp, precomp+precomp_size);

		// Precompute 2b
		EdPoint doubled;
		ecc_Double(P, &doubled);

		// Precompute +/- odd multiples of b by iteratively adding 2b
		EdPoint *last = precomp;
		int ctr = precomp_size;
		while (--ctr)
		{
			ecc_Add(last, &doubled, last+1);
			++last;
			ecc_Negate(last, last+precomp_size);
		}

		// Begin multiplication loop
		bool seen_high_bit = false;
		int leg = CAT_EDWARD_LIMBS - 1;
		u32 bits, bits_mask = ((1 << (w + 1)) - 1);
		u32 last_leg = k[leg--];
		int offset = w, doubles_before = 0, doubles_skip = 0;

		for (;;)
		{
			// If still processing bits from current leg of k,
			if (offset <= 32)
			{
				// Select next bits from current leg of k
				bits = last_leg >> (32 - offset);
			}
			else if (leg >= 0)
			{
				// Next bits straddle the previous and next legs of k
				u32 new_leg = k[leg--];
				offset -= 32;
				bits = (last_leg << offset) | (new_leg >> (32 - offset));
				last_leg = new_leg;
			}
			else if (offset < w + 32)
			{
				// Pad zeroes on the right
				bits = last_leg << (offset - 32);

				// Skip padding - 1 doubles after leaving this loop
				doubles_skip = offset - 32 - 1;
			}
			else break;

			// Mask out garbage bits, leaving only the w+1 bit window behind
			bits &= bits_mask;

			// Invert low bits if negative, and mask out high bit
			u32 z = (bits ^ -(s32)(bits >> w)) & ((1 << w) - 1);

#if !defined(CAT_WMOF_TABLE_CODE)
			// Perform shift and subtract to get positive index
			u32 x = z - (z >> 1);
			// Compute number of trailing zeroes in x (specialized for the known range of inputs)
			u32 y = x ^ (x - 1);
			u32 shift = ((15 - y) & 16) >> 2;
			y >>= shift;
			u32 s = shift;
			shift = ((3 - y) & 4) >> 1; y >>= shift; s |= shift;
			s |= (y >> 1);
#endif

			if (!z)
			{
				doubles_before += w;
#if defined(CAT_SIDE_CHANNEL_PROTECTION)
				// Side-Channel Attack protection: Make sure that the runtime of
				// scalar multiplication does not depend on whether or not there
				// is a long run of zeroes (6+) in the secret key.
				EdPoint devnull;
				ecc_AddNoT(precomp, &devnull, &devnull);
#endif
			}
			else
			{
#if defined(CAT_WMOF_TABLE_CODE)
				// Extract the operation for this table entry
				z = (z - 1) >> 1;
				EdPoint *precomputed_point = precomp + MOF_LUT[z].add_index + ((bits & (1 << w)) >> 2);
				int doubles_after = MOF_LUT[z].doubles_after;
#else
				// compute table index
				EdPoint *precomputed_point = precomp + (((x >> s) - 1) >> 1) + ((bits & (1 << w)) >> 2);
				int doubles_after = s;
#endif

				// If we have seen the high bit yet,
				if (seen_high_bit)
				{
					// Perform doubles before addition
					doubles_before += w - doubles_after;

					// There will always be at least one doubling to perform here
					while (--doubles_before)
						ecc_DoubleNoT(product, product);
					ecc_Double(product, product);

					// Perform addition or subtraction from the precomputed table
					ecc_AddNoT(precomputed_point, product, product);
				}
				else
				{
					// On the first seen bit, product = precomputed point, skip leading doubles
					memcpy(product, precomputed_point, sizeof(EdPoint));

#if defined(CAT_SIDE_CHANNEL_PROTECTION)
					// Side-Channel Attack protection: Make sure that the runtime of scalar
					// multiplication doesn't depend on the number of skipped leading doubles.
					int leading_doubles = w - doubles_after;
					EdPoint devnull;
					while (--leading_doubles)
						ecc_DoubleNoT(product, &devnull);
#endif

					seen_high_bit = true;
				}

				// Accumulate doubles after addition
				doubles_before = doubles_after;
			}

			// set up offset for next time around
			offset += w;
		}

		// Skip some doubles at the end due to window underrun
		if (doubles_before > doubles_skip)
		{
			doubles_before -= doubles_skip;

			// Perform trailing doubles
			while (doubles_before--);
				ecc_DoubleNoT(product, product);
		}
	}
}
