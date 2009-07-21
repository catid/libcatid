/*
	Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
	published by the Free Software Foundation, either version 3 of
	the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
	License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cat/crypt/hash/Skein.hpp>
#include <cat/port/EndianNeutral.hpp>
using namespace cat;

#define THREEFISH(R0, R1, R2, R3) \
	x0 += x1; x1 = CAT_ROL64(x1, R0); x1 ^= x0; \
	x2 += x3; x3 = CAT_ROL64(x3, R1); x3 ^= x2; \
	x0 += x3; x3 = CAT_ROL64(x3, R2); x3 ^= x0; \
	x2 += x1; x1 = CAT_ROL64(x1, R3); x1 ^= x2;

#define INJECTKEY(K0, K1, K2, K3, T0, T1, R) \
	x0 += (K0); \
	x1 += (K1) + (T0); \
	x2 += (K2) + (T1); \
	x3 += (K3) + (R);

void Skein::HashComputation256(const void *_message, int blocks, u32 byte_count, u64 *NextState)
{
	const int BITS = 256;
	const int WORDS = BITS / 64;
	const int BYTES = BITS / 8;
	const u64 *message = (const u64 *)_message;

	// Key schedule: Chaining
	u64 k[5];
	memcpy(k, State, BYTES);

	// Key schedule: Tweak
	u64 t0 = Tweak[0];
	u64 t1 = Tweak[1];

	do
	{
		t0 += byte_count;

		// Parity extension
		u64 t2 = t0 ^ t1;
		k[4] = 0x5555555555555555LL ^ k[0] ^ k[1] ^ k[2] ^ k[3];

		// First full key injection
		register u64 x0 = k[0] + getLE(message[0]);
		register u64 x1 = k[1] + getLE(message[1]) + t0;
		register u64 x2 = k[2] + getLE(message[2]) + t1;
		register u64 x3 = k[3] + getLE(message[3]);

		// 72 rounds

		const enum {
			R_256_0_0= 5, R_256_0_1=56,
			R_256_1_0=36, R_256_1_1=28,
			R_256_2_0=13, R_256_2_1=46,
			R_256_3_0=58, R_256_3_1=44,
			R_256_4_0=26, R_256_4_1=20,
			R_256_5_0=53, R_256_5_1=35,
			R_256_6_0=11, R_256_6_1=42,
			R_256_7_0=59, R_256_7_1=50
		};

		for (int round = 1; round <= 18; round += 6)
		{
			THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
			THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);

			u64 k1 = k[(round)%5];
			u64 k2 = k[(round+1)%5];
			u64 k3 = k[(round+2)%5];
			u64 k4 = k[(round+3)%5];
			u64 k0 = k[(round+4)%5];

			INJECTKEY(k1, k2, k3, k4, t1, t2, round);
			THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
			THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
			INJECTKEY(k2, k3, k4, k0, t2, t0, round+1);
			THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
			THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
			INJECTKEY(k3, k4, k0, k1, t0, t1, round+2);
			THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
			THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
			INJECTKEY(k4, k0, k1, k2, t1, t2, round+3);
			THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
			THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
			INJECTKEY(k0, k1, k2, k3, t2, t0, round+4);
			THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
			THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
			INJECTKEY(k1, k2, k3, k4, t0, t1, round+5);
		}
/*
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k1, k2, k3, k4, t1, t2, 1);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k2, k3, k4, k0, t2, t0, 2);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k3, k4, k0, k1, t0, t1, 3);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k4, k0, k1, k2, t1, t2, 4);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k0, k1, k2, k3, t2, t0, 5);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k1, k2, k3, k4, t0, t1, 6);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k2, k3, k4, k0, t1, t2, 7);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k3, k4, k0, k1, t2, t0, 8);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k4, k0, k1, k2, t0, t1, 9);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k0, k1, k2, k3, t1, t2, 10);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k1, k2, k3, k4, t2, t0, 11);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k2, k3, k4, k0, t0, t1, 12);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k3, k4, k0, k1, t1, t2, 13);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k4, k0, k1, k2, t2, t0, 14);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k0, k1, k2, k3, t0, t1, 15);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k1, k2, k3, k4, t1, t2, 16);
		THREEFISH(R_256_0_0, R_256_0_1, R_256_1_0, R_256_1_1);
		THREEFISH(R_256_2_0, R_256_2_1, R_256_3_0, R_256_3_1);
		INJECTKEY(k2, k3, k4, k0, t2, t0, 17);
		THREEFISH(R_256_4_0, R_256_4_1, R_256_5_0, R_256_5_1);
		THREEFISH(R_256_6_0, R_256_6_1, R_256_7_0, R_256_7_1);
		INJECTKEY(k3, k4, k0, k1, t0, t1, 18);
*/
		// Feedforward XOR
		k[0] = x0 ^ getLE(message[0]);
		k[1] = x1 ^ getLE(message[1]);
		k[2] = x2 ^ getLE(message[2]);
		k[3] = x3 ^ getLE(message[3]);

		// Update tweak
		t1 &= ~T1_MASK_FIRST;

		// Eat data bytes
		message += WORDS;
	} while (--blocks > 0);

	// Update tweak
	Tweak[0] = t0;
	Tweak[1] = t1;

	// Update state
	memcpy(NextState, k, BYTES);
}
