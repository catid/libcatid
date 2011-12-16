/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAT_SMALL_PRNG_HPP
#define CAT_SMALL_PRNG_HPP

#include "Platform.hpp"

namespace cat {


/*
	Notes on combining generators:

	All LCG, MWC, and XORS generators are safe to combine with simple addition
	since the periods of all of the generators here are relatively prime.
	In this case the overall period will be the sum of the periods.

	If you need to achieve a period of 2^^X, then the period of the generators
	should be at least 2^^(3X).  So, combine MWC with XORS or LCG to make a
	generator that would be good for 2^^32 output numbers.
*/

/*
	Linear Congruential Generator (LCG) with power-of-two modulus

	Guidelines:
		M = 2^^b
		A = Chosen so that A - 1 is a multiple of 4, since M is a multiple of 4
			For other M, A - 1 should be divisible by all prime factors of M
		C = Relatively prime to M
			So it should be odd
			And I think it should be close to M in magnitude

	Output: b bits
	Period: 2^^b
	Issues:
		Lower bits have lower period, and lowest bit alternates
*/
template <u32 A, u32 C>
class LCG32
{
	u32 _x;

public:
	CAT_INLINE void Initialize(u32 seed)
	{
		_x = seed;
	}

	CAT_INLINE void MixSeed(u32 seed)
	{
		Next();
		_x ^= seed;
	}

	CAT_INLINE u32 Next()
	{
		return (_x = A * _x + C);
	}
};

// 64-bit version:
template <u64 A, u64 C>
class LCG64
{
	u64 _x;

public:
	CAT_INLINE void Initialize(u64 seed)
	{
		_x = seed;
	}

	CAT_INLINE void MixSeed(u64 seed)
	{
		Next();
		_x ^= seed;
	}

	CAT_INLINE u64 Next()
	{
		return (_x = A * _x + C);
	}
};


/*
	from "TABLES OF LINEAR CONGRUENTIAL GENERATORS OF DIFFERENT SIZES AND GOOD LATTICE STRUCTURE" (1999)
	by Pierre L'ecuyer
*/
typedef LCG32<2891336453, 1234567897> LecuyerLCG32_1;
typedef LCG32<29943829, 1234567897> LecuyerLCG32_2;
typedef LCG32<32310901, 1234567897> LecuyerLCG32_3;
typedef LCG64<2862933555777941757ULL, 7891234567891234567ULL> LecuyerLCG64_1;
typedef LCG64<3202034522624059733ULL, 7891234567891234567ULL> LecuyerLCG64_2;
typedef LCG64<3935559000370003845ULL, 7891234567891234567ULL> LecuyerLCG64_3;


/*
	Multiply With Carry (MWC) PRNG
	by George Marsaglia

	Guidelines:
		B = 2^^32 (base)
		A = Chosen such that A*B-1 and A*B/2 -1 are prime

	Output: 32 bits
	Period: [2^^32 * A] / 2 - 1
	Issues:
		Will get stuck if both M and C are zero
		High bits tend to be less random
*/
template <u64 A, u32 M0, u32 C0>
class MWC
{
	u32 _m, _c;

public:
	CAT_INLINE void Initialize(u32 seed)
	{
		_m = M0 ^ seed;
		_c = C0;
	}

	CAT_INLINE void MixSeed(u32 seed)
	{
		Next();
		_m ^= seed;

		if (_m == 0 && _c == 0)
			Initialize(seed);
	}

