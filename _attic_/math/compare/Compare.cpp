#include <cat/math/BigInt.hpp>
#include <cat/math/BitMath.hpp>
#include <malloc.h>

namespace cat
{
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
}
