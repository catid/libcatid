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

#include <cat/sphynx/TLS.hpp>
using namespace cat;
using namespace sphynx;

static CAT_TLS TLS *m_tls = 0;


//// TLS

TLS *TLS::ref()
{
	TLS *tls = m_tls;

	// If instance is not set,
	if (!tls)
	{
		// Create an instance
		tls = new TLS;
		if (!tls) return 0;

		// Validate it
		if (!tls->Valid())
		{
			delete tls;
			return 0;
		}

		// Store it as the global copy
		m_tls = tls;
		return tls;
	}

	// Increment reference count
	tls->_ref_count++;

	return tls;
}

TLS::TLS()
{
	_ref_count = 0;
	_math = 0;
	_csprng = 0;

	if (Initialize())
		_ref_count = 1;
}

TLS::~TLS()
{
	Finalize();
}

void TLS::RemoveRef()
{
	// If no more references remain,
	if (--_ref_count <= 0)
	{
		m_tls = 0;

		delete this;
	}
}

bool TLS::Initialize()
{
	Finalize();

	_csprng = FortunaFactory::ref()->Create();
	if (!_csprng)
	{
		CAT_FATAL("TLS") << "Unable to get a valid CSPRNG";
		return false;
	}

	_math = KeyAgreementCommon::InstantiateMath(256);
	if (!_math || !_math->Valid())
	{
		CAT_FATAL("TLS") << "Unable to get a valid math object";
		return false;
	}

	return true;
}

void TLS::Finalize()
{
	if (_csprng)
	{
		delete _csprng;
		_csprng = 0;
	}

	if (_math)
	{
		delete _math;
		_math = 0;
	}
}
