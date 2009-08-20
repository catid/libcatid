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

BigTwistedEdward *KeyAgreementCommon::InstantiateMath(int bits)
{
	const char *EDWARD_Q_256 = "28948022309329048855892746252171976963461314589887294264891545010474297951221";
	const char *EDWARD_Q_384 = "9850501549098619803069760025035903451269934817616361666989904550033111091443156013902778362733054842505505091826087";
	const char *EDWARD_Q_512 = "3351951982485649274893506249551461531869841455148098344430890360930441007518428505811628503485002357654150053918764604945126948031916092776575822477898019";

	switch (bits)
    {
    case 256: return new BigTwistedEdward(ECC_REG_OVERHEAD, 256, EDWARD_C_256, EDWARD_D_256, EDWARD_Q_256);
    case 384: return new BigTwistedEdward(ECC_REG_OVERHEAD, 384, EDWARD_C_384, EDWARD_D_384, EDWARD_Q_384);
    case 512: return new BigTwistedEdward(ECC_REG_OVERHEAD, 512, EDWARD_C_512, EDWARD_D_512, EDWARD_Q_512);
    default:  return 0;
    }
}
