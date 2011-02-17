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
	_bandwidth_low_limit = 10000;
	_bandwidth_high_limit = 100000000;
	_bps = _bandwidth_low_limit;

	_loss_timeout = 1500;

	_last_bw_update = 0;
	_available_bw = 0;

	_stats_ack_ii = 0;
}

s32 FlowControl::GetRemainingBytes()
{
	_lock.Enter();

	u32 now = Clock::msec();

	u32 elapsed = now - _last_bw_update;
	_last_bw_update = now;

	// Need to use 64-bit here because this number can exceed 4 MB
	u32 bytes = (u32)(((u64)elapsed * _bps) / 1000);

	u32 available = _available_bw + bytes;

	// If available is more than we would want to send in a tick,
	u32 bytes_per_tick_max = _bps * Transport::TICK_INTERVAL / 1000;
	if (available > bytes_per_tick_max)
		available = bytes_per_tick_max;

	_available_bw = available;

	_lock.Leave();

	return available;
}

void FlowControl::OnPacketSend(u32 bytes_with_overhead)
{
	_lock.Enter();

	_available_bw -= bytes_with_overhead;

	_lock.Leave();
}

void FlowControl::OnTick(u32 now, u32 timeout_loss_count)
{
}

void FlowControl::OnACK(u32 now, u32 avg_one_way_time, u32 nack_loss_count)
{
	_stats_trip[_stats_ack_ii] = avg_one_way_time;
	_stats_nack[_stats_ack_ii] = nack_loss_count;
	_stats_ack_ii++;

	if (_stats_ack_ii == IIMAX)
	{
		u32 avg_trip, min_trip, max_trip, nack_count = 0;
		avg_trip = min_trip = max_trip = _stats_trip[0];

		for (int ii = 1; ii < IIMAX; ++ii)
		{
			u32 trip = _stats_trip[ii];
			avg_trip += trip;
			if (min_trip > trip) min_trip = trip;
			if (max_trip < trip) max_trip = trip;
			nack_count += _stats_nack[ii];
		}

		avg_trip /= IIMAX;
		FATAL("FlowControl") << "AvgTrip=" << avg_trip << " MinTrip=" << min_trip << " MaxTrip=" << max_trip << " NACK=" << nack_count;
		_stats_ack_ii = 0;
	}

	if (avg_one_way_time > 300)
	{
		FATAL("FlowControl") << "Halving transmit rate since one way time shot up to " << avg_one_way_time;

		_bps /= 2;
		if (_bps < (s32)_bandwidth_low_limit)
			_bps = (s32)_bandwidth_low_limit;
	}
}