	CAT_INLINE u32 Next()
	{
		u64 t = A * _m + _c;
		_m = (u32)t;
		_c = (u32)(t >> 32);
		return _m;
	}
};

/*
	Maximal safe prime version, and the maximum period version
	from the wikipedia article

	MaxSafeMWC period = 9223371654602686463 (prime)
	MaximalMWC period = 9223371873646018559 = 773 * 1621 * 7360837163623
*/
typedef MWC<4294967118ULL, 21987643, 1732654> MaxSafeMWC;
typedef MWC<4294967220ULL, 21987643, 1732654> MaximalMWC;
/*
	from "Good Practice in (Pseudo) Random Number Generation for Bioinformatics Applications" (2010)
	by David Jones

	DJonesMWC1 period = 9222549758923505663 (prime)
	DJonesMWC2 period = 9119241012177272831 (prime)
*/
typedef MWC<4294584393ULL, 43219876, 6543217> DJonesMWC1;
typedef MWC<4246477509ULL, 21987643, 1732654> DJonesMWC2;


/*
	Type-I XOR Shift Linear Feedback Shift Register (LFSR) PRNG
	from "Xorshift RNGs" (2003)
	by George Marsaglia

	Guidelines:
		Choose shifts A,B,C from Marsaglia's comprehensive list

	Output: b bits
	Period: 2^^b - 1
		32-bit period factors = 3×5×17×257×65537
		64-bit period factors = 3×5×17×257×641×65537×6700417
	Issues:
		Halts on zero
		Linear relationship between blocks of b + 1 consecutive bits
*/
template <typename T, int A, int B, int C>
class XORShift
{
	T _x;

public:
	CAT_INLINE void Initialize(T seed)
	{
		_x = seed;

		if (_x == 0)
			_x = ~(T)0;
	}

	CAT_INLINE void MixSeed(T seed)
	{
		Next();
		_x += seed;

		if (_x == 0)
			Initialize(seed);
	}

	CAT_INLINE T Next()
	{
		register T x = _x;
		x ^= x << A;
		x ^= x >> B;
		x ^= x << C;
		return (_x = x);
	}
};

/*
	Chose these at random from the list
*/
typedef XORShift<u32, 5, 7, 22> XORShift32_1;	// Used in JKISS32 and AsgKISS
typedef XORShift<u32, 8, 7, 23> XORShift32_2;
typedef XORShift<u32, 3, 13, 7> XORShift32_3;
typedef XORShift<u64, 21, 17, 30> XORShift64_1;	// Used in JLKISS64
typedef XORShift<u64, 17, 23, 29> XORShift64_2;
typedef XORShift<u64, 16, 21, 35> XORShift64_3;


/*
	Weyl Generator PRNG
	from "Some long-period random number generators using shifts and xor" (2007)
	by Richard. P. Brent

	Guidelines:
		A = Odd, close to 2^^(b-1) * (sqrt(5) - 1)
		For b=32, close to 2654435769
		For b=64, close to 11400714819323198485
		Weak generator for combining with other generators.

	Output: b bits
	Period: 2^^b
	Issues:
		Horrible in general
*/
template <u32 A>
class WeylGenerator32
{
	u32 _x;

public:
	CAT_INLINE void Initialize(u32 seed)
	{
		_x = seed;
	}

	CAT_INLINE void MixSeed(u32 seed)
	{
		Next();
		_x ^= seed;
	}

	CAT_INLINE u32 Next()
	{
		return (_x += A);
	}
};

// 64-bit version:
template <u64 A>
class WeylGenerator64
{
	u64 _x;

public:
	CAT_INLINE void Initialize(u64 seed)
	{
		_x = seed;
	}

	CAT_INLINE void MixSeed(u64 seed)
	{
		Next();
		_x ^= seed;
	}

	CAT_INLINE u64 Next()
	{
		return (_x += A);
	}
};

// Close to choice criterion from Brent
typedef WeylGenerator32<2654435769> Weyl32_1;
typedef WeylGenerator64<11400714819323198485ULL> Weyl64_1;

/*
	from "Good Practice in (Pseudo) Random Number Generation for Bioinformatics Applications" (2010)
	by David Jones
*/
typedef WeylGenerator32<1411392427> Weyl32_2;


/*
	Add With Carry (AWC) PRNG
	by George Marsaglia

	Weak generator for combining with other generators.

	Output: 32 bits
	Period: <2^^28 with random seeding, ~2^^31 with chosen values
	Issues:
		Cannot be seeded without seriously affecting the period
		Horrible in general
*/
template <u32 Z0, u32 C0>
class AWC
{
	u32 _z, _c;

public:
	CAT_INLINE void Initialize(u32 seed)
	{
		_z = Z0;
		_c = C0;
	}

