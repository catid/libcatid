// 06/24/09 split across many source files
// 06/12/09 added SwapLittleEndian
// 06/11/09 added special modulus; part of libcat-1.0
// 04/25/09 fixed divide() bug
// 10/08/08 disable CAT_INLINE assembly for Intel's compiler (it does a better job!)
// 10/03/08 added 32-bit ExpMod
// 09/20/08 for Kevin Jenkins' RakNet

/*
	Several algorithms based on ideas from the "Handbook of Applied Cryptography"
	http://www.cacr.math.uwaterloo.ca/hac/

	Several algorithms based on ideas from the
	"Handbook of Elliptic and Hyperelliptic Curve Cryptography"
	http://www.hyperelliptic.org/HEHCC/

	Montgomery Reduce operation idea from GMP
	http://gmplib.org/
*/

/*
	Notes on Intel Compiler:
		Compiling with the free Intel compiler that integrates with VS.NET
		with full optimization and removing the assembly code I wrote improves
		performance by ** 20% ** at least.  I highly recommend using ICC!

	Notes on Memory:
		BigInts are stored as 32-bit integer arrays.
		Each integer in the array is referred to as a limb ala GMP.
		Lower numbered limbs are less significant to the number represented.
		eg, limb 0 is the least significant limb.
*/

#ifndef CAT_BIGINT_HPP
#define CAT_BIGINT_HPP

#include <cat/Platform.hpp>
#include <cat/rand/IRandom.hpp>
#include <string>

namespace cat {

#if defined(CAT_ARCH_64)
	typedef u64 Leg;
#elif defined(CAT_ARCH_32)
	typedef u32 Leg;
#endif

	class Big
	{
		Leg leg_count;

		Leg legs[1];
	};

	class BigLib
	{
		Leg *library_memory;

