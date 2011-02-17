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

#ifndef CAT_IO_LAYER_HPP
#define CAT_IO_LAYER_HPP

#include <cat/CommonLayer.hpp>
#include <cat/net/Sockets.hpp>

#if defined(CAT_OS_WINDOWS)
#include <cat/iocp/IOThreads.hpp>
#include <cat/iocp/UDPEndpoint.hpp>
#else
TODO
#endif

namespace cat {


class IOLayer : public CommonLayer
{
	IOThreads _io_threads;

public:
	CAT_INLINE virtual ~IOLayer() {}

	CAT_INLINE IOThreads *GetIOThreads() { return &_io_threads; }

protected:
	virtual bool OnStartup(u32 worker_tick_interval, IWorkerTLSBuilder *tls, const char *settings_file_name, bool service, const char *service_name);
	virtual void OnShutdown(bool watched_shutdown);
};


} // namespace cat

#endif // CAT_IO_LAYER_HPP
