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

// Include all libcat Framework headers

#ifndef ALL_FRAMEWORK_HPP
#define ALL_FRAMEWORK_HPP

#include <cat/AllCommon.hpp>
#include <cat/AllMath.hpp>
#include <cat/AllCrypt.hpp>
#include <cat/AllCodec.hpp>
#include <cat/AllTunnel.hpp>
#include <cat/AllGraphics.hpp>

#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <cat/io/Settings.hpp>
#include <cat/io/ThreadPoolFiles.hpp>

#include <cat/net/ThreadPoolSockets.hpp>

#include <cat/parse/BitStream.hpp>
#include <cat/parse/BufferTok.hpp>
#include <cat/parse/MessageRouter.hpp>

#include <cat/threads/ThreadPool.hpp>
#include <cat/threads/LocklessFIFO.hpp>
#include <cat/threads/Mutex.hpp>
#include <cat/threads/RegionAllocator.hpp>

namespace cat {


void InitializeFramework();

void ShutdownFramework(bool WriteSettings = true);


} // namespace cat

#endif // ALL_FRAMEWORK_HPP
