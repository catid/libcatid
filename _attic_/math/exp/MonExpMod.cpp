#include <cat/math/BigInt.hpp>
#include <malloc.h>

namespace cat
{
	// Computes: result = base ^ exponent (Mod modulus)
	// Using Montgomery multiplication with sliding window method
	// Base parameter must be a Montgomery Residue created with MonInputResidue()
	void MonExpMod(
		const u32 *base,	//	Base for exponentiation, buffer size = mod_limbs
		const u32 *exponent,//	Exponent, buffer size = exponent_limbs
		int exponent_limbs,	//	Number of limbs in exponent
		const u32 *modulus,	//	Modulus, buffer size = mod_limbs
		int mod_limbs,		//	Number of limbs in modulus
		u32 mod_inv,		//	MonReducePrecomp() return
		u32 *result)		//	Result, buffer size = mod_limbs
	{
		// Calculate the number of window bits to use (decent approximation..)
		int window_bits = Degree32(exponent_limbs);

		// If the window bits are too small, might as well just use left-to-right S&M method
		if (window_bits < 4)
		{
			SimpleMonExpMod(base, exponent, exponent_limbs, modulus, mod_limbs, mod_inv, result);
			return;
		}

		// Precompute a window of the size determined above
		u32 *window = ExpPrecomputeWindow(base, modulus, mod_limbs, mod_inv, window_bits);

		bool seen_bits = false;
		u32 e_bits, trailing_zeroes, used_bits = 0;

		u32 *temp = (u32*)alloca((mod_limbs*2)*4);

		for (int ii = exponent_limbs - 1; ii >= 0; --ii)
		{
			u32 e_i = exponent[ii];

			int wordbits = 32;
			while (wordbits--)
			{
				// If we have been accumulating bits,
				if (used_bits)
				{
					// If this new bit is set,
					if (e_i >> 31)
					{
						e_bits <<= 1;
						e_bits |= 1;

						trailing_zeroes = 0;
					}
					else // the new bit is unset
					{
						e_bits <<= 1;

						++trailing_zeroes;
					}

					++used_bits;

					// If we have used up the window bits,
					if (used_bits == window_bits)
					{
						// Select window index 1011 from "101110"
						u32 window_index = e_bits >> (trailing_zeroes + 1);

						if (seen_bits)
						{
							u32 ctr = used_bits - trailing_zeroes;
							while (ctr--)
							{
								// result = result^2
								Square(mod_limbs, temp, result);
								MonReduce(mod_limbs, temp, modulus, mod_inv, result);
							}

							// result = result * window[index]
							Multiply(mod_limbs, temp, result, &window[window_index * mod_limbs]);
							MonReduce(mod_limbs, temp, modulus, mod_inv, result);
						}
						else
						{
							// result = window[index]
							Set(result, mod_limbs, &window[window_index * mod_limbs]);
							seen_bits = true;
						}

						while (trailing_zeroes--)
						{
							// result = result^2
							Square(mod_limbs, temp, result);
							MonReduce(mod_limbs, temp, modulus, mod_inv, result);
						}

						used_bits = 0;
					}
				}
				else
				{
					// If this new bit is set,
					if (e_i >> 31)
					{
						used_bits = 1;
						e_bits = 1;
						trailing_zeroes = 0;
					}
					else // the new bit is unset
					{
						// If we have processed any bits yet,
						if (seen_bits)
						{
							// result = result^2
							Square(mod_limbs, temp, result);
							MonReduce(mod_limbs, temp, modulus, mod_inv, result);
						}
					}
				}

				e_i <<= 1;
			}
		}

		if (used_bits)
		{
			// Select window index 1011 from "101110"
			u32 window_index = e_bits >> (trailing_zeroes + 1);

			if (seen_bits)
			{
				u32 ctr = used_bits - trailing_zeroes;
				while (ctr--)
				{
					// result = result^2
					Square(mod_limbs, temp, result);
					MonReduce(mod_limbs, temp, modulus, mod_inv, result);
				}

				// result = result * window[index]
				Multiply(mod_limbs, temp, result, &window[window_index * mod_limbs]);
				MonReduce(mod_limbs, temp, modulus, mod_inv, result);
			}
			else
			{
				// result = window[index]
				Set(result, mod_limbs, &window[window_index * mod_limbs]);
				//seen_bits = true;
			}

			while (trailing_zeroes--)
			{
				// result = result^2
				Square(mod_limbs, temp, result);
				MonReduce(mod_limbs, temp, modulus, mod_inv, result);
			}

			//e_bits = 0;
		}

		delete []window;
	}
}
