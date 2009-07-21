#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// root = sqrt(square)
	// Based on Newton-Raphson iteration: root_n+1 = (root_n + square/root_n) / 2
	// Doubles number of correct bits each iteration
	// Precondition: The high limb of square is non-zero
	// Returns false if it was unable to determine the root
	bool SquareRoot(
		int limbs,			// Number of limbs in root
		const u32 *square,	// Square to root, size = limbs * 2
		u32 *root)			// Output root, size = limbs
	{
		u32 *q = (u32*)alloca(limbs*2*4);
		u32 *r = (u32*)alloca((limbs+1)*4);

		// Take high limbs of square as the initial root guess
		Set(root, limbs, square + limbs);

		int ctr = 64;
		while (ctr--)
		{
			// {q, r} = square / root
			Divide(square, limbs*2, root, limbs, q, r);

			// root = (root + q) / 2, assuming high limbs of q = 0
			Add(q, limbs+1, root, limbs);

			// Round division up to the nearest bit
			// Fixes a problem where root is off by 1
			if (q[0] & 1) Add32(q, limbs+1, 2);

			ShiftRight(limbs+1, q, q, 1);

			// Return success if there was no change
			if (Equal(limbs, q, root))
				return true;

			// Else update root and continue
			Set(root, limbs, q);
		}

		// In practice only takes about 9 iterations, as many as 31
		// Varies slightly as number of limbs increases but not by much
		return false;
	}
}