	CAT_INLINE void MixSeed(u32 seed)
	{
	}

	CAT_INLINE u32 Next()
	{
		u32 t = ((_z + _c) & 0x7FFFFFFF) + (_c >> 31);
		_z = _c;
		_c = t;
		return t;
	}
};

/*
	Factors 3×5×17×257×641×65537×6700417 cannot be combined with XORShift

	After a short random search I came up with these values:
		2741480657 yields combined period 2741480657 from (z=3284958323, c=2208763121)
		1991279629 yields combined period 1991279629 from (z=433678300, c=3220706408)
		1957051087 yields combined period 1957051087 from (z=1034995322, c=3764933876)
*/
typedef AWC<3284958323, 2208763121> AWC32_1;
typedef AWC<433678300, 3220706408> AWC32_2;
typedef AWC<1034995322, 3764933876> AWC32_3;


/*
	Single-bit LFSR PRNG

	Guidelines:
		Choose taps wisely

	Output: b bits
	Period: 2^^b - 1
	Issues:
		Halts on zero
		Horrible in general
*/
template <u32 TAP_MASK>
class SingleBitLFSR32
{
	u32 _x;

public:
	CAT_INLINE void Initialize(u32 seed)
	{
		_x = seed;

		if (_x == 0)
			_x = ~(u32)0;
	}

	CAT_INLINE void MixSeed(u32 seed)
	{
		Next();
		_x += seed;

		if (_x == 0)
			Initialize(seed);
	}

	CAT_INLINE bool Next()
	{
		_x = (_x >> 1) ^ (-(s32)(_x & 1) & TAP_MASK); 
		return (_x & 1) != 0;
	}
};

// 64-bit version:
template <u64 TAP_MASK>
class SingleBitLFSR64
{
	u64 _x;

public:
	CAT_INLINE void Initialize(u64 seed)
	{
		_x = seed;

		if (_x == 0)
			_x = ~(u64)0;
	}

	CAT_INLINE void MixSeed(u64 seed)
	{
		Next();
		_x += seed;

		if (_x == 0)
			Initialize(seed);
	}

	CAT_INLINE bool Next()
	{
		_x = (_x >> 1) ^ (-(_x & 1) & TAP_MASK); 
		return (_x & 1) != 0;
	}
};

/*
	From an LFSR taps table floating around the net
	32-bit characteristic polynomial: x^32 + x^22 + x + 1
	64-bit characteristic polynomial: x^64 + x^63 + x^61 + x^60
*/
typedef SingleBitLFSR32<0x80200003> SingleBitLFSR32_1;
typedef SingleBitLFSR64<0xD800000000000000ULL> SingleBitLFSR64_1;

/*
	From Wikipedia
	Characteristic polynomial: x^32 + x^31 + x^29 + x + 1
*/
typedef SingleBitLFSR32<0xD0000001> SingleBitLFSR32_2;


/*
	Catid's KISS with LFSR

	Read notes on combining generators for proper usage.

	Always adds in generator 1 result.
	Uses an LFSR to gate generators 2 and 3.
*/
template<typename T, class LFSR, class G1, class G2, class G3>
class CKISSL
{
	LFSR _lfsr;
	G1 _g1;
	G2 _g2;
	G3 _g3;

public:
	void Initialize(T seed)
	{
		_lfsr.Initialize(seed);
		_g1.Initialize(seed);
		_g2.Initialize(seed);
		_g3.Initialize(seed);
	}

	void MixSeed(T seed)
	{
		_lfsr.MixSeed(seed);
		_g1.MixSeed(seed);
		_g2.MixSeed(seed);
		_g3.MixSeed(seed);
	}

