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

#include <cat/sphynx/FlowControl.hpp>
#include <cat/time/Clock.hpp>
#include <cat/io/Logging.hpp>
#include <cat/sphynx/Transport.hpp>
using namespace cat;
using namespace sphynx;

FlowControl::FlowControl()
{
	_last_epoch_bytes = 0;
	_send_epoch_bytes = 0;

	_next_epoch_time = Clock::msec();

	_stats_ack_ii = 0;

	_bandwidth_low_limit = 10000;
	_bandwidth_high_limit = 100000;

	_max_epoch_bytes = _bandwidth_low_limit / 2;
}

void FlowControl::OnTick(u32 now, u32 timeout_loss_count)
{
	if (timeout_loss_count)
	{
		FATAL("FlowControl") << "Timeout loss count: " << timeout_loss_count;
	}

	// If epoch has ended,
	if ((s32)(now - _next_epoch_time) >= 0)
	{
		// If some bandwidth has been used this epoch,
		if ((s32)_send_epoch_bytes > 0)
		{
			FATAL("FlowControl") << "_send_epoch_bytes = " << (s32)_send_epoch_bytes - _last_epoch_bytes;

			// Subtract off the amount allowed this epoch
			_last_epoch_bytes = Atomic::Add(&_send_epoch_bytes, -_max_epoch_bytes);
		}

		// Set next epoch time
		_next_epoch_time += EPOCH_INTERVAL;

		// If within one tick of another epoch,
		if ((s32)(now - _next_epoch_time + Transport::TICK_INTERVAL) > 0)
		{
			FATAL("FlowControl") << "Slow epoch - Scheduling next epoch one interval into the future";

			// Lagged too much - reset epoch interval
			_next_epoch_time = now + EPOCH_INTERVAL;
		}
	}

	_max_epoch_bytes += 1000;
	if (_max_epoch_bytes > (s32)_bandwidth_high_limit)
		_max_epoch_bytes = (s32)_bandwidth_high_limit;
}

void FlowControl::OnACK(u32 now, u32 avg_one_way_time, u32 nack_loss_count)
{
	/*
	_stats_trip[_stats_ack_ii] = avg_one_way_time;
	_stats_nack[_stats_ack_ii] = nack_loss_count;
	_stats_ack_ii++;

	if (_stats_ack_ii == IIMAX)
	{
		for (int ii = 0; ii < IIMAX; ++ii)
		{
			FATAL("FlowControl") << "AvgTrip=" << _stats_trip[ii] << " NACK=" << _stats_nack[ii];
		}
		_stats_ack_ii = 0;
	}*/

	if (avg_one_way_time > 300)
	{
		FATAL("FlowControl") << "Halving transmit rate since one way time shot up to " << avg_one_way_time;
		_max_epoch_bytes /= 2;
		if (_max_epoch_bytes < (s32)_bandwidth_low_limit / 2)
			_max_epoch_bytes = (s32)_bandwidth_low_limit / 2;
	}
}
