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
    "Siamese" Flow Control algorithm:

	Siamese is a TCP-Reno Friendly approach to flow control.  It throttles the
	rate of a flow to attempt to maximize throughput and friendliness with other
	network flows.

	Siamese is designed for online game flows.  Features of these flows:
		+ Many types of message delivery and multiple streams
		+ Most messages are not part of bulk file transfers
		+ Low tolerance for packetloss
		+ Bandwidth requirements burst and wane unexpectedly

	Siamese is built to be integrated with the Sphynx transport layer, which
	wakes up on the following events:
		+ On message send request from another thread : Asynchronous sending
		+ On datagram arrival : Processing incoming data and may transmit
		+ 20 millisecond timer : Retransmission and Message Blobbing

	Sphynx supports reliable messaging with selective acknowledgments (SACK).
	This implies support for negative acknowledgment (NACK) as well.  So, it is
	possible to measure the rate of packetloss (PL).

	Sphynx tags each packet it sends with a timestamp and it synchronizes clocks
	between each endpoint.  So, it is possible to measure the one-way trip time
	of each message (TT).

	Siamese attempts to correlate bandwidth-used to PL and TT.  Within a sample
	window, it will gather statistics and make predictions about the channel
	capacity.  In periods of nominal loss, it will rely on past data.  When loss
	events occur more often than expected, it will adjust its channel capacity
	estimates to react swiftly to loss events.

	Siamese has three phases:
		+ Slow Start
			- Collecting ambient PL and TT of network until first loss event
		+ Steady State
			- Congestion avoidance based on PL and TT
		+ Congestion Reaction
			- Cuts channel capacity estimation down to a perceived safe level
*/

/*
	How Rate Limiting Works

	Going to do rate limiting a little differently:

	A count of bytes recently sent is kept in a volatile 32-bit number.

	Whenever a PostPacket() call succeeds, it will increase the count of bytes
	by the packet size + estimated IP4/6 and UDP header sizes.  This addition is
	done atomically so no locking needs to be done.

	A thread will wake up every 20 ms, and if it has been 500 ms since the last
	time it has done so, and the count of bytes is positive, it will subtract
	off the rate limit over two (10KBPS = -5,000).  This is done atomically
	also, and may make the number go negative.  The negative allows the data
	flow to "burst" up and maintain the same rate if the rate of data flow is
	not constant.  I think this is good for games, since events are bursty.

	I chose 500 ms with a goal in mind.  For a data rate of about 10 KBPS, that
	would reduce by 5k every 500 ms, which allows for a burst of about 4 MSS
	sized messages to be transmitted.  That would allow 4 acks to be combined
	into one after the burst, which is about my target for combining acks.
	
	It just feels like the right amount, but I bet there would be a way to prove
	the "correct" number is somewhere around this value.

	Not all data is rate limited.  The following are not rate limited:
	Internal messages (out of band)
	Unreliable messages (probably contains data that cannot be delayed)

	The following ARE rate limited:
	Unordered, stream 1, 2 and 3 (these always contain less important data)
*/

// TODO: Check for thread safety

class FlowControl
{
public:
	static const int EPOCH_INTERVAL = 500; // Milliseconds per epoch

	static const u32 MIN_RATE_LIMIT = 100000; // Smallest data rate allowed

protected:
	// Bytes per epoch maximum (0 = none)
	s32 _max_epoch_bytes;

	// Milliseconds without receiving acknowledgment that a message will be considered lost
	u32 _loss_timeout;

	// Time when next epoch will start
	u32 _next_epoch_time;

	// Number of bytes sent during the current epoch, atomically synchronized
	volatile u32 _send_epoch_bytes;

public:
	FlowControl();

	// The whole purpose of this class is to calculate this value
	CAT_INLINE s32 GetMaxEpochBytes() { return _max_epoch_bytes; }

	// Get timeout for reliable message delivery before considering it lost
	CAT_INLINE u32 GetLossTimeout() { return _loss_timeout; }

	// Get number of bytes sent in this epoch
	CAT_INLINE s32 GetSentBytes() { return (s32)_send_epoch_bytes; }

	// Report number of bytes for each successfully sent packet, including overhead bytes
	CAT_INLINE void OnPacketSend(u32 bytes_with_overhead)
	{
		Atomic::Add(&_send_epoch_bytes, bytes_with_overhead);
	}

	void OnTick(u32 now);
	void OnLosses(u32 now, u32 count);
};


} // namespace sphynx


} // namespace cat

#endif // CAT_FLOW_CONTROL_HPP
