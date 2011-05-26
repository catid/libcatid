/*
	Copyright (c) 2009-2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_SPHYNX_LAYER_HPP
#define CAT_SPHYNX_LAYER_HPP

#include <cat/sphynx/Common.hpp>
#include <cat/io/IOLayer.hpp>
#include <cat/math/BigTwistedEdwards.hpp>
#include <cat/crypt/rand/Fortuna.hpp>
#include <cat/net/DNSClient.hpp>

namespace cat {


class SphynxTLS;
class SphynxLayer;


// Sphynx thread-local storage
class CAT_EXPORT SphynxTLS : public IWorkerTLS
{
public:
	static const u32 DELIVERY_QUEUE_DEPTH = 128;

	// Used internally by the Sphynx Transport layer to queue up messages for delivery
	sphynx::IncomingMessage delivery_queue[DELIVERY_QUEUE_DEPTH];
	u32 delivery_queue_depth;
	u8 *free_list[DELIVERY_QUEUE_DEPTH];
	u32 free_list_count;

	// A cryptographically secure random number generator, if you need random numbers
	FortunaOutput *csprng;

	// Cryptographic math primitive
	BigTwistedEdwards *math;

	SphynxTLS();
	virtual ~SphynxTLS();

	bool Valid();
};

// Application layer for Sphynx library
class CAT_EXPORT SphynxLayer : public IOLayer
{
public:
	CAT_INLINE virtual ~SphynxLayer() {}

	static SphynxLayer *ref();

	bool Startup(const char *settings_file_name = "Settings.cfg", bool service = false, const char *service_name = "MyService");

protected:
	virtual bool PreWorkerThreads();
	virtual bool OnStartup(IWorkerTLSBuilder *tls, const char *settings_file_name, bool service, const char *service_name);
	virtual void OnShutdown(bool watched_shutdown);
};


} // namespace cat

#endif // CAT_SPHYNX_LAYER_HPP
