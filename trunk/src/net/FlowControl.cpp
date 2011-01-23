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

#include <cat/net/FlowControl.hpp>
#include <cat/time/Clock.hpp>
#include <cat/io/Logging.hpp>
#include <cat/net/SphynxTransport.hpp>
using namespace cat;
using namespace sphynx;

FlowControl::FlowControl()
{
	_max_epoch_bytes = MIN_RATE_LIMIT / 2;

	_send_epoch_bytes = 0;

	_next_epoch_time = Clock::msec();
}

void FlowControl::OnTick(u32 now)
{
	// If epoch has ended,
	if ((s32)(now - _next_epoch_time) >= 0)
	{
		INFO("FlowControl") << "_send_epoch_bytes = " << _send_epoch_bytes;

		// If some bandwidth has been used this epoch,
		if ((s32)_send_epoch_bytes > 0)
		{
			// Subtract off the amount allowed this epoch
			Atomic::Add(&_send_epoch_bytes, -_max_epoch_bytes);
		}

		// Set next epoch time
		_next_epoch_time += EPOCH_INTERVAL;

		// If within one tick of another epoch,
		if ((s32)(now - _next_epoch_time + sphynx::Transport::TICK_INTERVAL) > 0)
		{
			WARN("FlowControl") << "Slow epoch - Scheduling next epoch one interval into the future";

			// Lagged too much - reset epoch interval
			_next_epoch_time = now + EPOCH_INTERVAL;
		}
	}
}

void FlowControl::OnLoss(u32 bytes)
{

}
