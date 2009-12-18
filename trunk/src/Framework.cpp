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

#include <cat/AllFramework.hpp>
using namespace cat;

// Framework Initialize
bool cat::InitializeFramework()
{
	// Initialize clock subsystem
	if (!Clock::Initialize())
	{
		FatalStop("Clock subsystem failed to initialize");
	}

	// Initialize custom memory allocator subsystem
	if (!RegionAllocator::ref()->Valid())
	{
		FatalStop("Custom memory allocator failed to initialize");
	}

	// Initialize logging subsystem with INFO reporting level
	Logging::ref()->Initialize(LVL_INFO);

	// Initialize disk settings subsystem
	Settings::ref()->read();

	// Read logging subsystem settings
	Logging::ref()->ReadSettings();

	// Start the CS PRNG subsystem
	if (!FortunaFactory::ref()->Initialize())
	{
		FATAL("Framework") << "Unable to initialize the CSPRNG";
		ShutdownFramework(false);
		return false;
	}

	// Start the socket subsystem
	if (!StartupSockets())
	{
		FATAL("Framework") << "Unable to initialize Sockets: " << SocketGetLastErrorString();
		ShutdownFramework(false);
		return false;
	}

	// Start the worker threads
	if (!ThreadPool::ref()->Startup())
	{
		FATAL("Framework") << "Unable to initialize the ThreadPool";
		ShutdownFramework(false);
		return false;
	}

	return true;
}

// Framework Shutdown
void cat::ShutdownFramework(bool WriteSettings)
{
	// Terminate worker threads
	ThreadPool::ref()->Shutdown();

	// Terminate sockets
	CleanupSockets();

	// Terminate the entropy collection thread in the CSPRNG
	FortunaFactory::ref()->Shutdown();

	// Write settings to disk if requested
	if (WriteSettings)
		Settings::ref()->write();

	Clock::Shutdown();
}
