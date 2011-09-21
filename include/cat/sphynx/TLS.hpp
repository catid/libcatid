/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_TLS_HPP
#define CAT_TLS_HPP

#include <cat/math/BigTwistedEdwards.hpp>
#include <cat/crypt/rand/Fortuna.hpp>

namespace cat {

namespace sphynx {


// Thread-local-storage (TLS) for Sphynx
class TLS
{
	BigTwistedEdwards *_math;
	FortunaOutput *_csprng;
	int _ref_count;

	// Returns true if initialization succeeds
	bool Initialize();
	void Finalize();

public:
	TLS();
	~TLS();

	CAT_INLINE bool Valid() { return _ref_count > 0; }
	CAT_INLINE BigTwistedEdwards *Math() { return _math; }
	CAT_INLINE FortunaOutput *CSPRNG() { return _csprng; }

	// Use these two functions together to manage TLS
	// in actual thread local storage instead of on the heap
	static TLS *ref();
	void RemoveRef();
};


// Automatically deref when AutoTLS goes out of scope
class AutoTLS
{
	TLS *_tls;

public:
	CAT_INLINE AutoTLS()
	{
		_tls = TLS::ref();
	}
	CAT_INLINE ~AutoTLS()
	{
		if (_tls) _tls->RemoveRef();
	}

	CAT_INLINE bool Valid() { return _tls != 0; }

	CAT_INLINE TLS *operator->() { return _tls; }
};


} // namespace sphynx

} // namespace cat

#endif // CAT_TLS_HPP
