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

#ifndef CAT_FLOW_CONTROL_HPP
#define CAT_FLOW_CONTROL_HPP

#include <cat/sphynx/Common.hpp>
#include <cat/threads/Atomic.hpp>

namespace cat {


namespace sphynx {


/*
    Approach inspired by TCP Adaptive Westwood from
    Marcondes-Sanadidi-Gerla-Shimonishi paper "TCP Adaptive Westwood" (ICC 2008)
*/

/*
    "Tampon" Flow Control algorithm:

	Tampon is a TCP-Reno Friendly approach to flow control.  It throttles the
	rate of a flow to attempt to maximize throughput and friendliness with other
	network flows.

	Tampon is designed for online game flows.  Features of these flows:
		+ Many types of message delivery and multiple streams
		+ Most messages are not part of bulk file transfers
		+ Low tolerance for packetloss and latency
		+ Bandwidth requirements burst and wane unexpectedly

	Tampon is built to be integrated with the Sphynx transport layer, which
	wakes up on the following events:
		+ On message send request from another thread : Asynchronous sending
		+ On datagram arrival : Processing incoming data and may transmit
		+ Timer : Retransmission and message blobbing

	Sphynx supports reliable messaging with selective acknowledgments (SACK).
	This implies support for negative acknowledgment (NACK) as well.  So, it is
	possible to measure the rate of packetloss (PL).

	Sphynx tags each packet it sends with a timestamp and it synchronizes clocks
	between each endpoint.  So, it is possible to measure the one-way trip time
	of each message (TT).

	Tampon attempts to correlate bandwidth-used to PL and TT.  Within a sample
	window, it will gather statistics and make predictions about the channel
	capacity.  In periods of nominal loss, it will rely on past data.  When loss
	events occur more often than expected, it will adjust its channel capacity
	estimates to react swiftly to loss events.

	Tampon has three phases:
		+ Slow Start
			- Collecting ambient PL and TT of network until first loss event
		+ Steady State
			- Congestion avoidance based on PL and TT
		+ Congestion Reaction
			- Cuts channel capacity estimation down to a perceived safe level
*/

// TODO: Check for thread safety

class CAT_EXPORT FlowControl
{
public:
	static const int EPOCH_INTERVAL = 500; // Milliseconds per epoch

protected:
	Mutex _lock;

	// BPS low and high limits
	u32 _bandwidth_low_limit, _bandwidth_high_limit;

	// Current BPS limit
	s32 _bps;

	s32 _available_bw;
	u32 _last_bw_update;

	// Milliseconds without receiving acknowledgment that a message will be considered lost
	u32 _loss_timeout;

	static const int IIMAX = 20;
	u32 _stats_trip[IIMAX];
	u32 _stats_nack[IIMAX];
	u32 _stats_ack_ii;

public:
	FlowControl();

	CAT_INLINE u32 GetBandwidthLowLimit() { return _bandwidth_low_limit; }
	CAT_INLINE void SetBandwidthLowLimit(u32 limit) { _bandwidth_low_limit = limit; }
	CAT_INLINE u32 GetBandwidthHighLimit() { return _bandwidth_high_limit; }
	CAT_INLINE void SetBandwidthHighLimit(u32 limit) { _bandwidth_high_limit = limit; }

	// The whole purpose of this class is to calculate this value
	s32 GetRemainingBytes(u32 now);

	// Report number of bytes for each successfully sent packet, including overhead bytes
	void OnPacketSend(u32 bytes_with_overhead);

	// Get timeout for reliable message delivery before considering it lost
	CAT_INLINE u32 GetLossTimeout() { return _loss_timeout; }

	// Called when a transport layer tick occurs
	void OnTick(u32 now, u32 timeout_loss_count);

	// Called when an acknowledgment is received
	void OnACK(u32 now, SendQueue *node);
	void OnACKDone(u32 now, u32 avg_one_way_time, u32 nack_loss_count, u32 data_bytes);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_FLOW_CONTROL_HPP
