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

// 07/18/2009 began

#ifndef CAT_KEY_MAKER_HPP
#define CAT_KEY_MAKER_HPP

#include <cat/crypt/tunnel/KeyAgreement.hpp>
#include <cat/crypt/rand/Fortuna.hpp>

namespace cat {


class KeyMaker : public KeyAgreementCommon
{
public:
    bool GenerateKeyPair(BigTwistedEdward *math, FortunaOutput *csprng, u8 *public_key, int public_bytes, u8 *private_key, int private_bytes);
};


} // namespace cat

#endif // CAT_KEY_MAKER_HPP
