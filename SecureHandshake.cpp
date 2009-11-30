/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

/*
	To quickly include LibCat's security code in your project:

	1. Add SecureHandshake.cpp to your project's source code.
	2. Copy the "cat" directory from "./include/cat" to your source code.
	3. Copy the "src" directory from "./src" under the "cat" directory.
	4. Copy the "lib" directory from "./lib" under the "cat" directory.

	(Optional) For 64-bit targets:
	5. Statically link to cat/lib/cat/big_x64.o (for Windows use .obj)

	So your source code directory structure should look like this:

	MyProject/SourceFolder/cat/ <- contains contents of ./include/cat
	MyProject/SourceFolder/cat/src <- contains contents of ./src
	MyProject/SourceFolder/cat/lib <- contains contents of ./lib

	See <cat/crypt/tunnel/EasyHandshake.hpp> for example usage.
*/

// Add your conditional compilation here in case you don't want to build
// LibCat's security code for some of the platforms that you target.
#define LIBCAT_SECURITY

#if defined(LIBCAT_SECURITY)

#include "cat/src/port/EndianNeutral.cpp"
#include "cat/src/port/AlignedAlloc.cpp"
#include "cat/src/time/Clock.cpp"
#include "cat/src/threads/Mutex.cpp"
#include "cat/src/threads/LoopThread.cpp"

#include "cat/src/math/BigRTL.cpp"
#include "cat/src/math/BigPseudoMersenne.cpp"
#include "cat/src/math/BigTwistedEdwards.cpp"

#include "cat/src/crypt/SecureCompare.cpp"
#include "cat/src/crypt/cookie/CookieJar.cpp"
#include "cat/src/crypt/hash/HMAC_MD5.cpp"
#include "cat/src/crypt/privatekey/ChaCha.cpp"
#include "cat/src/crypt/hash/Skein.cpp"
#include "cat/src/crypt/hash/Skein256.cpp"
#include "cat/src/crypt/hash/Skein512.cpp"

#include "cat/src/crypt/rand/EntropyWindows.cpp"
#include "cat/src/crypt/rand/EntropyLinux.cpp"
#include "cat/src/crypt/rand/EntropyWindowsCE.cpp"
#include "cat/src/crypt/rand/EntropyGeneric.cpp"
#include "cat/src/crypt/rand/Fortuna.cpp"

#include "cat/src/crypt/tunnel/KeyAgreement.cpp"
#include "cat/src/crypt/tunnel/AuthenticatedEncryption.cpp"
#include "cat/src/crypt/tunnel/KeyAgreementInitiator.cpp"
#include "cat/src/crypt/tunnel/KeyAgreementResponder.cpp"
#include "cat/src/crypt/tunnel/KeyMaker.cpp"

#include "cat/src/crypt/tunnel/EasyHandshake.cpp"

#endif // LIBCAT_SECURITY
