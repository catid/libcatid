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

#include <cat/io/IOLayer.hpp>
using namespace cat;

bool IOLayer::OnStartup(IWorkerTLSBuilder *tls_builder, const char *settings_file_name, bool service, const char *service_name)
{
	if (!CommonLayer::OnStartup(tls_builder, settings_file_name, service, service_name))
		return false;

	// Start the CSPRNG subsystem
	if (!FortunaFactory::ref()->Initialize())
	{
		FATAL("IOLayer") << "CSPRNG subsystem failed to initialize";
		return false;
	}

	// Start the socket subsystem
	if (!StartupSockets())
	{
		FATAL("IOLayer") << "Socket subsystem failed to initialize";
		return false;
	}

	// Start the IO threads
	if (!_io_threads.Startup())
	{
		FATAL("IOLayer") << "IOThreads subsystem failed to initialize";
		return false;
	}

	// Start the Worker threads
	if (!_worker_threads.Startup(tls_builder))
	{
		FATAL("IOLayer") << "WorkerThreads subsystem failed to initialize";
		return false;
	}

	return true;
}

void IOLayer::OnShutdown()
{
	if (!_watcher.WaitForShutdown())
	{
		WARN("ChatClient") << "Wait for shutdown expired";
	}

	// Terminate Worker threads
	_worker_threads.Shutdown();

	// Terminate IO threads
	_io_threads.Shutdown();

	// Terminate sockets
	CleanupSockets();

	// Terminate the entropy collection thread in the CSPRNG
	FortunaFactory::ref()->Shutdown();

	// Write settings to disk
	Settings::ref()->write();

	// Cleanup clock subsystem
	Clock::Shutdown();
}
