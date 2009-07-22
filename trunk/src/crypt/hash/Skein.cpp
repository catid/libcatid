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

Skein::~Skein()
{
    CAT_OBJCLR(State);
    CAT_OBJCLR(Tweak);
    CAT_OBJCLR(Work);
}

void Skein::GenerateInitialState(int bits)
{
    u64 w[MAX_WORDS] = { getLE64(SCHEMA_VER), getLE64(bits), 0 };

    CAT_OBJCLR(State);

    // T1 = FIRST | FINAL | CFG
    Tweak[0] = 0;
    Tweak[1] = T1_MASK_FIRST | T1_MASK_FINAL | ((u64)BLK_TYPE_CFG << T1_POS_BLK_TYPE);

    (this->*hash_func)(w, 1, 32, State);
}

// Cached copies of initial state for different bit lengths
static const u64 State0_160[4] = {
    0xa38a0d80a3687723LL, 0xb73cdb6a5963ffc9LL, 0x9633e8ea07a1b447LL, 0xca0ed09ec9529c22LL
};

static const u64 State0_224[4] = {
    0xb80929699ae0f431LL, 0xd340dc14a06929dcLL, 0xae866594bde4dc5aLL, 0x339767c25a60ea1dLL
};

static const u64 State0_256[4] = {
    0x388512680e660046LL, 0x4b72d5dec5a8ff01LL, 0x281a9298ca5eb3a5LL, 0x54ca5249f46070c4LL
};

static const u64 State0_384[8] = {
    0xe5bf4d02ba62494cLL, 0x7aa1eabcc3e6fc68LL, 0xbbe5fc26e1038c5aLL, 0x53c9903e8f88e9faLL,
    0xf30d8dddfb940c83LL, 0x500fda3c4865abecLL, 0x2226c67f745bc5e7LL, 0x015da80077c639f7LL
};

static const u64 State0_512[8] = {
    0xa8d47980544a6e32LL, 0x847511533e9b1a8aLL, 0x6faee870d8e81a00LL, 0x58b0d9d6cb557f92LL,
    0x9bbc0051dac1d4e9LL, 0xb744e2b1d189e7caLL, 0x979350fa709c5ef3LL, 0x0350125a92067bcdLL
};

bool Skein::BeginKey(int bits)
{
    if (bits <= 256) {
        digest_bytes = 256 / 8;
        digest_words = 256 / 64;
        hash_func = &Skein::HashComputation256;
    } else if (bits <= 512) {
        digest_bytes = 512 / 8;
        digest_words = 512 / 64;
        hash_func = &Skein::HashComputation512;
    } else return false;

    // Try to use a cached copy of the initial state
    switch (bits)
    {
    case 160: memcpy(State, State0_160, sizeof(State0_160)); break;
    case 224: memcpy(State, State0_224, sizeof(State0_224)); break;
    case 256: memcpy(State, State0_256, sizeof(State0_256)); break;
    case 384: memcpy(State, State0_384, sizeof(State0_384)); break;
    case 512: memcpy(State, State0_512, sizeof(State0_512)); break;
    default: GenerateInitialState(bits);
    }

    // T1 = FIRST | KEY
    Tweak[0] = 0;
    Tweak[1] = T1_MASK_FIRST | ((u64)BLK_TYPE_KEY << T1_POS_BLK_TYPE);

    used_bytes = 0;
    output_prng_mode = false;

    return true;
}

bool Skein::SetKey(ICryptHash *key_hash)
{
    const Skein *parent = dynamic_cast<const Skein *>(key_hash);
    if (!parent) return false;

    memcpy(State, parent->State, sizeof(State));
    digest_bytes = parent->digest_bytes;
    digest_words = parent->digest_words;
    hash_func = parent->hash_func;

    // The user will then call one of the Begin() functions below:

    return true;
}

bool Skein::BeginMAC()
{
    // T1 = FIRST | MSG
    Tweak[0] = 0;
    Tweak[1] = T1_MASK_FIRST | ((u64)BLK_TYPE_MSG << T1_POS_BLK_TYPE);

    used_bytes = 0;
    output_prng_mode = false;

    return true;
}

bool Skein::BeginKDF()
{
    // T1 = FIRST | KDF
    Tweak[0] = 0;
    Tweak[1] = T1_MASK_FIRST | ((u64)BLK_TYPE_KDF << T1_POS_BLK_TYPE);

    used_bytes = 0;
    output_prng_mode = false;

    return true;
}

