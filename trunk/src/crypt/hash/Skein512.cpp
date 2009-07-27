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

#define THREEFISH(R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, RA, RB, RC, RD, RE, RF) \
    x[0] += x[1]; x[1] = CAT_ROL64(x[1], R0); x[1] ^= x[0]; \
    x[2] += x[3]; x[3] = CAT_ROL64(x[3], R1); x[3] ^= x[2]; \
    x[4] += x[5]; x[5] = CAT_ROL64(x[5], R2); x[5] ^= x[4]; \
    x[6] += x[7]; x[7] = CAT_ROL64(x[7], R3); x[7] ^= x[6]; \
    x[2] += x[1]; x[1] = CAT_ROL64(x[1], R4); x[1] ^= x[2]; \
    x[4] += x[7]; x[7] = CAT_ROL64(x[7], R5); x[7] ^= x[4]; \
    x[6] += x[5]; x[5] = CAT_ROL64(x[5], R6); x[5] ^= x[6]; \
    x[0] += x[3]; x[3] = CAT_ROL64(x[3], R7); x[3] ^= x[0]; \
    x[4] += x[1]; x[1] = CAT_ROL64(x[1], R8); x[1] ^= x[4]; \
    x[6] += x[3]; x[3] = CAT_ROL64(x[3], R9); x[3] ^= x[6]; \
    x[0] += x[5]; x[5] = CAT_ROL64(x[5], RA); x[5] ^= x[0]; \
    x[2] += x[7]; x[7] = CAT_ROL64(x[7], RB); x[7] ^= x[2]; \
    x[6] += x[1]; x[1] = CAT_ROL64(x[1], RC); x[1] ^= x[6]; \
    x[0] += x[7]; x[7] = CAT_ROL64(x[7], RD); x[7] ^= x[0]; \
    x[2] += x[5]; x[5] = CAT_ROL64(x[5], RE); x[5] ^= x[2]; \
    x[4] += x[3]; x[3] = CAT_ROL64(x[3], RF); x[3] ^= x[4];

#define INJECTKEY(K0, K1, K2, K3, K4, K5, K6, K7, T0, T1, R) \
    x[0] += (K0); \
    x[1] += (K1); \
    x[2] += (K2); \
    x[3] += (K3); \
    x[4] += (K4); \
    x[5] += (K5) + (T0); \
    x[6] += (K6) + (T1); \
    x[7] += (K7) + (R);

void Skein::HashComputation512(const void *_message, int blocks, u32 byte_count, u64 *NextState)
{
    const int BITS = 512;
    const int WORDS = BITS / 64;
    const int BYTES = BITS / 8;
    const u64 *message = (const u64 *)_message;

    // Key schedule: Chaining
    u64 k[WORDS+1];
    memcpy(k, State, BYTES);

    // Key schedule: Tweak
    u64 t[3];
    memcpy(t, Tweak, sizeof(Tweak));

    do
    {
        t[0] += byte_count;

        // Parity extension
        t[2] = t[0] ^ t[1];
        k[WORDS] = 0x5555555555555555LL;

        // First full key injection
        u64 x[WORDS];
        for (int ii = 0; ii < WORDS; ++ii)
        {
            x[ii] = k[ii] + getLE(message[ii]);
            k[WORDS] ^= k[ii];
        }

        x[WORDS-3] += t[0];
        x[WORDS-2] += t[1];

        // 72 rounds

        enum {
            R_512_0_0=38, R_512_0_1=30, R_512_0_2=50, R_512_0_3=53,
            R_512_1_0=48, R_512_1_1=20, R_512_1_2=43, R_512_1_3=31,
            R_512_2_0=34, R_512_2_1=14, R_512_2_2=15, R_512_2_3=27,
            R_512_3_0=26, R_512_3_1=12, R_512_3_2=58, R_512_3_3= 7,
            R_512_4_0=33, R_512_4_1=49, R_512_4_2= 8, R_512_4_3=42,
            R_512_5_0=39, R_512_5_1=27, R_512_5_2=41, R_512_5_3=14,
            R_512_6_0=29, R_512_6_1=26, R_512_6_2=11, R_512_6_3= 9,
            R_512_7_0=33, R_512_7_1=51, R_512_7_2=39, R_512_7_3=35,
        };

#if defined(CAT_ARCH_64)
        // Should unroll farther on 64-bit OS
        for (int round = 1; round <= 18; round += 6)
        {
            THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                      R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);

            u64 k0 = k[(round)%9];
            u64 k1 = k[(round+1)%9];
            u64 k2 = k[(round+2)%9];
            u64 k3 = k[(round+3)%9];
            u64 k4 = k[(round+4)%9];
            u64 k5 = k[(round+5)%9];
            u64 k6 = k[(round+6)%9];
            u64 k7 = k[(round+7)%9];
            u64 k8 = k[(round+8)%9];

            INJECTKEY(k0, k1, k2, k3, k4, k5, k6, k7, t[1], t[2], round);

            THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                      R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
            INJECTKEY(k1, k2, k3, k4, k5, k6, k7, k8, t[2], t[0], round+1);
            THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                      R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
            INJECTKEY(k2, k3, k4, k5, k6, k7, k8, k0, t[0], t[1], round+2);
            THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                      R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
            INJECTKEY(k3, k4, k5, k6, k7, k8, k0, k1, t[1], t[2], round+3);
            THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                      R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
            INJECTKEY(k4, k5, k6, k7, k8, k0, k1, k2, t[2], t[0], round+4);
            THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                      R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
            INJECTKEY(k5, k6, k7, k8, k0, k1, k2, k3, t[0], t[1], round+5);
        }
#else
        for (int round = 1; round <= 18; round += 2)
        {
            THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                      R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);

            u64 k0 = k[(round)%9];
            u64 k1 = k[(round+1)%9];
            u64 k2 = k[(round+2)%9];
            u64 k3 = k[(round+3)%9];
            u64 k4 = k[(round+4)%9];
            u64 k5 = k[(round+5)%9];
            u64 k6 = k[(round+6)%9];
            u64 k7 = k[(round+7)%9];
            u64 k8 = k[(round+8)%9];

            u64 t1 = t[(round+1)%3];

            INJECTKEY(k0, k1, k2, k3, k4, k5, k6, k7, t[(round)%3], t1, round);

            THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                      R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);

            INJECTKEY(k1, k2, k3, k4, k5, k6, k7, k8, t1, t[(round+2)%3], round+1);
        }
