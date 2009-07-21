#include <cat/math/BigInt.hpp>

namespace cat
{

#if defined(CAT_ENDIAN_BIG)

	// Flip the byte order only on big endian systems
	void SwapLittleEndian(u32 *to, const u32 *from, int limbs)
	{
		for (int ii = 0; ii < limbs; ++ii)
			to[ii] = getLE(from[ii]);
	}

	// Flip the byte order only on big endian systems
	void SwapLittleEndian(u32 *inplace, int limbs)
	{
		for (int ii = 0; ii < limbs; ++ii)
			swapLE(inplace[ii]);
	}

#endif // CAT_ENDIAN_BIG

}