bool Skein::BeginPRNG()
{
    // T1 = FIRST | NONCE
    Tweak[0] = 0;
    Tweak[1] = T1_MASK_FIRST | ((u64)BLK_TYPE_NONCE << T1_POS_BLK_TYPE);

    used_bytes = 0;
    output_prng_mode = true;

    return true;
}

void Skein::Crunch(const void *_message, int bytes)
{
    const u8 *buffer = (const u8*)_message;

    // If there are bytes left to hash from last time,
    if (used_bytes)
    {
        // If we still cannot overflow the workspace,
        if (used_bytes + bytes <= digest_bytes)
        {
            // Just append the new message bytes
            memcpy(Work + used_bytes, buffer, bytes);
            used_bytes += bytes;
            return;
        }

        // Fill the rest of the workspace
        u32 copied = digest_bytes - used_bytes;

        memcpy(Work + used_bytes, buffer, copied);

        (this->*hash_func)(Work, 1, digest_bytes, State);

        // Eat those bytes of the message
        bytes -= copied;
        buffer += copied;
    }

    // If the remaining bytes of the message overflows the workspace,
    if (bytes > digest_bytes)
    {
        int eat_bytes = bytes - 1;

        // Hash directly from the message
        (this->*hash_func)(buffer, eat_bytes / digest_bytes, bytes, State);

        // Eat those bytes of the message
        eat_bytes &= ~(digest_bytes-1);
        buffer += eat_bytes;
        bytes -= eat_bytes;
    }

    // Copy what remains into the workspace
    memcpy(Work, buffer, bytes);
    used_bytes = bytes;
}

void Skein::End()
{
    // NOTE: We should always have at least one message hash to perform here

    // Pad with zeroes
    memset(Work + used_bytes, 0, digest_bytes - used_bytes);

    // Final message hash
    Tweak[1] |= T1_MASK_FINAL;
    (this->*hash_func)(Work, 1, used_bytes, State);

    output_block_counter = 0;
}

void Skein::Generate(void *out, int bytes)
{
    // Put the Skein generator in counter mode and generate WORDS at a time
    u64 NextState[MAX_WORDS];
    u64 FinalMessage[MAX_WORDS] = {output_block_counter, 0};
    u64 *out64 = (u64 *)out;

    // In output mode, we hide the first block of each request
    if (output_prng_mode)
    {
        // T1 = FIRST | FINAL | OUT
        Tweak[0] = 0;
        Tweak[1] = T1_MASK_FIRST | T1_MASK_FINAL | ((u64)BLK_TYPE_OUT << T1_POS_BLK_TYPE);

        // Produce next output
        (this->*hash_func)(FinalMessage, 1, 8, NextState);

        // Next counter
        FinalMessage[0] = ++output_block_counter;
    }

    while (bytes >= digest_bytes)
    {
        // T1 = FIRST | FINAL | OUT
        Tweak[0] = 0;
        Tweak[1] = T1_MASK_FIRST | T1_MASK_FINAL | ((u64)BLK_TYPE_OUT << T1_POS_BLK_TYPE);

        // Produce next output
        (this->*hash_func)(FinalMessage, 1, 8, out64);

        for (int ii = 0; ii < digest_words; ++ii)
            swapLE(out64[ii]);

        // Next counter
        out64 += digest_words;
        bytes -= digest_bytes;
        FinalMessage[0] = ++output_block_counter;
    }

    if (bytes > 0)
    {
        // T1 = FIRST | FINAL | OUT
        Tweak[0] = 0;
        Tweak[1] = T1_MASK_FIRST | T1_MASK_FINAL | ((u64)BLK_TYPE_OUT << T1_POS_BLK_TYPE);

        // Produce final output
        (this->*hash_func)(FinalMessage, 1, 8, FinalMessage);

        for (int ii = CAT_CEIL_UNIT(bytes, sizeof(u64)); ii >= 0; --ii)
            swapLE(FinalMessage[ii]);

        // Copy however many bytes they wanted
        memcpy(out64, FinalMessage, bytes);

        ++output_block_counter;
    }

    // In PRNG output mode, use the first hidden output block as the next state
    if (output_prng_mode)
        memcpy(State, NextState, digest_bytes);
}
