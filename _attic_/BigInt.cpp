#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
	// returns the degree of the base 2 monic polynomial
	// (the number of bits used to represent the number)
	// eg, 0 0 0 0 1 0 1 1 ... => 28 out of 32 used
	u32 Degree32(u32 v)
	{
		return v ? (BitMath::BSR32(v) + 1) : 0;
	}

	// returns the number of limbs that are actually used
	int LimbDegree(const u32 *n, int limbs)
	{
		while (limbs--)
			if (n[limbs])
				return limbs + 1;

		return 0;
	}

	// return bits used
	u32 Degree(const u32 *n, int limbs)
	{
		u32 limb_degree = LimbDegree(n, limbs);
		if (!limb_degree) return 0;
		--limb_degree;

		u32 msl_degree = Degree32(n[limb_degree]);

		return msl_degree + limb_degree*32;
	}

	void Set(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int min = lhs_limbs < rhs_limbs ? lhs_limbs : rhs_limbs;

		memcpy(lhs, rhs, min*4);
		memset(&lhs[min], 0, (lhs_limbs - min)*4);
	}
	void Set(u32 *lhs, int limbs, const u32 *rhs)
	{
		memcpy(lhs, rhs, limbs*4);
	}
	void Set32(u32 *lhs, int lhs_limbs, const u32 rhs)
	{
		*lhs = rhs;
		memset(&lhs[1], 0, (lhs_limbs - 1)*4);
	}

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

	bool Less(int limbs, const u32 *lhs, const u32 *rhs)
	{
		for (int ii = limbs-1; ii >= 0; --ii)
			if (lhs[ii] != rhs[ii])
				return lhs[ii] < rhs[ii];

		return false;
	}
	bool Greater(int limbs, const u32 *lhs, const u32 *rhs)
	{
		for (int ii = limbs-1; ii >= 0; --ii)
			if (lhs[ii] != rhs[ii])
				return lhs[ii] > rhs[ii];

		return false;
	}
	bool Equal(int limbs, const u32 *lhs, const u32 *rhs)
	{
		return 0 == memcmp(lhs, rhs, limbs*4);
	}

	bool Less(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		if (lhs_limbs > rhs_limbs)
			do if (lhs[--lhs_limbs] != 0) return false; while (lhs_limbs > rhs_limbs);
		else if (lhs_limbs < rhs_limbs)
			do if (rhs[--rhs_limbs] != 0) return true; while (lhs_limbs < rhs_limbs);

		while (lhs_limbs--) if (lhs[lhs_limbs] != rhs[lhs_limbs]) return lhs[lhs_limbs] < rhs[lhs_limbs];
		return false; // equal
	}
	bool Greater(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		if (lhs_limbs > rhs_limbs)
			do if (lhs[--lhs_limbs] != 0) return true; while (lhs_limbs > rhs_limbs);
		else if (lhs_limbs < rhs_limbs)
			do if (rhs[--rhs_limbs] != 0) return false; while (lhs_limbs < rhs_limbs);

		while (lhs_limbs--) if (lhs[lhs_limbs] != rhs[lhs_limbs]) return lhs[lhs_limbs] > rhs[lhs_limbs];
		return false; // equal
	}
	bool Equal(const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		if (lhs_limbs > rhs_limbs)
			do if (lhs[--lhs_limbs] != 0) return false; while (lhs_limbs > rhs_limbs);
		else if (lhs_limbs < rhs_limbs)
			do if (rhs[--rhs_limbs] != 0) return false; while (lhs_limbs < rhs_limbs);

		while (lhs_limbs--) if (lhs[lhs_limbs] != rhs[lhs_limbs]) return false;
		return true; // equal
	}

	bool Greater32(const u32 *lhs, int lhs_limbs, u32 rhs)
	{
		if (*lhs > rhs) return true;
		while (--lhs_limbs)
			if (*++lhs) return true;
		return false;
	}
	bool Equal32(const u32 *lhs, int lhs_limbs, u32 rhs)
	{
		if (*lhs != rhs) return false;
		while (--lhs_limbs)
			if (*++lhs) return false;
		return true; // equal
	}

	// out = in >>> shift
	// Precondition: 0 <= shift < 31
	void ShiftRight(int limbs, u32 *out, const u32 *in, int shift)
	{
		if (!shift)
		{
			Set(out, limbs, in);
			return;
		}

		u32 carry = 0;

		for (int ii = limbs - 1; ii >= 0; --ii)
		{
			u32 r = in[ii];

			out[ii] = (r >> shift) | carry;

			carry = r << (32 - shift);
		}
	}

	// {out, carry} = in <<< shift
	// Precondition: 0 <= shift < 31
	u32 ShiftLeft(int limbs, u32 *out, const u32 *in, int shift)
	{
		if (!shift)
		{
			Set(out, limbs, in);
			return 0;
		}

		u32 carry = 0;

		for (int ii = 0; ii < limbs; ++ii)
		{
			u32 r = in[ii];

			out[ii] = (r << shift) | carry;

			carry = r >> (32 - shift);
		}

		return carry;
	}

	// lhs += rhs, return carry out
	// precondition: lhs_limbs >= rhs_limbs
	u32 Add(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		u64 r = (u64)lhs[0] + rhs[0];
		lhs[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((u64)lhs[ii] + rhs[ii]) + (u32)(r >> 32);
			lhs[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (u32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			lhs[ii] = (u32)r;
		}

		return (u32)(r >> 32);
	}

	// out = lhs + rhs, return carry out
	// precondition: lhs_limbs >= rhs_limbs
	u32 Add(u32 *out, const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		u64 r = (u64)lhs[0] + rhs[0];
		out[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((u64)lhs[ii] + rhs[ii]) + (u32)(r >> 32);
			out[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (u32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			out[ii] = (u32)r;
		}

		return (u32)(r >> 32);
	}

	// lhs += rhs, return carry out
	// precondition: lhs_limbs > 0
	u32 Add32(u32 *lhs, int lhs_limbs, u32 rhs)
	{
		if ((lhs[0] = lhs[0] + rhs) >= rhs)
			return 0;

		for (int ii = 1; ii < lhs_limbs; ++ii)
			if (++lhs[ii])
				return 0;

		return 1;
	}

	// lhs -= rhs, return borrow out
	// precondition: lhs_limbs >= rhs_limbs
	s32 Subtract(u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		s64 r = (s64)lhs[0] - rhs[0];
		lhs[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((s64)lhs[ii] - rhs[ii]) + (s32)(r >> 32);
			lhs[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (s32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			lhs[ii] = (u32)r;
		}

		return (s32)(r >> 32);
	}

	// out = lhs - rhs, return borrow out
	// precondition: lhs_limbs >= rhs_limbs
	s32 Subtract(u32 *out, const u32 *lhs, int lhs_limbs, const u32 *rhs, int rhs_limbs)
	{
		int ii;
		s64 r = (s64)lhs[0] - rhs[0];
		out[0] = (u32)r;

		for (ii = 1; ii < rhs_limbs; ++ii)
		{
			r = ((s64)lhs[ii] - rhs[ii]) + (s32)(r >> 32);
			out[ii] = (u32)r;
		}

		for (; ii < lhs_limbs && (s32)(r >>= 32) != 0; ++ii)
		{
			r += lhs[ii];
			out[ii] = (u32)r;
		}

		return (s32)(r >> 32);
	}

	// lhs -= rhs, return borrow out
	// precondition: lhs_limbs > 0, result limbs = lhs_limbs
	s32 Subtract32(u32 *lhs, int lhs_limbs, u32 rhs)
	{
		u32 n = lhs[0];
		u32 r = n - rhs;
		lhs[0] = r;

		if (r <= n)
			return 0;

		for (int ii = 1; ii < lhs_limbs; ++ii)
			if (lhs[ii]--)
				return 0;

		return -1;
	}

	// lhs = -rhs
	void Negate(int limbs, u32 *lhs, const u32 *rhs)
	{
		// Propagate negations until carries stop
		while (limbs-- > 0 && !(*lhs++ = -(s32)(*rhs++)));

		// Then just invert the remaining words
		while (limbs-- > 0) *lhs++ = ~(*rhs++);
	}

	// n = ~n, only invert bits up to the MSB, but none above that
	void BitNot(u32 *n, int limbs)
	{
		limbs = LimbDegree(n, limbs);
		if (limbs)
		{
			u32 high = n[--limbs];
			u32 high_degree = 32 - Degree32(high);

			n[limbs] = ((u32)(~high << high_degree) >> high_degree);
			while (limbs--) n[limbs] = ~n[limbs];
		}
	}

	// n = ~n, invert all bits, even ones above MSB
	void LimbNot(u32 *n, int limbs)
	{
		while (limbs--) *n++ = ~(*n);
	}

	// lhs ^= rhs
	void Xor(int limbs, u32 *lhs, const u32 *rhs)
	{
		while (limbs--) *lhs++ ^= *rhs++;
	}

	// Return the carry out from A += B << S
    u32 AddLeftShift32(
    	int limbs,		// Number of limbs in parameter A and B
    	u32 *A,			// Large number
    	const u32 *B,	// Large number
    	u32 S)			// 32-bit number
	{
		u64 sum = 0;
		u32 last = 0;

		while (limbs--)
		{
			u32 b = *B++;

			sum = (u64)((b << S) | (last >> (32 - S))) + *A + (u32)(sum >> 32);

			last = b;
			*A++ = (u32)sum;
		}

		return (u32)(sum >> 32) + (last >> (32 - S));
	}

	// Return the carry out from result = A * B
    u32 Multiply32(
    	int limbs,		// Number of limbs in parameter A, result
    	u32 *result,	// Large number
    	const u32 *A,	// Large number
    	u32 B)			// 32-bit number
	{
		u64 p = (u64)A[0] * B;
		result[0] = (u32)p;

		while (--limbs)
		{
			p = (u64)*(++A) * B + (u32)(p >> 32);
			*(++result) = (u32)p;
		}

		return (u32)(p >> 32);
	}

	// Return the carry out from X = X * M + A
    u32 Multiply32Add32(
    	int limbs,	// Number of limbs in parameter A and B
    	u32 *X,		// Large number
    	u32 M,		// 32-bit number
    	u32 A)		// 32-bit number
	{
		u64 p = (u64)X[0] * M + A;
		X[0] = (u32)p;

		while (--limbs)
		{
			p = (u64)*(++X) * M + (u32)(p >> 32);
			*X = (u32)p;
		}

		return (u32)(p >> 32);
	}

	// Return the carry out from A += B * M
    u32 AddMultiply32(
    	int limbs,		// Number of limbs in parameter A and B
    	u32 *A,			// Large number
    	const u32 *B,	// Large number
    	u32 M)			// 32-bit number
	{
		// This function is roughly 85% of the cost of exponentiation

		// ICC does a better job than my hand-written version by using SIMD instructions,
		// so I use its optimizer instead.
#if !defined(CAT_COMPILER_ICC) && defined(CAT_ASSEMBLY_INTEL_SYNTAX)
		CAT_ASSEMBLY_BLOCK // VS.NET, x86, 32-bit words
		{
			mov esi, [B]
			mov edi, [A]
			mov eax, [esi]
			mul [M]					; (edx,eax) = [M]*[esi]
			add eax, [edi]			; (edx,eax) += [edi]
			adc edx, 0
			; (edx,eax) = [B]*[M] + [A]

			mov [edi], eax
			; [A] = eax

			mov ecx, [limbs]
			sub ecx, 1
			jz loop_done
loop_head:
				lea esi, [esi + 4]	; ++B
				mov eax, [esi]		; eax = [B]
				mov ebx, edx		; ebx = last carry
				lea edi, [edi + 4]	; ++A
				mul [M]				; (edx,eax) = [M]*[esi]
				add eax, [edi]		; (edx,eax) += [edi]
				adc edx, 0
				add eax, ebx		; (edx,eax) += ebx
				adc edx, 0
				; (edx,eax) = [esi]*[M] + [edi] + (ebx=last carry)

				mov [edi], eax
				; [A] = eax

			sub ecx, 1
			jnz loop_head
loop_done:
			mov [M], edx	; Use [M] to copy the carry into C++ land
		}

		return M;
#else
		// Unrolled first loop
		u64 p = B[0] * (u64)M + A[0];
		A[0] = (u32)p;

		while (--limbs)
		{
			p = (*(++B) * (u64)M + *(++A)) + (u32)(p >> 32);
			A[0] = (u32)p;
		}

		return (u32)(p >> 32);
#endif
	}

	// product = x * y
	void SimpleMultiply(
		int limbs,		// Number of limbs in parameters x, y
		u32 *product,	// Large number; buffer size = limbs*2
		const u32 *x,	// Large number
		const u32 *y)	// Large number
	{
		// Roughly 25% of the cost of exponentiation
		product[limbs] = Multiply32(limbs, product, x, *y);
		u32 ctr = limbs;
		while (--ctr) product[limbs] = AddMultiply32(limbs, ++product, x, *++y);
	}

	// product = low half of x * y product
	void SimpleMultiplyLowHalf(
		int limbs,		// Number of limbs in parameters x, y
		u32 *product,	// Large number; buffer size = limbs
		const u32 *x,	// Large number
		const u32 *y)	// Large number
	{
		Multiply32(limbs, product, x, *y);
		while (--limbs) AddMultiply32(limbs, ++product, x, *++y);
	}

	// product = x ^ 2
	void SimpleSquare(
		int limbs,		// Number of limbs in parameter x
		u32 *product,	// Large number; buffer size = limbs*2
		const u32 *x)	// Large number
	{
		// Seems about 15% faster than SimpleMultiply() in practice
		u32 *cross_product = (u32*)alloca(limbs*2*4);

		// Calculate square-less and repeat-less cross products
		cross_product[limbs] = Multiply32(limbs - 1, cross_product + 1, x + 1, x[0]);
		for (int ii = 1; ii < limbs - 1; ++ii)
		{
			cross_product[limbs + ii] = AddMultiply32(limbs - ii - 1,
													  cross_product + ii*2 + 1,
													  x + ii + 1,
													  x[ii]);
		}

		// Calculate square products
		for (int ii = 0; ii < limbs; ++ii)
		{
			u32 xi = x[ii];
			u64 si = (u64)xi * xi;
			product[ii*2] = (u32)si;
			product[ii*2+1] = (u32)(si >> 32);
		}

		// Multiply the cross product by 2 and add it to the square products
		product[limbs*2 - 1] += AddLeftShift32(limbs*2 - 2, product + 1, cross_product + 1, 1);
	}

	// product = xy
	// memory space for product may not overlap with x,y
    void Multiply(
    	int limbs,		// Number of limbs in x,y
    	u32 *product,	// Product; buffer size = limbs*2
    	const u32 *x,	// Large number; buffer size = limbs
    	const u32 *y)	// Large number; buffer size = limbs
	{
		// Stop recursing under 640 bits or odd limb count
		if (limbs < 30 || (limbs & 1))
		{
			SimpleMultiply(limbs, product, x, y);
			return;
		}

		// Compute high and low products
		Multiply(limbs/2, product, x, y);
		Multiply(limbs/2, product + limbs, x + limbs/2, y + limbs/2);

		// Compute (x1 + x2), xc = carry out
		u32 *xsum = (u32*)alloca((limbs/2)*4);
		u32 xcarry = Add(xsum, x, limbs/2, x + limbs/2, limbs/2);

		// Compute (y1 + y2), yc = carry out
		u32 *ysum = (u32*)alloca((limbs/2)*4);
		u32 ycarry = Add(ysum, y, limbs/2, y + limbs/2, limbs/2);

		// Compute (x1 + x2) * (y1 + y2)
		u32 *cross_product = (u32*)alloca(limbs*4);
		Multiply(limbs/2, cross_product, xsum, ysum);

		// Subtract out the high and low products
		s32 cross_carry = Subtract(cross_product, limbs, product, limbs);
		cross_carry += Subtract(cross_product, limbs, product + limbs, limbs);

		// Fix the extra high carry bits of the result
		if (ycarry) cross_carry += Add(cross_product + limbs/2, limbs/2, xsum, limbs/2);
		if (xcarry) cross_carry += Add(cross_product + limbs/2, limbs/2, ysum, limbs/2);
		cross_carry += (xcarry & ycarry);

		// Add the cross product into the result
		cross_carry += Add(product + limbs/2, limbs*3/2, cross_product, limbs);

		// Add in the fixed high carry bits
		if (cross_carry) Add32(product + limbs*3/2, limbs/2, cross_carry);
	}

	// product = x^2
	// memory space for product may not overlap with x
    void Square(
    	int limbs,		// Number of limbs in x
    	u32 *product,	// Product; buffer size = limbs*2
    	const u32 *x)	// Large number; buffer size = limbs
	{
		// Stop recursing under 1280 bits or odd limb count
		if (limbs < 40 || (limbs & 1))
		{
			SimpleSquare(limbs, product, x);
			return;
		}

		// Compute high and low squares
		Square(limbs/2, product, x);
		Square(limbs/2, product + limbs, x + limbs/2);

		// Generate the cross product
		u32 *cross_product = (u32*)alloca(limbs*4);
		Multiply(limbs/2, cross_product, x, x + limbs/2);

		// Multiply the cross product by 2 and add it to the result
		u32 cross_carry = AddLeftShift32(limbs, product + limbs/2, cross_product, 1);

		// Roll the carry out up to the highest limb
		if (cross_carry) Add32(product + limbs*3/2, limbs/2, cross_carry);
	}

	// Returns the remainder of N / divisor for a 32-bit divisor
    u32 Modulus32(
    	int limbs,		// Number of limbs in parameter N
    	const u32 *N,	// Large number, buffer size = limbs
    	u32 divisor)	// 32-bit number
	{
		u32 remainder = N[limbs-1] < divisor ? N[limbs-1] : 0;
		u32 counter = N[limbs-1] < divisor ? limbs-1 : limbs;

		while (counter--) remainder = (u32)((((u64)remainder << 32) | N[counter]) % divisor);

		return remainder;
	}

	/*
	 * 'A' is overwritten with the quotient of the operation
	 * Returns the remainder of 'A' / divisor for a 32-bit divisor
	 *
	 * Does not check for divide-by-zero
	 */
    u32 Divide32(
    	int limbs,		// Number of limbs in parameter A
    	u32 *A,			// Large number, buffer size = limbs
    	u32 divisor)	// 32-bit number
	{
		u64 r = 0;
		for (int ii = limbs-1; ii >= 0; --ii)
		{
			u64 n = (r << 32) | A[ii];
			A[ii] = (u32)(n / divisor);
			r = n % divisor;
		}

		return (u32)r;
	}

	// returns (n ^ -1) Mod 2^32
	u32 MulInverse32(u32 n)
	{
		// {u1, g1} = 2^32 / n
		u32 hb = (~(n - 1) >> 31);
		u32 u1 = -(s32)(0xFFFFFFFF / n + hb);
		u32 g1 = ((-(s32)hb) & (0xFFFFFFFF % n + 1)) - n;

		if (!g1) {
			if (n != 1) return 0;
			else return 1;
		}

		u32 q, u = 1, g = n;

		for (;;) {
			q = g / g1;
			g %= g1;

			if (!g) {
				if (g1 != 1) return 0;
				else return u1;
			}

			u -= q*u1;
			q = g1 / g;
			g1 %= g;

			if (!g1) {
				if (g != 1) return 0;
				else return u;
			}

			u1 -= q*u;
		}
	}

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
		u32 *result)	// Large number, buffer size = limbs
	{
		u32 *u1 = (u32*)alloca(limbs*4);
		u32 *u3 = (u32*)alloca(limbs*4);
		u32 *v1 = (u32*)alloca(limbs*4);
		u32 *v3 = (u32*)alloca(limbs*4);
		u32 *t1 = (u32*)alloca(limbs*4);
		u32 *t3 = (u32*)alloca(limbs*4);
		u32 *q = (u32*)alloca((limbs+1)*4);
		u32 *w = (u32*)alloca((limbs+1)*4);

		// Unrolled first iteration
		{
			Set32(u1, limbs, 0);
			Set32(v1, limbs, 1);
			Set(v3, limbs, u);
		}

		// Unrolled second iteration
		if (!LimbDegree(v3, limbs))
			return false;

		// {q, t3} <- R / v3
		Set32(w, limbs, 0);
		w[limbs] = 1;
		Divide(w, limbs+1, v3, limbs, q, t3);

		SimpleMultiplyLowHalf(limbs, t1, q, v1);
		Add(t1, limbs, u1, limbs);

		for (;;)
		{
			if (!LimbDegree(t3, limbs))
			{
				Set(result, limbs, v1);
				return Equal32(v3, limbs, 1);
			}

			Divide(v3, limbs, t3, limbs, q, u3);
			SimpleMultiplyLowHalf(limbs, u1, q, t1);
			Add(u1, limbs, v1, limbs);

			if (!LimbDegree(u3, limbs))
			{
				Negate(limbs, result, t1);
				return Equal32(t3, limbs, 1);
			}

			Divide(t3, limbs, u3, limbs, q, v3);
			SimpleMultiplyLowHalf(limbs, v1, q, u1);
			Add(v1, limbs, t1, limbs);

			if (!LimbDegree(v3, limbs))
			{
				Set(result, limbs, u1);
				return Equal32(u3, limbs, 1);
			}

			Divide(u3, limbs, v3, limbs, q, t3);
			SimpleMultiplyLowHalf(limbs, t1, q, v1);
			Add(t1, limbs, u1, limbs);

			if (!LimbDegree(t3, limbs))
			{
				Negate(limbs, result, v1);
				return Equal32(v3, limbs, 1);
			}

			Divide(v3, limbs, t3, limbs, q, u3);
			SimpleMultiplyLowHalf(limbs, u1, q, t1);
			Add(u1, limbs, v1, limbs);

			if (!LimbDegree(u3, limbs))
			{
				Set(result, limbs, t1);
				return Equal32(t3, limbs, 1);
			}

			Divide(t3, limbs, u3, limbs, q, v3);
			SimpleMultiplyLowHalf(limbs, v1, q, u1);
			Add(v1, limbs, t1, limbs);

			if (!LimbDegree(v3, limbs))
			{
				Negate(limbs, result, u1);
				return Equal32(u3, limbs, 1);
			}

			Divide(u3, limbs, v3, limbs, q, t3);
			SimpleMultiplyLowHalf(limbs, t1, q, v1);
			Add(t1, limbs, u1, limbs);
		}
	}

	// {q, r} = u / v
	// q is not u or v
	// Return false on divide by zero
	bool Divide(
		const u32 *u,	// numerator, size = u_limbs
		int u_limbs,
		const u32 *v,	// denominator, size = v_limbs
		int v_limbs,
		u32 *q,			// quotient, size = u_limbs
		u32 *r)			// remainder, size = v_limbs
	{
		// calculate v_used and u_used
		int v_used = LimbDegree(v, v_limbs);
		if (!v_used) return false;

		int u_used = LimbDegree(u, u_limbs);

		// if u < v, avoid division
		if (u_used <= v_used && Less(u, u_used, v, v_used))
		{
			// r = u, q = 0
			Set(r, v_limbs, u, u_used);
			Set32(q, u_limbs, 0);
			return true;
		}

		// if v is 32 bits, use faster Divide32 code
		if (v_used == 1)
		{
			// {q, r} = u / v[0]
			Set(q, u_limbs, u);
			Set32(r, v_limbs, Divide32(u_limbs, q, v[0]));
			return true;
		}

		// calculate high zero bits in v's high used limb
		int shift = 32 - Degree32(v[v_used - 1]);
		int uu_used = u_used;
		if (shift > 0) uu_used++;

		u32 *uu = (u32*)alloca(uu_used*4);
		u32 *vv = (u32*)alloca(v_used*4);

		// shift left to fill high MSB of divisor
		if (shift > 0)
		{
			ShiftLeft(v_used, vv, v, shift);
			uu[u_used] = ShiftLeft(u_used, uu, u, shift);
		}
		else
		{
			Set(uu, u_used, u);
			Set(vv, v_used, v);
		}

		int q_high_index = uu_used - v_used;

		if (GreaterOrEqual(uu + q_high_index, v_used, vv, v_used))
		{
			Subtract(uu + q_high_index, v_used, vv, v_used);
			Set32(q + q_high_index, u_used - q_high_index, 1);
		}
		else
		{
			Set32(q + q_high_index, u_used - q_high_index, 0);
		}

		u32 *vq_product = (u32*)alloca((v_used+1)*4);

		// for each limb,
		for (int ii = q_high_index - 1; ii >= 0; --ii)
		{
			u64 q_full = *(u64*)(uu + ii + v_used - 1) / vv[v_used - 1];
			u32 q_low = (u32)q_full;
			u32 q_high = (u32)(q_full >> 32);

			vq_product[v_used] = Multiply32(v_used, vq_product, vv, q_low);

			if (q_high) // it must be '1'
				Add(vq_product + 1, v_used, vv, v_used);

			if (Subtract(uu + ii, v_used + 1, vq_product, v_used + 1))
			{
				--q_low;
				if (Add(uu + ii, v_used + 1, vv, v_used) == 0)
				{
					--q_low;
					Add(uu + ii, v_used + 1, vv, v_used);
				}
			}

			q[ii] = q_low;
		}

		memset(r + v_used, 0, (v_limbs - v_used)*4);
		ShiftRight(v_used, r, uu, shift);

		return true;
	}

	// r = u % v
	// Return false on divide by zero
	bool Modulus(
		const u32 *u,	// numerator, size = u_limbs
		int u_limbs,
		const u32 *v,	// denominator, size = v_limbs
		int v_limbs,
		u32 *r)			// remainder, size = v_limbs
	{
		// calculate v_used and u_used
		int v_used = LimbDegree(v, v_limbs);
		if (!v_used) return false;

		int u_used = LimbDegree(u, u_limbs);

		// if u < v, avoid division
		if (u_used <= v_used && Less(u, u_used, v, v_used))
		{
			// r = u, q = 0
			Set(r, v_limbs, u, u_used);
			//Set32(q, u_limbs, 0);
			return true;
		}

		// if v is 32 bits, use faster Divide32 code
		if (v_used == 1)
		{
			// {q, r} = u / v[0]
			//Set(q, u_limbs, u);
			Set32(r, v_limbs, Modulus32(u_limbs, u, v[0]));
			return true;
		}

		// calculate high zero bits in v's high used limb
		int shift = 32 - Degree32(v[v_used - 1]);
		int uu_used = u_used;
		if (shift > 0) uu_used++;

		u32 *uu = (u32*)alloca(uu_used*4);
		u32 *vv = (u32*)alloca(v_used*4);

		// shift left to fill high MSB of divisor
		if (shift > 0)
		{
			ShiftLeft(v_used, vv, v, shift);
			uu[u_used] = ShiftLeft(u_used, uu, u, shift);
		}
		else
		{
			Set(uu, u_used, u);
			Set(vv, v_used, v);
		}

		int q_high_index = uu_used - v_used;

		if (GreaterOrEqual(uu + q_high_index, v_used, vv, v_used))
		{
			Subtract(uu + q_high_index, v_used, vv, v_used);
			//Set32(q + q_high_index, u_used - q_high_index, 1);
		}
		else
		{
			//Set32(q + q_high_index, u_used - q_high_index, 0);
		}

		u32 *vq_product = (u32*)alloca((v_used+1)*4);

		// for each limb,
		for (int ii = q_high_index - 1; ii >= 0; --ii)
		{
			u64 q_full = *(u64*)(uu + ii + v_used - 1) / vv[v_used - 1];
			u32 q_low = (u32)q_full;
			u32 q_high = (u32)(q_full >> 32);

			vq_product[v_used] = Multiply32(v_used, vq_product, vv, q_low);

			if (q_high) // it must be '1'
				Add(vq_product + 1, v_used, vv, v_used);

			if (Subtract(uu + ii, v_used + 1, vq_product, v_used + 1))
			{
				//--q_low;
				if (Add(uu + ii, v_used + 1, vv, v_used) == 0)
				{
					//--q_low;
					Add(uu + ii, v_used + 1, vv, v_used);
				}
			}

			//q[ii] = q_low;
		}

		memset(r + v_used, 0, (v_limbs - v_used)*4);
		ShiftRight(v_used, r, uu, shift);

		return true;
	}

	// m_inv ~= 2^(2k)/m
	// Generates m_inv parameter of BarrettModulus()
	// It is limbs in size, chopping off the 2^k bit
	// Only works for m with the high bit set
	void BarrettModulusPrecomp(
		int limbs,		// Number of limbs in m and m_inv
		const u32 *m,	// Modulus, size = limbs
		u32 *m_inv)		// Large number result, size = limbs
	{
		u32 *q = (u32*)alloca((limbs*2+1)*4);

		// q = 2^(2k)
		Set32(q, limbs*2, 0);
		q[limbs*2] = 1;

		// q /= m
		Divide(q, limbs*2+1, m, limbs, q, m_inv);

		// m_inv = q
		Set(m_inv, limbs, q);
	}

	// r = x mod m
	// Using Barrett's method with precomputed m_inv
	void BarrettModulus(
		int limbs,			// Number of limbs in m and m_inv
		const u32 *x,		// Number to reduce, size = limbs*2
		const u32 *m,		// Modulus, size = limbs
		const u32 *m_inv,	// R/Modulus, precomputed, size = limbs
		u32 *result)		// Large number result
	{
		// q2 = x * m_inv
		// Skips the low limbs+1 words and some high limbs too
		// Needs to partially calculate the next 2 words below for carries
		u32 *q2 = (u32*)alloca((limbs+3)*4);
		int ii, jj = limbs - 1;

		// derived from the fact that m_inv[limbs] was always 1, so m_inv is the same length as modulus now
		*(u64*)q2 = (u64)m_inv[jj] * x[jj];
		*(u64*)(q2 + 1) = (u64)q2[1] + x[jj];

		for (ii = 1; ii < limbs; ++ii)
			*(u64*)(q2 + ii + 1) = ((u64)q2[ii + 1] + x[jj + ii]) + AddMultiply32(ii + 1, q2, m_inv + jj - ii, x[jj + ii]);

		*(u64*)(q2 + ii + 1) = ((u64)q2[ii + 1] + x[jj + ii]) + AddMultiply32(ii, q2 + 1, m_inv, x[jj + ii]);

		q2 += 2;

		// r2 = (q3 * m2) mod b^(k+1)
		u32 *r2 = (u32*)alloca((limbs+1)*4);

		// Skip high words in product, also input limbs are different by 1
		Multiply32(limbs + 1, r2, q2, m[0]);
		for (int ii = 1; ii < limbs; ++ii)
			AddMultiply32(limbs + 1 - ii, r2 + ii, q2, m[ii]);

		// Correct the error of up to two modulii
		u32 *r = (u32*)alloca((limbs+1)*4);
		if (Subtract(r, x, limbs+1, r2, limbs+1))
		{
			while (!Subtract(r, limbs+1, m, limbs));
		}
		else
		{
			while (GreaterOrEqual(r, limbs+1, m, limbs))
				Subtract(r, limbs+1, m, limbs);
		}

		Set(result, limbs, r);
	}

	// r = x % m
	// For m in the special form: 2^(N=wordbits*m_limbs) - c
	// Assumes c < 2^28, to allow for numerators 4x larger than m
	void SpecialModulus(
		const u32 *x,	// numerator, size = x_limbs
		int x_limbs,	// always > m_limbs
		u32 c,			// small subtracted constant in m
		int m_limbs,	// number of limbs in modulus
		u32 *r)			// remainder, size = m_limbs
	{
		const u32 *q = x + m_limbs;
		int q_limbs = LimbDegree(q, x_limbs - m_limbs); // always >= 0
		u32 r_overflow = 0;
		u32 *qr = (u32*)alloca((x_limbs+1)*4);

		// Unrolled first loop to avoid some copies
		if (!q_limbs)
		{
			// r = x(mod 2^N)
			Set(r, m_limbs, x);
		}
		else
		{
			// q|r = q*c / b^t
			qr[q_limbs++] = Multiply32(q_limbs, qr, q, c);

			if (q_limbs <= m_limbs)
				r_overflow += Add(r, x, m_limbs, qr, q_limbs);
			else
			{
				r_overflow += Add(r, x, m_limbs, qr, m_limbs);

				q = qr + m_limbs;

				for (;;)
				{
					q_limbs -= m_limbs;
					q_limbs = LimbDegree(q, q_limbs);
					if (!q_limbs) break;

					// q|r = q*c / b^t
					qr[q_limbs++] = Multiply32(q_limbs, qr, q, c);

					if (q_limbs <= m_limbs)
					{
						r_overflow += Add(r, m_limbs, qr, q_limbs);
						break;
					}
					else
						r_overflow += Add(r, m_limbs, qr, m_limbs);
				}
			}
		}

		// r is probably a few moduli larger than it should be

		// Get it down to a number that could be just one modulus too large
		// To subtract 2^N - c, we just need to add c a few times
		if (r_overflow) Add32(r, m_limbs, r_overflow * c);

		// If number is one modulus too large, adding c to it will cause an
		// overflow, and we can use the low words of the result
		Set(qr, m_limbs, r);
		if (Add32(qr, m_limbs, c))
			Set(r, m_limbs, qr);
	}

	// result = (x * y) (Mod modulus)
	bool MulMod(
		int limbs,			// Number of limbs in x,y,modulus
		const u32 *x,		// Large number x
		const u32 *y,		// Large number y
		const u32 *modulus,	// Large number modulus
		u32 *result)		// Large number result
	{
		u32 *product = (u32*)alloca(limbs*2*4);

		Multiply(limbs, product, x, y);

		return Modulus(product, limbs * 2, modulus, limbs, result);
	}

	// Convert bigint to string
	std::string ToStr(const u32 *n, int limbs, int base)
	{
		limbs = LimbDegree(n, limbs);
		if (!limbs) return "0";

		std::string out;
		char ch;

		u32 *m = (u32*)alloca(limbs*4);
		Set(m, limbs, n, limbs);

		while (limbs)
		{
			u32 mod = Divide32(limbs, m, base);
			if (mod <= 9) ch = '0' + mod;
			else ch = 'A' + mod - 10;
			out = ch + out;
			limbs = LimbDegree(m, limbs);
		}

		return out;
	}

	// Convert string to bigint
	// Return 0 if string contains non-digit characters, else number of limbs used
	int ToInt(u32 *lhs, int max_limbs, const char *rhs, u32 base)
	{
		if (max_limbs < 2) return 0;

		lhs[0] = 0;
		int used = 1;

		char ch;
		while ((ch = *rhs++))
		{
			u32 mod;
			if (ch >= '0' && ch <= '9') mod = ch - '0';
			else mod = toupper(ch) - 'A' + 10;
			if (mod >= base) return 0;

			// lhs *= base
			u32 carry = Multiply32Add32(used, lhs, base, mod);

			// react to running out of room
			if (carry)
			{
				if (used >= max_limbs)
					return 0;

				lhs[used++] = carry;
			}
		}

		if (used < max_limbs)
			Set32(lhs+used, max_limbs-used, 0);

		return used;
	}

	/*
	 * Computes: result = GCD(a, b)  (greatest common divisor)
	 *
	 * Length of result is the length of the smallest argument
	 */
	void GCD(
		const u32 *a,	//	Large number, buffer size = a_limbs
		int a_limbs,	//	Size of a
		const u32 *b,	//	Large number, buffer size = b_limbs
		int b_limbs,	//	Size of b
		u32 *result)	//	Large number, buffer size = min(a, b)
	{
		int limbs = (a_limbs <= b_limbs) ? a_limbs : b_limbs;

		u32 *g = (u32*)alloca(limbs*4);
		u32 *g1 = (u32*)alloca(limbs*4);

		if (a_limbs <= b_limbs)
		{
			// g = a, g1 = b (mod a)
			Set(g, limbs, a, a_limbs);
			Modulus(b, b_limbs, a, a_limbs, g1);
		}
		else
		{
			// g = b, g1 = a (mod b)
			Set(g, limbs, b, b_limbs);
			Modulus(a, a_limbs, b, b_limbs, g1);
		}

		for (;;) {
			// g = (g mod g1)
			Modulus(g, limbs, g1, limbs, g);

			if (!LimbDegree(g, limbs)) {
				Set(result, limbs, g1, limbs);
				return;
			}

			// g1 = (g1 mod g)
			Modulus(g1, limbs, g, limbs, g1);

			if (!LimbDegree(g1, limbs)) {
				Set(result, limbs, g, limbs);
				return;
			}
		}
	}

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
		int limbs,		// Limbs in modulus(v) and result
		u32 *result)	// Large number, buffer size = limbs
	{
		u32 *u1 = (u32*)alloca(limbs*4);
		u32 *u3 = (u32*)alloca(limbs*4);
		u32 *v1 = (u32*)alloca(limbs*4);
		u32 *v3 = (u32*)alloca(limbs*4);
		u32 *t1 = (u32*)alloca(limbs*4);
		u32 *t3 = (u32*)alloca(limbs*4);
		u32 *q = (u32*)alloca((limbs + u_limbs)*4);

		// Unrolled first iteration
		{
			Set32(u1, limbs, 0);
			Set32(v1, limbs, 1);
			Set(u3, limbs, v);

			// v3 = u % v
			Modulus(u, u_limbs, v, limbs, v3);
		}

		for (;;)
		{
			if (!LimbDegree(v3, limbs))
			{
				Subtract(result, v, limbs, u1, limbs);
				return Equal32(u3, limbs, 1);
			}

			Divide(u3, limbs, v3, limbs, q, t3);
			SimpleMultiplyLowHalf(limbs, t1, q, v1);
			Add(t1, limbs, u1, limbs);

			if (!LimbDegree(t3, limbs))
			{
				Set(result, limbs, v1);
				return Equal32(v3, limbs, 1);
			}

			Divide(v3, limbs, t3, limbs, q, u3);
			SimpleMultiplyLowHalf(limbs, u1, q, t1);
			Add(u1, limbs, v1, limbs);

			if (!LimbDegree(u3, limbs))
			{
				Subtract(result, v, limbs, t1, limbs);
				return Equal32(t3, limbs, 1);
			}

			Divide(t3, limbs, u3, limbs, q, v3);
			SimpleMultiplyLowHalf(limbs, v1, q, u1);
			Add(v1, limbs, t1, limbs);

			if (!LimbDegree(v3, limbs))
			{
				Set(result, limbs, u1);
				return Equal32(u3, limbs, 1);
			}

			Divide(u3, limbs, v3, limbs, q, t3);
			SimpleMultiplyLowHalf(limbs, t1, q, v1);
			Add(t1, limbs, u1, limbs);

			if (!LimbDegree(t3, limbs))
			{
				Subtract(result, v, limbs, v1, limbs);
				return Equal32(v3, limbs, 1);
			}

			Divide(v3, limbs, t3, limbs, q, u3);
			SimpleMultiplyLowHalf(limbs, u1, q, t1);
			Add(u1, limbs, v1, limbs);

			if (!LimbDegree(u3, limbs))
			{
				Set(result, limbs, t1);
				return Equal32(t3, limbs, 1);
			}

			Divide(t3, limbs, u3, limbs, q, v3);
			SimpleMultiplyLowHalf(limbs, v1, q, u1);
			Add(v1, limbs, t1, limbs);
		}
	}

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

	// Take the square root in a finite field modulus a prime m.
	// Special conditions: m = 2^N - c = 3 (mod 4) -> c = 1 (mod 4)
	void SpecialSquareRoot(
		int limbs,
		const u32 *x,
		u32 c,
		u32 *r)
	{
		// Compute x ^ (m + 1)/4 = x ^ ((2^N-c) >> 2)
		u32 *p = (u32*)alloca(limbs*2*4);
		u32 *m = (u32*)alloca(limbs*4);
		u32 *x_squared = (u32*)alloca(limbs*4);

		// m = modulus >> 2
		Set32(m, limbs, 0);
		Subtract32(m, limbs, c);
		Add32(m, limbs, 1);
		ShiftRight(limbs, m, m, 2);

		bool seen = false;

		for (int limb = limbs - 1; limb >= 0; --limb)
		{
			for (u32 bit = 1 << 31; bit; bit >>= 1)
			{
				if (!seen)
				{
					if (m[limb] & bit)
					{
						Set(x_squared, limbs, x);
						seen = true;
					}
				}
				else
				{
					Square(limbs, p, x_squared);
					SpecialModulus(p, limbs*2, c, limbs, x_squared);

					if (m[limb] & bit)
					{
						Multiply(limbs, p, x_squared, x);
						SpecialModulus(p, limbs*2, c, limbs, x_squared);
					}
				}
			}
		}

		Set(r, limbs, x_squared);
	}

	// Calculates mod_inv from low limb of modulus for Mon*()
	u32 MonReducePrecomp(u32 modulus0)
	{
		// mod_inv = -M ^ -1 (Mod 2^32)
		return MulInverse32(-(s32)modulus0);
	}

	// Compute n_residue for Montgomery reduction
	void MonInputResidue(
		const u32 *n,		//	Large number, buffer size = n_limbs
		int n_limbs,		//	Number of limbs in n
		const u32 *modulus,	//	Large number, buffer size = m_limbs
		int m_limbs,		//	Number of limbs in modulus
		u32 *n_residue)		//	Result, buffer size = m_limbs
	{
		// p = n * 2^(k*m)
		u32 *p = (u32*)alloca((n_limbs+m_limbs)*4);
		Set(p+m_limbs, n_limbs, n, n_limbs);
		Set32(p, m_limbs, 0);

		// n_residue = p (Mod modulus)
		Modulus(p, n_limbs+m_limbs, modulus, m_limbs, n_residue);
	}

	// result = a * b * r^-1 (Mod modulus) in Montgomery domain
	void MonPro(
		int limbs,				// Number of limbs in each parameter
		const u32 *a_residue,	// Large number, buffer size = limbs
		const u32 *b_residue,	// Large number, buffer size = limbs
		const u32 *modulus,		// Large number, buffer size = limbs
		u32 mod_inv,			// MonReducePrecomp() return
		u32 *result)			// Large number, buffer size = limbs
	{
		u32 *t = (u32*)alloca(limbs*2*4);

		Multiply(limbs, t, a_residue, b_residue);
		MonReduce(limbs, t, modulus, mod_inv, result);
	}

	// result = a^-1 (Mod modulus) in Montgomery domain
	void MonInverse(
		int limbs,				// Number of limbs in each parameter
		const u32 *a_residue,	// Large number, buffer size = limbs
		const u32 *modulus,		// Large number, buffer size = limbs
		u32 mod_inv,			// MonReducePrecomp() return
		u32 *result)			// Large number, buffer size = limbs
	{
		Set(result, limbs, a_residue);
		MonFinish(limbs, result, modulus, mod_inv);
		InvMod(result, limbs, modulus, limbs, result);
		MonInputResidue(result, limbs, modulus, limbs, result);
	}

	// result = a * r^-1 (Mod modulus) in Montgomery domain
	// The result may be greater than the modulus, but this is okay since
	// the result is still in the RNS.  MonFinish() corrects this at the end.
	void MonReduce(
		int limbs,			// Number of limbs in modulus
		u32 *s,				// Large number, buffer size = limbs*2, gets clobbered
		const u32 *modulus,	// Large number, buffer size = limbs
		u32 mod_inv,		// MonReducePrecomp() return
		u32 *result)		// Large number, buffer size = limbs
	{
		// This function is roughly 60% of the cost of exponentiation
		for (int ii = 0; ii < limbs; ++ii)
		{
			u32 q = s[0] * mod_inv;
			s[0] = AddMultiply32(limbs, s, modulus, q);
			++s;
		}

		// Add the saved carries
		if (Add(result, s, limbs, s - limbs, limbs))
		{
			// Reduce the result only when needed
			Subtract(result, limbs, modulus, limbs);
		}
	}

	// result = a * r^-1 (Mod modulus) in Montgomery domain
	void MonFinish(
		int limbs,			// Number of limbs in each parameter
		u32 *n,				// Large number, buffer size = limbs
		const u32 *modulus,	// Large number, buffer size = limbs
		u32 mod_inv)		// MonReducePrecomp() return
	{
		u32 *t = (u32*)alloca(limbs*2*4);
		memcpy(t, n, limbs*4);
		memset(t + limbs, 0, limbs*4);

		// Reduce the number
		MonReduce(limbs, t, modulus, mod_inv, n);

		// Fix MonReduce() results greater than the modulus
		if (!Less(limbs, n, modulus))
			Subtract(n, limbs, modulus, limbs);
	}

	// Simple internal version without windowing for small exponents
	static void SimpleMonExpMod(
		const u32 *base,	//	Base for exponentiation, buffer size = mod_limbs
		const u32 *exponent,//	Exponent, buffer size = exponent_limbs
		int exponent_limbs,	//	Number of limbs in exponent
		const u32 *modulus,	//	Modulus, buffer size = mod_limbs
		int mod_limbs,		//	Number of limbs in modulus
		u32 mod_inv,		//	MonReducePrecomp() return
		u32 *result)		//	Result, buffer size = mod_limbs
	{
		bool set = false;

		u32 *temp = (u32*)alloca((mod_limbs*2)*4);

		// Run down exponent bits and use the squaring method
		for (int ii = exponent_limbs - 1; ii >= 0; --ii)
		{
			u32 e_i = exponent[ii];

			for (u32 mask = 0x80000000; mask; mask >>= 1)
			{
				if (set)
				{
					// result = result^2
					Square(mod_limbs, temp, result);
					MonReduce(mod_limbs, temp, modulus, mod_inv, result);

					if (e_i & mask)
					{
						// result *= base
						Multiply(mod_limbs, temp, result, base);
						MonReduce(mod_limbs, temp, modulus, mod_inv, result);
					}
				}
				else
				{
					if (e_i & mask)
					{
						// result = base
						Set(result, mod_limbs, base, mod_limbs);
						set = true;
					}
				}
			}
		}
	}

	// Precompute a window for ExpMod() and MonExpMod()
	// Requires 2^window_bits multiplies
	u32 *PrecomputeWindow(const u32 *base, const u32 *modulus, int limbs, u32 mod_inv, int window_bits)
	{
		u32 *temp = (u32*)alloca(limbs*2*4);

		u32 *base_squared = (u32*)alloca(limbs*4);
		Square(limbs, temp, base);
		MonReduce(limbs, temp, modulus, mod_inv, base_squared);

		// precomputed window starts with 000001, 000011, 000101, 000111, ...
		u32 k = (1 << (window_bits - 1));

		u32 *window = new u32[limbs * k];

		u32 *cw = window;
		Set(window, limbs, base);

		while (--k)
		{
			// cw+1 = cw * base^2
			Multiply(limbs, temp, cw, base_squared);
			MonReduce(limbs, temp, modulus, mod_inv, cw + limbs);
			cw += limbs;
		}

		return window;
	};

	// Computes: result = base ^ exponent (Mod modulus)
	// Using Montgomery multiplication with simple squaring method
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
		u32 *window = PrecomputeWindow(base, modulus, mod_limbs, mod_inv, window_bits);

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

	// Computes: result = base ^ exponent (Mod modulus)
	// Using Montgomery multiplication with simple squaring method
	void ExpMod(
		const u32 *base,	//	Base for exponentiation, buffer size = base_limbs
		int base_limbs,		//	Number of limbs in base
		const u32 *exponent,//	Exponent, buffer size = exponent_limbs
		int exponent_limbs,	//	Number of limbs in exponent
		const u32 *modulus,	//	Modulus, buffer size = mod_limbs
		int mod_limbs,		//	Number of limbs in modulus
		u32 mod_inv,		//	MonReducePrecomp() return
		u32 *result)		//	Result, buffer size = mod_limbs
	{
		u32 *mon_base = (u32*)alloca(mod_limbs*4);
		MonInputResidue(base, base_limbs, modulus, mod_limbs, mon_base);

		MonExpMod(mon_base, exponent, exponent_limbs, modulus, mod_limbs, mod_inv, result);

		MonFinish(mod_limbs, result, modulus, mod_inv);
	}

	// returns b ^ e (Mod m)
	u32 ExpMod32(u32 b, u32 e, u32 m)
	{
		// validate arguments
		if (b == 0 || m <= 1) return 0;
		if (e == 0) return 1;

		// find high bit of exponent
		u32 mask = 0x80000000;
		while ((e & mask) == 0) mask >>= 1;

		// seen 1 set bit, so result = base so far
		u32 r = b;

		while (mask >>= 1)
		{
			// VS.NET does a poor job recognizing that the division
			// is just an IDIV with a 32-bit dividend (not 64-bit) :-(

			// r = r^2 (mod m)
			r = (u32)(((u64)r * r) % m);

			// if exponent bit is set, r = r*b (mod m)
			if (e & mask) r = (u32)(((u64)r * b) % m);
		}

		return r;
	}

	// Rabin-Miller method for finding a strong pseudo-prime
	// Preconditions: High bit and low bit of n = 1
	bool RabinMillerPrimeTest(
		IRandom *prng,
		const u32 *n,	// Number to check for primality
		int limbs,		// Number of limbs in n
		u32 k)			// Confidence level (40 is pretty good)
	{
		// n1 = n - 1
		u32 *n1 = (u32 *)alloca(limbs*4);
		Set(n1, limbs, n);
		Subtract32(n1, limbs, 1);

		// d = n1
		u32 *d = (u32 *)alloca(limbs*4);
		Set(d, limbs, n1);

		// remove factors of two from d
		while (!(d[0] & 1))
			ShiftRight(limbs, d, d, 1);

		u32 *a = (u32 *)alloca(limbs*4);
		u32 *t = (u32 *)alloca(limbs*4);
		u32 *p = (u32 *)alloca((limbs*2)*4);
		u32 n_inv = MonReducePrecomp(n[0]);

		// iterate k times
		while (k--)
		{
			do prng->Generate(a, limbs*4);
			while (GreaterOrEqual(a, limbs, n, limbs));

			// a = a ^ d (Mod n)
			ExpMod(a, limbs, d, limbs, n, limbs, n_inv, a);

			Set(t, limbs, d);
			while (!Equal(limbs, t, n1) &&
				   !Equal32(a, limbs, 1) &&
				   !Equal(limbs, a, n1))
			{
				// TODO: verify this is actually working

				// a = a^2 (Mod n), non-critical path
				Square(limbs, p, a);
				Modulus(p, limbs*2, n, limbs, a);

				// t <<= 1
				ShiftLeft(limbs, t, t, 1);
			}

			if (!Equal(limbs, a, n1) && !(t[0] & 1)) return false;
		}

		return true;
	}

	// Generate a strong pseudo-prime using the Rabin-Miller primality test
	void GenerateStrongPseudoPrime(
		IRandom *prng,
		u32 *n,			// Output prime
		int limbs)		// Number of limbs in n
	{
		do {
			prng->Generate(n, limbs*4);
			n[limbs-1] |= 0x80000000;
			n[0] |= 1;
		} while (!RabinMillerPrimeTest(prng, n, limbs, 40)); // 40 iterations
	}
}
