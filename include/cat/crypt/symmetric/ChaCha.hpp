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

// 06/15/09 now supports variable key length up to 384 bits
// 06/13/09 added ChaCha::ReconstructIV
// 06/11/09 part of libcat-1.0

/*
	The ChaCha cipher is a symmetric stream cipher based on Salsa20.
	Author website: http://cr.yp.to/chacha.html
*/

#ifndef CAT_CHACHA_HPP
#define CAT_CHACHA_HPP

#include <cat/Platform.hpp>

namespace cat {

/*
	To initialize the ChaCha cipher, you must specify a 256-bit key.

	Code example:

		ChaCha cc;
		char key[32]; // fill key here

		cc.Key(key);

	Before each encryption or decryption with the ChaCha cipher,
	a 64-bit Initialization Vector (IV) must be specified.  Every
	time a message is encrypted, the IV must be incremented by 1.
	The IV is then sent along with the encrypted message.

	Encryption code example:

		char message[19], ciphertext[19]; // message filled here
		u64 iv = 125125;
		u64 message_iv = iv;
		iv = iv + 1;

		cc.Begin(message_iv);
		cc.Crypt(message, ciphertext, sizeof(ciphertext));

	Decryption code example:

		char ciphertext[19], decrypted[19]; // ciphertext filled here

		cc.Begin(message_iv);
		cc.Crypt(ciphertext, decrypted, sizeof(decrypted));

	Sending all 8 bytes of the IV in every packet is not necessary.
	Instead, only a few of the low bits of the IV need to be sent,
	if the IV is incremented by 1 each time.

	How many?  It depends on how many messages can get lost.
	If < 32768 messages can get lost in a row, then CAT_IV_BITS = 16 (default)

	I have provided a function to handle rollover/rollunder of the IV,
	which also works if the same IV is sent twice for some reason.
	It needs to know how many of the low bits are sent across, so be sure
	to change CAT_IV_BITS in this header if you send more or less than 16.

	Code example:

		u64 last_accepted_iv;
		u32 new_iv_low_bits;

		u64 new_iv = ChaCha::ReconstructIV(last_accepted_iv, new_iv_low_bits);

	-------------------------READ THIS BEFORE USING--------------------------

	Never use the same IV twice.
		Otherwise: An attacker can recover the plaintext without the key.

	Never use the same key twice.
		Otherwise: An attacker can recover the plaintext without the key.

	If you have two hosts talking to eachother securely with ChaCha encryption,
	then be sure that each host is encrypting with a DIFFERENT key.
		Otherwise: An attacker can recover the plaintext without the key.

	Remember that an attacker can impersonate the remote computer, so be
	sure not to accept the new IV until the message hash has been verified
	if your protocol has a hash on each message.
		Otherwise: An attacker could desynchronize the IVs.
*/


//// ChaCha cipher

class ChaCha
{
	u32 state[16];

	void GenerateKeyStream(u32 *out);

public:
	~ChaCha();

	// Key up to 384 bits
	void Key(const void *key, int bytes);

	// 64-bit iv
	void Begin(u64 iv);

	// Message with any number of bytes
	void Crypt(const void *in, void *out, int bytes);
};


} // namespace cat

#endif // CAT_CHACHA_HPP