	T Next()
	{
		T result = _g1.Next();

		if (_lfsr.Next())
			result += _g2.Next();
		else
			result += _g3.Next();

		return result;
	}
};

/*
	Period of ~2^^128

	Good for making the generator harder to analyze from its output.

	Passes all BigCrush tests.

	Catid32L_1: Generator operates at 119 million numbers / second
*/
typedef CKISSL<u32, SingleBitLFSR32_2, MaxSafeMWC, XORShift32_1, LecuyerLCG32_1> CatidL32_1;


/*
	Catid's KISS

	Read notes on combining generators for proper usage.

	Mixes results from all generators.
*/
template<typename T, class G1, class G2, class G3>
class CKISS
{
	G1 _g1;
	G2 _g2;
	G3 _g3;

public:
	void Initialize(T seed)
	{
		_g1.Initialize(seed);
		_g2.Initialize(seed);
		_g3.Initialize(seed);
	}

	void MixSeed(T seed)
	{
		_g1.MixSeed(seed);
		_g2.MixSeed(seed);
		_g3.MixSeed(seed);
	}

	T Next()
	{
		return _g1.Next() + _g2.Next() + _g3.Next();
	}
};

/*
	Period of ~2^^127

	Fails BigCrush tests:
		23  ClosePairs mNP2S, t = 5         0.9994

	Catid32_1: Generator operates at 249 million numbers / second
*/
typedef CKISS<u32, MaxSafeMWC, XORShift32_1, LecuyerLCG32_1> Catid32_1;
/*
	Period of ~2^^127

	Fails BigCrush tests:

	Catid32_1a: Generator operates at 228 million numbers / second
*/
typedef CKISS<u32, MaximalMWC, XORShift32_1, LecuyerLCG32_1> Catid32_1a;
/*
	Period of ~2^^127

	Fails BigCrush tests:

	Catid32_1b: Generator operates at 248 million numbers / second
*/
typedef CKISS<u32, MaxSafeMWC, XORShift32_2, LecuyerLCG32_1> Catid32_1b;
/*
	Period of ~2^^127

	Fails BigCrush tests:

	Catid32_1c: Generator operates at 259 million numbers / second
*/
typedef CKISS<u32, MaxSafeMWC, XORShift32_1, LecuyerLCG32_2> Catid32_1c;
/*
	Period of ~2^^127

	Fails BigCrush tests:

	Catid32_1d: Generator operates at 258 million numbers / second
*/
typedef CKISS<u32, MaximalMWC, XORShift32_2, LecuyerLCG32_2> Catid32_1d;
/*
	Period of ~2^^96

	Fails BigCrush tests:
		2  SerialOver, r = 22               eps
		19  BirthdaySpacings, t = 8       2.0e-130
		21  BirthdaySpacings, t = 16         eps
		81  LinearComp, r = 29             1 - eps1

	Catid32_2: Generator operates at 269 million numbers / second
*/
typedef CKISS<u32, AWC32_1, XORShift32_1, Weyl32_1> Catid32_2;
/*
	Period of ~2^^96

	Fails BigCrush tests:

	Catid32_2a: Generator operates at 269 million numbers / second
*/
typedef CKISS<u32, AWC32_2, XORShift32_1, Weyl32_1> Catid32_2a;
/*
	Period of ~2^^96

	Fails BigCrush tests:

	Catid32_2b: Generator operates at 270 million numbers / second
*/
typedef CKISS<u32, AWC32_1, XORShift32_2, Weyl32_1> Catid32_2b;
/*
	Period of ~2^^96

	Fails BigCrush tests:

	Catid32_2c: Generator operates at 270 million numbers / second
*/
typedef CKISS<u32, AWC32_1, XORShift32_1, Weyl32_2> Catid32_2c;
/*
	Period of ~2^^96

	Fails BigCrush tests:

	Catid32_2d: Generator operates at 269 million numbers / second
*/
typedef CKISS<u32, AWC32_2, XORShift32_2, Weyl32_2> Catid32_2d;


/*
	Catid's Smootch

	Read notes on combining generators for proper usage.

	Mixes just two generators.
*/
template<typename T, class G1, class G2>
class CSmootch
{
	G1 _g1;
	G2 _g2;

public:
	void Initialize(T seed)
	{
		_g1.Initialize(seed);
		_g2.Initialize(seed);
	}

