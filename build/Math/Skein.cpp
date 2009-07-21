#include <cat/AllMath.hpp>
using namespace cat;


struct Context
{
	int result_bits;
	int bytes_used;
	u64 TweakWords[2];

	u64 X[4];
	u8 b[32];
};

enum
{
	R_256_0_0 =  5, R_256_0_1 = 56,
	R_256_1_0 = 36, R_256_1_1 = 28,
	R_256_2_0 = 13, R_256_2_1 = 46,
	R_256_3_0 = 58, R_256_3_1 = 44,
	R_256_4_0 = 26, R_256_4_1 = 20,
	R_256_5_0 = 53, R_256_5_1 = 35,
	R_256_6_0 = 11, R_256_6_1 = 42,
	R_256_7_0 = 59, R_256_7_1 = 50,
};

static const u64 SKEIN_KS_PARITY = 0x5555555555555555LL;

void Skein_256_Process_Block(Context *ctx, const u8 *buffer, u32 blocks, u32 bytes)
{
	u64 KeyScheduleTweak[3];
	u64 KeyScheduleChain[4+1];
	u64 X[4];
	u64 w[4];

	while (blocks--)
	{
		if ((ctx->TweakWords[0] += bytes) < bytes)
			++ctx->TweakWords[1];

		KeyScheduleChain[4] = SKEIN_KS_PARITY;

		for (int ii = 0; ii < 4; ++ii)
		{
			KeyScheduleChaining[ii] = ctx->X[ii];
			KeyScheduleChaining[4] ^= ctx->X[ii];
		}

		KeyScheduleTweak[0] = ctx->TweakWords[0];
	}
}
