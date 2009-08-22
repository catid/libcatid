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

#include <cat/crypt/tunnel/KeyAgreement.hpp>
using namespace cat;

bool KeyAgreementCommon::Initialize(int bits)
{
    // Restrict the bits to pre-defined security levels
    switch (bits)
    {
    case 256:
    case 384:
    case 512:
        KeyBits = bits;
        KeyBytes = KeyBits / 8;
        KeyLegs = KeyBytes / sizeof(Leg);
        return true;
    }

    return false;
}

static u8 Q_256[32] = {
	245,131,113,179,240,8,8,95,168,93,210,180,187,107,50,108,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,64
};

static u8 GeneratorPoint_256[64] = {
	199,172,100,86,21,9,105,52,27,54,112,27,130,66,212,206,2,201,
	233,157,146,53,115,139,157,11,140,127,85,208,200,234,89,166,
	146,6,210,52,127,185,80,155,102,54,140,112,165,133,28,79,155,
	87,57,23,8,77,36,126,56,208,44,204,44,122
};

static u8 Q_384[48] = {
	167,49,135,1,253,184,43,116,11,231,6,199,63,186,43,6,212,2,
	195,225,178,24,119,115,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,64
};

static u8 GeneratorPoint_384[96] = {
	194,86,91,5,46,236,168,80,129,111,223,121,77,185,26,167,98,57,
	177,25,134,193,90,222,174,244,78,5,90,166,102,139,157,79,136,
	152,90,103,195,102,213,108,231,170,27,180,46,139,122,198,200,
	43,174,187,240,150,104,198,109,121,27,97,12,24,247,39,173,231,
	37,229,140,166,121,97,65,16,195,24,98,137,210,165,181,166,216,
	60,210,21,169,22,2,184,123,239,159,170
};

static u8 Q_512[64] = {
	7,136,153,241,166,33,123,142,62,77,254,231,156,219,24,171,220,
	146,88,148,11,12,153,176,182,120,137,227,1,235,197,30,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,64
};

static u8 GeneratorPoint_512[128] = {
	34,181,62,219,167,17,152,185,106,113,24,141,78,124,179,108,16,
	48,126,37,104,196,116,5,113,214,124,250,203,234,112,49,212,165,
	39,68,243,53,190,108,48,157,70,80,175,7,192,46,248,115,204,239,
	167,212,174,129,140,89,190,85,8,34,104,88,250,79,0,135,145,37,
	62,237,70,162,67,180,82,32,36,152,68,69,190,222,107,234,15,25,
	163,135,191,127,173,153,143,102,64,84,233,112,34,9,153,176,215,
	157,50,59,31,184,235,134,116,241,238,177,3,109,59,251,125,45,
	228,99,172,83,95,26
};

BigTwistedEdward *KeyAgreementCommon::InstantiateMath(int bits)
{
	switch (bits)
    {
    case 256: return new BigTwistedEdward(ECC_REG_OVERHEAD, 256, EDWARD_C_256, EDWARD_D_256, Q_256, GeneratorPoint_256);
    case 384: return new BigTwistedEdward(ECC_REG_OVERHEAD, 384, EDWARD_C_384, EDWARD_D_384, Q_384, GeneratorPoint_384);
    case 512: return new BigTwistedEdward(ECC_REG_OVERHEAD, 512, EDWARD_C_512, EDWARD_D_512, Q_512, GeneratorPoint_512);
    default:  return 0;
    }
}
