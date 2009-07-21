#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
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
}
