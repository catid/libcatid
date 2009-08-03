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
    switch (bits)
    {
    case 256: return new BigTwistedEdward(ECC_REG_OVERHEAD, 256, EDWARD_C_256, EDWARD_D_256);
    case 384: return new BigTwistedEdward(ECC_REG_OVERHEAD, 384, EDWARD_C_384, EDWARD_D_384);
    case 512: return new BigTwistedEdward(ECC_REG_OVERHEAD, 512, EDWARD_C_512, EDWARD_D_512);
    default:  return 0;
    }
}