#endif

/*
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[1], k[2], k[3], k[4], k[5], k[6], k[7], k[8], t1, t2, 1);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[2], k[3], k[4], k[5], k[6], k[7], k[8], k[0], t2, t0, 2);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[3], k[4], k[5], k[6], k[7], k[8], k[0], k[1], t0, t1, 3);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[4], k[5], k[6], k[7], k[8], k[0], k[1], k[2], t1, t2, 4);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[5], k[6], k[7], k[8], k[0], k[1], k[2], k[3], t2, t0, 5);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[6], k[7], k[8], k[0], k[1], k[2], k[3], k[4], t0, t1, 6);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[7], k[8], k[0], k[1], k[2], k[3], k[4], k[5], t1, t2, 7);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[8], k[0], k[1], k[2], k[3], k[4], k[5], k[6], t2, t0, 8);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7], t0, t1, 9);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[1], k[2], k[3], k[4], k[5], k[6], k[7], k[8], t1, t2, 10);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[2], k[3], k[4], k[5], k[6], k[7], k[8], k[0], t2, t0, 11);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[3], k[4], k[5], k[6], k[7], k[8], k[0], k[1], t0, t1, 12);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[4], k[5], k[6], k[7], k[8], k[0], k[1], k[2], t1, t2, 13);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[5], k[6], k[7], k[8], k[0], k[1], k[2], k[3], t2, t0, 14);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[6], k[7], k[8], k[0], k[1], k[2], k[3], k[4], t0, t1, 15);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[7], k[8], k[0], k[1], k[2], k[3], k[4], k[5], t1, t2, 16);
        THREEFISH(R_512_0_0, R_512_0_1, R_512_0_2, R_512_0_3, R_512_1_0, R_512_1_1, R_512_1_2, R_512_1_3,
                  R_512_2_0, R_512_2_1, R_512_2_2, R_512_2_3, R_512_3_0, R_512_3_1, R_512_3_2, R_512_3_3);
        INJECTKEY(k[8], k[0], k[1], k[2], k[3], k[4], k[5], k[6], t2, t0, 17);
        THREEFISH(R_512_4_0, R_512_4_1, R_512_4_2, R_512_4_3, R_512_5_0, R_512_5_1, R_512_5_2, R_512_5_3,
                  R_512_6_0, R_512_6_1, R_512_6_2, R_512_6_3, R_512_7_0, R_512_7_1, R_512_7_2, R_512_7_3);
        INJECTKEY(k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7], t0, t1, 18);
*/
        // Feedforward XOR
        for (int ii = 0; ii < WORDS; ++ii)
            k[ii] = x[ii] ^ getLE(message[ii]);

        // Update tweak
        t[1] &= ~T1_MASK_FIRST;

        // Eat data bytes
        message += WORDS;
    } while (--blocks > 0);

    // Update tweak
    memcpy(Tweak, t, sizeof(Tweak));

    // Update state
    memcpy(NextState, k, BYTES);
}