	public:
		BigLib();
		~BigLib();
	};

// returns the degree of the base 2 monic polynomial
// (the number of bits used to represent the number)
// eg, 0 0 0 0 1 0 1 1 ... => 28 out of 32 used
u32 Degree32(u32 v);

// returns the number of limbs that are actually used
int LimbDegree(const u32 *n, int limbs);

// return bits used
u32 Degree(const u32 *n, int limbs);

// lhs = rhs (unequal limbs)
void Set(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// lhs = rhs (equal limbs)
void Set(u32 *lhs, int limbs, const u32 *rhs);

// lhs = rhs (32-bit extension)
void Set32(u32 *lhs, int lhs_limbs, const u32 rhs);

#if defined(CAT_ENDIAN_BIG)

	// Flip the byte order only on big endian systems
	void SwapLittleEndian(u32 *to, const u32 *from, int limbs);
	void SwapLittleEndian(u32 *inplace, int limbs);

#else

	CAT_INLINE void SwapLittleEndian(u32 *to, const u32 *from, int limbs)
	{
		Set(to, limbs, from);
	}
	CAT_INLINE void SwapLittleEndian(u32 *inplace, int limbs) {}

#endif // CAT_ENDIAN_BIG

// Comparisons where both operands have the same number of limbs
bool Less(int limbs, const u32 *lhs, const u32 *rhs);
bool Greater(int limbs, const u32 *lhs, const u32 *rhs);
bool Equal(int limbs, const u32 *lhs, const u32 *rhs);

// lhs < rhs
bool Less(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// lhs >= rhs
CAT_INLINE bool GreaterOrEqual(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
{
	return !Less(lhs, lhs_limbs, rhs, rhs_limbs);
}

// lhs > rhs
bool Greater(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// lhs <= rhs
CAT_INLINE bool LessOrEqual(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
{
	return !Greater(lhs, lhs_limbs, rhs, rhs_limbs);
}

// lhs > rhs
bool Greater32(const u32 *lhs, int lhs_limbs, u32 rhs);

// lhs <= rhs
CAT_INLINE bool LessOrEqual32(const u32 *lhs, int lhs_limbs, u32 rhs)
{
	return !Greater32(lhs, lhs_limbs, rhs);
}

// lhs == rhs
bool Equal(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// lhs == rhs
bool Equal32(const u32 *lhs, int lhs_limbs, u32 rhs);

// out = in >>> shift
// Precondition: 0 <= shift < 31
void ShiftRight(int limbs, u32 *out, const u32 *in, int shift);

// {out, carry} = in <<< shift
// Precondition: 0 <= shift < 31
u32 ShiftLeft(int limbs, u32 *out, const u32 *in, int shift);

// lhs += rhs, return carry out
// precondition: lhs_limbs >= rhs_limbs
u32 Add(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// out = lhs + rhs, return carry out
// precondition: lhs_limbs >= rhs_limbs
u32 Add(u32 *out, const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// lhs += rhs, return carry out
// precondition: lhs_limbs > 0
u32 Add32(u32 *lhs, int lhs_limbs, u32 rhs);

// lhs -= rhs, return borrow out
// precondition: lhs_limbs >= rhs_limbs
s32 Subtract(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// out = lhs - rhs, return borrow out
// precondition: lhs_limbs >= rhs_limbs
s32 Subtract(u32 *out, const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs);

// lhs -= rhs, return borrow out
// precondition: lhs_limbs > 0, result limbs = lhs_limbs
s32 Subtract32(u32 *lhs, int lhs_limbs, u32 rhs);

// lhs = -rhs
void Negate(int limbs, u32 *lhs, const u32 *rhs);

// n = ~n, only invert bits up to the MSB, but none above that
void BitNot(u32 *n, int limbs);

// n = ~n, invert all bits, even ones above MSB
void LimbNot(u32 *n, int limbs);

// lhs ^= rhs
void Xor(int limbs, u32 *lhs, const u32 *rhs);

// Return the carry out from A += B << S
u32 AddLeftShift32(
	int limbs,		// Number of limbs in parameter A and B
	u32 *A,			// Large number
	const u32 *B,	// Large number
	u32 S);			// 32-bit number

// Return the carry out from result = A * B
u32 Multiply32(
	int limbs,		// Number of limbs in parameter A, result
	u32 *result,	// Large number
	const u32 *A,	// Large number
	u32 B);			// 32-bit number

// Return the carry out from X = X * M + A
u32 Multiply32Add32(
	int limbs,	// Number of limbs in parameter A and B
	u32 *X,		// Large number
	u32 M,		// 32-bit number
	u32 A);		// 32-bit number

// Return the carry out from A += B * M
u32 AddMultiply32(
	int limbs,		// Number of limbs in parameter A and B
	u32 *A,			// Large number
	const u32 *B,	// Large number
	u32 M);			// 32-bit number

// product = x * y
void SimpleMultiply(
	int limbs,		// Number of limbs in parameters x, y
	u32 *product,	// Large number; buffer size = limbs*2
	const u32 *x,	// Large number
	const u32 *y);	// Large number

// product = x ^ 2
void SimpleSquare(
	int limbs,		// Number of limbs in parameter x
	u32 *product,	// Large number; buffer size = limbs*2
	const u32 *x);	// Large number

// product = xy
// memory space for product may not overlap with x,y
void Multiply(
	int limbs,		// Number of limbs in x,y
	u32 *product,	// Product; buffer size = limbs*2
	const u32 *x,	// Large number; buffer size = limbs
	const u32 *y);	// Large number; buffer size = limbs

// product = low half of x * y product
void SimpleMultiplyLowHalf(
	int limbs,		// Number of limbs in parameters x, y
	u32 *product,	// Large number; buffer size = limbs
	const u32 *x,	// Large number
	const u32 *y);	// Large number

// product = x^2
// memory space for product may not overlap with x
void Square(
	int limbs,		// Number of limbs in x
	u32 *product,	// Product; buffer size = limbs*2
	const u32 *x);	// Large number; buffer size = limbs

// Returns the remainder of N / divisor for a 32-bit divisor
u32 Modulus32(
	int limbs,     // Number of limbs in parameter N
	const u32 *N,  // Large number, buffer size = limbs
	u32 divisor);  // 32-bit number

/*
 * 'A' is overwritten with the quotient of the operation
 * Returns the remainder of 'A' / divisor for a 32-bit divisor
 * 
 * Does not check for divide-by-zero
 */
u32 Divide32(
	int limbs,		// Number of limbs in parameter A
	u32 *A,			// Large number, buffer size = limbs
	u32 divisor);	// 32-bit number

// returns (n ^ -1) Mod 2^32
u32 MulInverse32(u32 n);

/*
 * Computes multiplicative inverse of given number
 * Such that: result * u = 1
 * Using Extended Euclid's Algorithm (GCDe)
 * 
 * This is not always possible, so it will return false iff not possible.
 */
bool MulInverse(
	int limbs,		// Limbs in u and result
	const u32 *u,	// Large number, buffer size = limbs
	u32 *result);	// Large number, buffer size = limbs

// {q, r} = u / v
// Return false on divide by zero
bool Divide(
	const u32 *u,	// numerator, size = u_limbs
	int u_limbs,
	const u32 *v,	// denominator, size = v_limbs
	int v_limbs,
	u32 *q,			// quotient, size = u_limbs
	u32 *r);		// remainder, size = v_limbs

// r = u % v
// Return false on divide by zero
bool Modulus(
	const u32 *u,	// numerator, size = u_limbs
	int u_limbs,
	const u32 *v,	// denominator, size = v_limbs
	int v_limbs,
	u32 *r);		// remainder, size = v_limbs

// m_inv ~= 2^(2k)/m
// Generates m_inv parameter of BarrettModulus()
// It is limbs in size, chopping off the 2^k bit
// Only works for m with the high bit set
void BarrettModulusPrecomp(
	int limbs,		// Number of limbs in m and m_inv
	const u32 *m,	// Modulus, size = limbs
	u32 *m_inv);	// Large number result, size = limbs

// r = x mod m
// Using Barrett's method with precomputed m_inv
void BarrettModulus(
	int limbs,			// Number of limbs in m and m_inv
	const u32 *x,		// Number to reduce, size = limbs*2
	const u32 *m,		// Modulus, size = limbs
	const u32 *m_inv,	// R/Modulus, precomputed, size = limbs
	u32 *result);		// Large number result

// r = x % m
// For m in the special form: 2^(N=wordbits*m_limbs) - c
// Assumes c < 2^28, to allow for numerators 4x larger than m
void SpecialModulus(
	const u32 *x,	// numerator, size = x_limbs
	int x_limbs,	// always > m_limbs
	u32 c,			// small subtracted constant in m
	int m_limbs,	// number of limbs in modulus
	u32 *r);		// remainder, size = m_limbs

// Convert bigint to string
std::string ToStr(const u32 *n, int limbs, int base = 10);

// Convert string to bigint
// Return 0 if string contains non-digit characters, else number of limbs used
int ToInt(u32 *lhs, int max_limbs, const char *rhs, u32 base = 10);

/*
 * Computes: result = (1/u) (Mod v)
 * Such that: result * u (Mod v) = 1
 * Using Extended Euclid's Algorithm (GCDe)
 * 
 * This is not always possible, so it will return false iff not possible.
 */
bool InvMod(
	const u32 *u,	// Large number, buffer size = u_limbs
	int u_limbs,	// Limbs in u
	const u32 *v,	// Large number, buffer size = limbs
	int limbs,		// Limbs in modulus and result
	u32 *result);	// Large number, buffer size = limbs

/*
 * Computes: result = GCD(a, b)  (greatest common divisor)
 * 
 * Length of result is the length of the smallest argument
 */
void GCD(
	const u32 *a,	// Large number, buffer size = a_limbs
	int a_limbs,	// Size of a
	const u32 *b,	// Large number, buffer size = b_limbs
	int b_limbs,	// Size of b
	u32 *result);	// Large number, buffer size = min(a, b)

// root = sqrt(square)
// Based on Newton-Raphson iteration: root_n+1 = (root_n + square/root_n) / 2
// Doubles number of correct bits each iteration
// Precondition: The high limb of square is non-zero
// Returns false if it was unable to determine the root
bool SquareRoot(
	int limbs,			// Number of limbs in root
	const u32 *square,	// Square to root, size = limbs * 2
	u32 *root);			// Output root, size = limbs

// Take the square root in a finite field modulus a prime m.
// Special conditions: m = 2^N - c = 3 (mod 4) -> c = 1 (mod 4)
void SpecialSquareRoot(
	int limbs,
	const u32 *x,
	u32 c,
	u32 *r);

// Calculates mod_inv from low limb of modulus for Mon*()
u32 MonReducePrecomp(u32 modulus0);

// Compute n_residue for Montgomery reduction
void MonInputResidue(
	const u32 *n,		// Large number, buffer size = n_limbs
	int n_limbs,		// Number of limbs in n
	const u32 *modulus,	// Large number, buffer size = m_limbs
	int m_limbs,		// Number of limbs in modulus
	u32 *n_residue);	// Result, buffer size = m_limbs

// result = a * b * r^-1 (Mod modulus) in Montgomery domain
void MonPro(
	int limbs,				// Number of limbs in each parameter
	const u32 *a_residue,	// Large number, buffer size = limbs
	const u32 *b_residue,	// Large number, buffer size = limbs
	const u32 *modulus,		// Large number, buffer size = limbs
	u32 mod_inv,			// MonReducePrecomp() return
	u32 *result);			// Large number, buffer size = limbs

// result = a^-1 (Mod modulus) in Montgomery domain
void MonInverse(
	int limbs,				// Number of limbs in each parameter
	const u32 *a_residue,	// Large number, buffer size = limbs
	const u32 *modulus,		// Large number, buffer size = limbs
	u32 mod_inv,			// MonReducePrecomp() return
	u32 *result);			// Large number, buffer size = limbs

// result = a * r^-1 (Mod modulus) in Montgomery domain
// The result may be greater than the modulus, but this is okay since
// the result is still in the RNS.  MonFinish() corrects this at the end.
void MonReduce(
	int limbs,			// Number of limbs in each parameter
	u32 *s,				// Large number, buffer size = limbs*2, gets clobbered
	const u32 *modulus,	// Large number, buffer size = limbs
	u32 mod_inv,		// MonReducePrecomp() return
	u32 *result);		// Large number, buffer size = limbs

// result = a * r^-1 (Mod modulus) in Montgomery domain
void MonFinish(
	int limbs,			// Number of limbs in each parameter
	u32 *n,				// Large number, buffer size = limbs
	const u32 *modulus,	// Large number, buffer size = limbs
	u32 mod_inv);		// MonReducePrecomp() return

// Precompute a window for ExpMod() and MonExpMod()
// Requires 2^window_bits multiplies
u32 *ExpPrecomputeWindow(
	const u32 *base,
	const u32 *modulus,
	int limbs,
	u32 mod_inv,
	int window_bits);

// Simple internal version without windowing for small exponents
void SimpleMonExpMod(
	const u32 *base,	//	Base for exponentiation, buffer size = mod_limbs
	const u32 *exponent,//	Exponent, buffer size = exponent_limbs
	int exponent_limbs,	//	Number of limbs in exponent
	const u32 *modulus,	//	Modulus, buffer size = mod_limbs
	int mod_limbs,		//	Number of limbs in modulus
	u32 mod_inv,		//	MonReducePrecomp() return
	u32 *result);		//	Result, buffer size = mod_limbs

// Computes: result = base ^ exponent (Mod modulus)
// Using Montgomery multiplication with simple squaring method
// Base parameter must be a Montgomery Residue created with MonInputResidue()
void MonExpMod(
	const u32 *base,	// Base for exponentiation, buffer size = mod_limbs
	const u32 *exponent,// Exponent, buffer size = exponent_limbs
	int exponent_limbs,	// Number of limbs in exponent
	const u32 *modulus,	// Modulus, buffer size = mod_limbs
	int mod_limbs,		// Number of limbs in modulus
	u32 mod_inv,		// MonReducePrecomp() return
	u32 *result);		// Result, buffer size = mod_limbs

// Computes: result = base ^ exponent (Mod modulus)
// Using Montgomery multiplication with simple squaring method
void ExpMod(
	const u32 *base,	// Base for exponentiation, buffer size = base_limbs
	int base_limbs,		// Number of limbs in base
	const u32 *exponent,// Exponent, buffer size = exponent_limbs
	int exponent_limbs,	// Number of limbs in exponent
	const u32 *modulus,	// Modulus, buffer size = mod_limbs
	int mod_limbs,		// Number of limbs in modulus
	u32 mod_inv,		// MonReducePrecomp() return
	u32 *result);		// Result, buffer size = mod_limbs

// returns b ^ e (Mod m)
u32 ExpMod32(u32 b, u32 e, u32 m);

// Rabin-Miller method for finding a strong pseudo-prime
// Preconditions: High bit and low bit of n = 1
bool RabinMillerPrimeTest(
	IRandom *prng,
	const u32 *n,	// Number to check for primality
	int limbs,		// Number of limbs in n
	u32 k);			// Confidence level (40 is pretty good)

// Generate a strong pseudo-prime using the Rabin-Miller primality test
void GenerateStrongPseudoPrime(
	IRandom *prng,
	u32 *n,			// Output prime
	int limbs);		// Number of limbs in n


} // namespace cat

#endif // CAT_BIGINT_HPP
