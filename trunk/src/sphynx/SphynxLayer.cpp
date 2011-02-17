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

#include <cat/sphynx/SphynxLayer.hpp>
#include <cat/crypt/rand/Fortuna.hpp>
#include <cat/sphynx/Transport.hpp>
using namespace cat;

SphynxTLS::SphynxTLS()
{
	csprng = new FortunaOutput;

	math = KeyAgreementCommon::InstantiateMath(256);
}

SphynxTLS::~SphynxTLS()
{
	if (csprng) delete csprng;

	if (math) delete math;
}

bool SphynxTLS::Valid()
{
	return csprng && math;
}


//// SphynxLayer

bool SphynxLayer::PreWorkerThreads()
{
	// Start the CSPRNG subsystem
	if (!FortunaFactory::ref()->Initialize())
	{
		FATAL("SphynxLayer") << "CSPRNG subsystem failed to initialize";
		return false;
	}

	return true;
}

bool SphynxLayer::Startup(const char *settings_file_name, bool service, const char *service_name)
{
	return CommonLayer::Startup<SphynxTLS>(sphynx::Transport::TICK_INTERVAL, settings_file_name, service, service_name);
}

bool SphynxLayer::OnStartup(u32 worker_tick_interval, IWorkerTLSBuilder *tls_builder, const char *settings_file_name, bool service, const char *service_name)
{
	if (!IOLayer::OnStartup(worker_tick_interval, tls_builder, settings_file_name, service, service_name))
		return false;

	_dns_client = new DNSClient;

	if (!_dns_client)
	{
		FATAL("IOLayer") << "DNS subsystem failed to initialize";
		return false;
	}

	return true;
}

void SphynxLayer::OnShutdown(bool watched_shutdown)
{
	// Terminate the entropy collection thread in the CSPRNG
	FortunaFactory::ref()->Shutdown();

	IOLayer::OnShutdown(watched_shutdown);
}