	void MixSeed(T seed)
	{
		_g1.MixSeed(seed);
		_g2.MixSeed(seed);
	}

	T Next()
	{
		return _g1.Next() + _g2.Next();
	}
};

/*
	Period of ~2^^95

	Fails BigCrush tests:
		77  RandomWalk1 R (L=1000, r=20)    3.4e-4

	Catid32S_1: Generator operates at 293 million numbers / second
*/
typedef CSmootch<u32, XORShift32_1, MaxSafeMWC> Catid32S_1;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_1a: Generator operates at 306 million numbers / second
*/
typedef CSmootch<u32, XORShift32_2, MaxSafeMWC> Catid32S_1a;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_1b: Generator operates at 306 million numbers / second
*/
typedef CSmootch<u32, XORShift32_3, MaxSafeMWC> Catid32S_1b;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_1c: Generator operates at 301 million numbers / second
*/
typedef CSmootch<u32, XORShift32_1, MaximalMWC> Catid32S_1c;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_1d: Generator operates at 306 million numbers / second
*/
typedef CSmootch<u32, XORShift32_2, MaximalMWC> Catid32S_1d;
/*
	Period of ~2^^95

	Fails BigCrush tests:
		15  BirthdaySpacings, t = 4          eps

	Catid32S_2: Generator operates at 402 million numbers / second
*/
typedef CSmootch<u32, MaxSafeMWC, LecuyerLCG32_1> Catid32S_2;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_2a: Generator operates at 337 million numbers / second
*/
typedef CSmootch<u32, MaxSafeMWC, LecuyerLCG32_2> Catid32S_2a;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_2b: Generator operates at 398 million numbers / second
*/
typedef CSmootch<u32, MaximalMWC, LecuyerLCG32_1> Catid32S_2b;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_2c: Generator operates at 336 million numbers / second
*/
typedef CSmootch<u32, MaximalMWC, LecuyerLCG32_2> Catid32S_2c;
/*
	Period of ~2^^95

	Fails BigCrush tests:

	Catid32S_2d: Generator operates at 338 million numbers / second
*/
typedef CSmootch<u32, MaxSafeMWC, LecuyerLCG32_3> Catid32S_2d;
/*
	Period of ~2^^64

	Fails BigCrush tests:
		2  SerialOver, r = 22               eps
		19  BirthdaySpacings, t = 8          eps
		21  BirthdaySpacings, t = 16         eps
		69  MatrixRank, L=1000, r=26         eps
		70  MatrixRank, L=5000               eps
		81  LinearComp, r = 29             1 - eps1

	Catid32S_3: Generator operates at 311 million numbers / second
*/
typedef CSmootch<u32, XORShift32_1, LecuyerLCG32_1> Catid32S_3;
/*
	Period of ~2^^126

	Passes all BigCrush tests.

	Catid32S_4: Generator operates at 279 million numbers / second
*/
typedef CSmootch<u32, MaxSafeMWC, DJonesMWC1> Catid32S_4;
/*
	Period of ~2^^126

	Fails BigCrush tests:

	Catid32S_4a: Generator operates at 275 million numbers / second
*/
typedef CSmootch<u32, MaxSafeMWC, MaximalMWC> Catid32S_4a;
/*
	Period of ~2^^126

	Fails BigCrush tests:

	Catid32S_4b: Generator operates at 315 million numbers / second
*/
typedef CSmootch<u32, MaxSafeMWC, DJonesMWC2> Catid32S_4b;
/*
	Period of ~2^^95

	Fails BigCrush tests:
		2  SerialOver, r = 22               eps
		17  BirthdaySpacings, t = 7         7.4e-7
		102  Run of bits, r = 27            1.1e-14

	Catid32S_5: Generator operates at 321 million numbers / second
*/
typedef CSmootch<u32, MaxSafeMWC, AWC32_1> Catid32S_5;


} // namespace cat

#endif // CAT_SMALL_PRNG_HPP
