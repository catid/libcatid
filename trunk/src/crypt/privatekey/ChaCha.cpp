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

#include <cat/crypt/symmetric/ChaCha.hpp>
#include <cat/port/EndianNeutral.hpp>
#include <string.h>
using namespace cat;

ChaCha::~ChaCha()
{
    CAT_OBJCLR(state);
}

static u32 InitialState[12] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	// These are from BLAKE-32:
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    // Took the rest of these from the SHA-256 SBOX constants:
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
};

// Key up to 384 bits, must be a multiple of 32 bits
void ChaCha::Key(const void *key, int bytes)
{
    if (bytes > 48) bytes = 48;

    int remaining = 48 - bytes;

    memcpy(state, InitialState, remaining);
    memcpy(&state[remaining/4], key, bytes);

    // Fix byte order for math
    for (int ii = remaining/4; ii < 12; ++ii)
        swapLE(state[ii]);
}

// 64-bit IV
void ChaCha::Begin(u64 iv)
{
    // Initialize block counter to zero
    state[12] = 0;
    state[13] = 0;

    // Initialize IV
    state[14] = (u32)iv;
    state[15] = (u32)(iv >> 32);
}

#define QUARTERROUND(a,b,c,d) \
  x[a] += x[b]; x[d] = CAT_ROL32(x[d] ^ x[a], 16); \
  x[c] += x[d]; x[b] = CAT_ROL32(x[b] ^ x[c], 12); \
  x[a] += x[b]; x[d] = CAT_ROL32(x[d] ^ x[a], 8); \
  x[c] += x[d]; x[b] = CAT_ROL32(x[b] ^ x[c], 7);

void ChaCha::GenerateKeyStream(u32 *out)
{
    // Update block counter
    if (!++state[12]) state[13]++;

    register u32 x[16];

    // Copy state into work registers
    for (int ii = 0; ii < 16; ++ii)
        x[ii] = state[ii];

    // Mix state for 12 rounds
    for (int round = 12; round > 0; round -= 2)
    {
        QUARTERROUND(0, 4, 8,  12)
        QUARTERROUND(1, 5, 9,  13)
        QUARTERROUND(2, 6, 10, 14)
        QUARTERROUND(3, 7, 11, 15)
        QUARTERROUND(0, 5, 10, 15)
        QUARTERROUND(1, 6, 11, 12)
        QUARTERROUND(2, 7, 8,  13)
        QUARTERROUND(3, 4, 9,  14)
    }

    // Add state to mixed state, little-endian
    for (int jj = 0; jj < 16; ++jj)
        out[jj] = getLE(x[jj] + state[jj]);
}

// Message with any number of bytes
void ChaCha::Crypt(const void *in, void *out, int bytes)
{
    const u64 *in64 = (const u64 *)in;
    u64 *out64 = (u64 *)out;

    while (bytes >= 64)
    {
        u64 key64[8];
        GenerateKeyStream((u32*)key64);

        out64[0] = in64[0] ^ key64[0];
        out64[1] = in64[1] ^ key64[1];
        out64[2] = in64[2] ^ key64[2];
        out64[3] = in64[3] ^ key64[3];
        out64[4] = in64[4] ^ key64[4];
        out64[5] = in64[5] ^ key64[5];
        out64[6] = in64[6] ^ key64[6];
        out64[7] = in64[7] ^ key64[7];

        out64 += 8;
        in64 += 8;
        bytes -= 64;
    }

    if (bytes)
    {
        u64 key64[8];
        GenerateKeyStream((u32*)key64);

        int words = bytes / 8;
        for (int ii = 0; ii < words; ++ii)
            out64[ii] = in64[ii] ^ key64[ii];

        const u8 *in8 = (const u8 *)(in64 + words);
        u8 *out8 = (u8 *)(out64 + words);
        const u8 *key8 = (const u8 *)(key64 + words);

        switch (bytes % 8)
        {
        case 7: out8[6] = in8[6] ^ key8[6];
        case 6: out8[5] = in8[5] ^ key8[5];
        case 5: out8[4] = in8[4] ^ key8[4];
        case 4: *(u32*)out8 = *(const u32*)in8 ^ *(const u32*)key8;
            break;
        case 3: out8[2] = in8[2] ^ key8[2];
        case 2: out8[1] = in8[1] ^ key8[1];
        case 1: out8[0] = in8[0] ^ key8[0];
        }
    }
}
