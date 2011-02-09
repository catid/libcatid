/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

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

#include <cat/sphynx/Client.hpp>
#include <cat/mem/AlignedAllocator.hpp>
#include <cat/io/Logging.hpp>
#include <cat/io/MMapFile.hpp>
#include <cat/net/DNSClient.hpp>
#include <cat/io/Settings.hpp>
#include <cat/parse/BufferStream.hpp>
#include <cat/time/Clock.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <fstream>
using namespace std;
using namespace cat;
using namespace sphynx;

void Client::OnShutdownRequest()
{
	UDPEndpoint::OnShutdownRequest();
}

bool Client::OnZeroReferences()
{
	return UDPEndpoint::OnZeroReferences();
}

void Client::OnReadRouting(const BatchSet &buffers)
{
	BatchSet garbage;
	garbage.Clear();
	u32 garbage_count = 0;

	BatchSet delivery;
	delivery.Clear();

	// For each buffer in the set,
	for (BatchHead *next, *node = buffers.head; node; node = node->batch_next)
	{
		next = node->batch_next;
		RecvBuffer *buffer = reinterpret_cast<RecvBuffer*>( node );

		SetRemoteAddress(buffer);

		// If packet source is not the server,
		if (_server_addr != buffer->addr)
		{
			garbage.PushBack(buffer);
			++garbage_count;
		}
		else
		{
			buffer->callback = this;
			delivery.PushBack(buffer);
		}
	}

	// If delivery set is not empty,
	if (delivery.head)
		GetIOLayer()->GetWorkerThreads()->DeliverBuffers(_worker_id, delivery);

	// If free set is not empty,
	if (garbage_count > 0)
		ReleaseRecvBuffers(garbage, garbage_count);
}

void Client::OnWorkerRead(IWorkerTLS *itls, const BatchSet &buffers)
{
	SphynxTLS *tls = reinterpret_cast<SphynxTLS*>( itls );
	u32 buffer_count = 0;
	BatchHead *node = buffers.head;

	if (!_connected)
	{
		for (; node; node = node->batch_next)
		{
			++buffer_count;
			RecvBuffer *buffer = reinterpret_cast<RecvBuffer*>( node );
			u32 bytes = buffer->data_bytes;
			u8 *data = GetTrailingBytes(buffer);

			if (bytes == S2C_COOKIE_LEN && data[0] == S2C_COOKIE)
			{
				// Allocate a post buffer
				u8 *pkt = SendBuffer::Acquire(C2S_CHALLENGE_LEN);

				if (!pkt)
				{
					WARN("Client") << "Unable to connect: Cannot allocate buffer for challenge message";
					ConnectFail(ERR_CLIENT_OUT_OF_MEMORY);
					continue;
				}

				// Start ignoring ICMP unreachable messages now that we've seen a pkt from the server
				if (!IgnoreUnreachable())
				{
					WARN("Client") << "ICMP ignore unreachable failed";
				}

				// Construct challenge
				pkt[0] = C2S_CHALLENGE;

				u32 *magic = reinterpret_cast<u32*>( pkt + 1 );
				*magic = getLE32(PROTOCOL_MAGIC);

				u32 *in_cookie = reinterpret_cast<u32*>( data + 1 );
				u32 *out_cookie = reinterpret_cast<u32*>( pkt + 1 + 4 );
				*out_cookie = *in_cookie;

				memcpy(pkt + 1 + 4 + 4, _cached_challenge, CHALLENGE_BYTES);

				// Attempt to post a pkt
				if (!Write(pkt, _server_addr))
				{
					WARN("Client") << "Unable to connect: Cannot post pkt to cookie";
					ConnectFail(ERR_CLIENT_BROKEN_PIPE);
				}
				else
				{
					INANE("Client") << "Accepted cookie and posted challenge";
				}
			}
			else if (bytes == S2C_ANSWER_LEN && data[0] == S2C_ANSWER)
			{
				Port *port = reinterpret_cast<Port*>( data + 1 );

				Port server_session_port = getLE(*port);

				// Ignore packet if the port doesn't make sense
				if (server_session_port > _server_addr.GetPort())
				{
					Skein key_hash;

					// Process answer from server, ignore invalid
					if (_key_agreement_initiator.ProcessAnswer(tls->math, data + 1 + 2, ANSWER_BYTES, &key_hash) &&
						_key_agreement_initiator.KeyEncryption(&key_hash, &_auth_enc, _session_key) &&
						InitializeTransportSecurity(true, _auth_enc))
					{
						_connected = true;
						OnConnect(tls);

						// If we have already received the first encrypted message, keep processing
						node = node->batch_next;
						break;
					}
					else
					{
						INANE("Client") << "Ignored invalid server answer";
					}
				}
				else
				{
					INANE("Client") << "Ignored server answer with insane port";
				}
			}
			else if (bytes == S2C_ERROR_LEN && data[0] == S2C_ERROR)
			{
				HandshakeError err = (HandshakeError)data[1];

				if (err <= ERR_NUM_INTERNAL_ERRORS)
				{
					INANE("Client") << "Ignored invalid server error";
					continue;
				}

				WARN("Client") << "Unable to connect: Server returned error " << GetHandshakeErrorString(err);
				ConnectFail(err);
			}
		}
	}

	BatchSet delivery;
	delivery.Clear();

	// If connected datagrams exist, process them
	for (; node; node = node->batch_next)
	{
		++buffer_count;
		RecvBuffer *buffer = reinterpret_cast<RecvBuffer*>( node );

		// If the data could be decrypted,
		if (_auth_enc.Decrypt(GetTrailingBytes(buffer), buffer->data_bytes))
		{
			delivery.PushBack(buffer);
		}
		else
		{
			WARN("Client") << "Ignored invalid encrypted data";
		}
	}

	// Process all datagrams that decrypted properly
	if (delivery.head) OnTransportDatagrams(tls, delivery);

	ReleaseRecvBuffers(buffers, buffer_count);
}

void Client::OnWorkerTick(IWorkerTLS *itls, u32 now)
{
	SphynxTLS *tls = reinterpret_cast<SphynxTLS*>( itls );

	u32 start_time = Clock::msec_fast();
	u32 first_hello_post = start_time;
	u32 last_hello_post = start_time;
	u32 hello_post_interval = INITIAL_HELLO_POST_INTERVAL;

	// While still not connected,
	while (!_connected)
	{
		// Wait for quit signal
		if (_kill_flag.Wait(HANDSHAKE_TICK_RATE))
			return;

		// If now connected, break out
		if (_connected)
			break;

		u32 now = Clock::msec_fast();

		// If connection timed out,
		if (now - first_hello_post >= CONNECT_TIMEOUT)
		{
			// NOTE: Connection can complete before or after OnConnectFail()
			WARN("Client") << "Unable to connect: Timeout";

			ConnectFail(ERR_CLIENT_TIMEOUT);

			return false;
		}

		// If time to repost hello packet,
		if (now - last_hello_post >= hello_post_interval)
		{
			if (!PostHello())
			{
				WARN("Client") << "Unable to connect: Post failure";

				ConnectFail(ERR_CLIENT_BROKEN_PIPE);

				return false;
			}

			last_hello_post = now;
			hello_post_interval *= 2;
		}

		OnTick(&tls, now);
	}

	// Begin MTU probing after connection completes
	u32 mtu_discovery_time = Clock::msec();
	int mtu_discovery_attempts = 2;

	if (!DontFragment())
	{
		WARN("Client") << "Unable to detect MTU: Unable to set DF bit";

		mtu_discovery_attempts = 0;
	}
	else if (!PostMTUProbe(&tls, MAXIMUM_MTU) ||
		!PostMTUProbe(&tls, MEDIUM_MTU))
	{
		WARN("Client") << "Unable to detect MTU: First probe post failure";
	}

	// Time synchronization begins right away
	u32 next_sync_time = Clock::msec();
	u32 sync_attempts = 0;

	// Set last receive time to avoid disconnecting due to timeout too soon
	_last_recv_tsc = next_sync_time;

	// While waiting for quit signal,
	while (!_kill_flag.Wait(Transport::TICK_INTERVAL))
	{
		u32 now = Clock::msec();

		TickTransport(&tls, now);

		// If it is time for time synch,
		if ((s32)(now - next_sync_time) >= 0)
		{
			PostTimePing();

			// Increase synch interval after the first few data points
			if (sync_attempts >= TIME_SYNC_FAST_COUNT)
				next_sync_time = now + TIME_SYNC_INTERVAL;
			else
			{
				next_sync_time = now + TIME_SYNC_FAST;
				++sync_attempts;
			}
		}

		// If MTU discovery attempts continue,
		if (mtu_discovery_attempts > 0)
		{
			// If it is time to re-probe the MTU,
			if (now - mtu_discovery_time >= MTU_PROBE_INTERVAL)
			{
				// If payload bytes already maxed out,
				if (_max_payload_bytes >= MAXIMUM_MTU - _overhead_bytes)
				{
					// Stop posting probes
					mtu_discovery_attempts = 0;

					// On final attempt set DF=0
					DontFragment(false);
				}
				else
				{
					// If not on final attempt,
					if (mtu_discovery_attempts > 1)
					{
						// Post probes
						if (!PostMTUProbe(&tls, MAXIMUM_MTU - _overhead_bytes) ||
							!PostMTUProbe(&tls, MEDIUM_MTU - _overhead_bytes))
						{
							WARN("Client") << "Unable to detect MTU: Probe post failure";
						}

						mtu_discovery_time = now;
						--mtu_discovery_attempts;
					}
					else
					{
						// Stop posting probes
						mtu_discovery_attempts = 0;

						// On final attempt set DF=0
						DontFragment(false);
					}
				}
			}
		}

		// If no packets have been received,
		if ((s32)(now - _last_recv_tsc) >= TIMEOUT_DISCONNECT)
		{
			Disconnect(DISCO_TIMEOUT, true);

			return true;
		}

		// Tick subclass
		OnTick(&tls, now);

		// Send a keep-alive after the silence limit expires
		if ((s32)(now - _last_send_msec) >= SILENCE_LIMIT)
		{
			PostTimePing();

			next_sync_time = now + TIME_SYNC_INTERVAL;
		}
	}
}

Client::Client()
{
	_connected = false;
	_last_send_msec = 0;

	// Clock synchronization
	_ts_next_index = 0;
	_ts_sample_count = 0;
}

bool Client::InitialConnect(SphynxLayer *layer, SphynxTLS *tls, TunnelPublicKey &public_key, const char *session_key)
{
	if (!tls->Valid())
	{
		WARN("Client") << "Failed to connect: Invalid thread local storage";
		return false;
	}

	if (!public_key.Valid())
	{
		WARN("Client") << "Failed to connect: Invalid server public key provided";
		return false;
	}

	// Verify public key and initialize crypto library with it
	if (!_key_agreement_initiator.Initialize(tls->math, public_key))
	{
		WARN("Client") << "Failed to connect: Corrupted server public key provided";
		return false;
	}

	// Generate a challenge for the server
	if (!_key_agreement_initiator.GenerateChallenge(tls->math, tls->csprng,
										_cached_challenge, CHALLENGE_BYTES))
	{
		WARN("Client") << "Failed to connect: Cannot generate crypto-challenge";
		return false;
	}

	// Copy session key
	CAT_STRNCPY(_session_key, session_key, SESSION_KEY_BYTES);

	// Copy public key
	_server_public_key = public_key;

	// Get SupportIPv6 flag from settings
	bool only_ipv4 = Settings::ii->getInt("Sphynx.Client.SupportIPv6", 0) == 0;

	// Get kernel receive buffer size
	int kernelReceiveBufferBytes =
		Settings::ii->getInt("Sphynx.Client.KernelReceiveBuffer", 1000000);

	// Attempt to bind to any port and accept ICMP errors initially
	if (!Bind(layer, only_ipv4, 0, false, kernelReceiveBufferBytes))
	{
		WARN("Client") << "Failed to connect: Unable to bind to any port";
		return false;
	}

	// Initialize max payload bytes
	InitializePayloadBytes(Is6());

	return true;
}

bool Client::FinalConnect(const NetAddr &addr)
{
	if (!addr.Valid())
	{
		WARN("Client") << "Failed to connect: Invalid server address";
		return false;
	}

	_server_addr = addr;

	// Convert server address if needed
	if (!_server_addr.Convert(Is6()))
	{
		WARN("Client") << "Failed to connect: Invalid address specified";
		return false;
	}

	// Attempt to post hello message
	if (!PostHello())
	{
		WARN("Client") << "Failed to connect: Post failure";
		return false;
	}

	return true;
}

bool Client::Connect(SphynxLayer *layer, SphynxTLS *tls, const char *hostname, Port port, TunnelPublicKey &public_key, const char *session_key)
{
	if (!InitialConnect(layer, tls, public_key, session_key))
	{
		RequestShutdown();
		return false;
	}

	_server_addr.SetPort(port);

	if (!layer->GetDNSClient()->Resolve(layer, hostname, fastdelegate::MakeDelegate(this, &Client::OnResolve), this))
	{
		WARN("Client") << "Failed to connect: Cannot start hostname resolve for " << hostname;
		RequestShutdown();
		return false;
	}

	return true;
}

bool Client::Connect(SphynxLayer *layer, SphynxTLS *tls, const NetAddr &addr, TunnelPublicKey &public_key, const char *session_key)
{
	if (!InitialConnect(layer, tls, public_key, session_key) ||
		!FinalConnect(addr))
	{
		RequestShutdown();
		return false;
	}

	return true;
}

bool Client::OnResolve(const char *hostname, const NetAddr *array, int array_length)
{
	// If resolve failed,
	if (array_length <= 0)
	{
		WARN("Client") << "Failed to connect: Server hostname resolve failed";
		RequestShutdown();
		return false;
	}
	else
	{
		NetAddr addr = array[0];
		addr.SetPort(_server_addr.GetPort());

		INFO("Client") << "Connecting: Resolved '" << hostname << "' to " << addr.IPToString();

		if (!FinalConnect(addr))
		{
			WARN("Client") << "Failed to connect: Cannot start hostname resolve for " << hostname;
			RequestShutdown();
			return false;
		}
	}

	return true;
}

void Client::OnUnreachable(const NetAddr &src)
{
	// If IP matches the server and we're not connected yet,
	if (!_connected && _server_addr.EqualsIPOnly(src))
	{
		WARN("Client") << "Failed to connect: ICMP error received from server address";
		ConnectFail(ERR_CLIENT_ICMP);
		RequestShutdown();
	}
}

bool Client::PostHello()
{
	if (_connected)
	{
		WARN("Client") << "Refusing to post hello after connected";
		return false;
	}

	// Allocate space for a post buffer
	u8 *pkt = SendBuffer::Acquire(C2S_HELLO_LEN);

	// If unable to allocate,
	if (!pkt)
	{
		WARN("Client") << "Cannot allocate a post buffer for hello packet";
		return false;
	}

	// Construct packet
	pkt[0] = C2S_HELLO;

	u32 *magic = reinterpret_cast<u32*>( pkt + 1 );
	*magic = getLE32(PROTOCOL_MAGIC);

	u8 *public_key = pkt + 1 + 4;
	memcpy(public_key, _server_public_key.GetPublicKey(), PUBLIC_KEY_BYTES);

	// Attempt to post packet
	if (!Write(pkt, _server_addr))
	{
		WARN("Client") << "Unable to post hello packet";
		return false;
	}

	INANE("Client") << "Posted hello packet";

	return true;
}

bool Client::PostTimePing()
{
	u32 timestamp = Clock::msec();

	// Write it out-of-band to avoid delays in transmission
	return WriteUnreliableOOB(IOP_C2S_TIME_PING, &timestamp, sizeof(timestamp), SOP_INTERNAL);
}

bool Client::PostDatagrams(const BatchSet &buffers)
{
	u32 now = getLocalTime();
	u16 timestamp = getLE(encodeClientTimestamp(now));

	/*
		The format of each buffer:

		[TRANSPORT(X)] [TIMESTAMP(2)] [ENCRYPTION(11)]

		At this point, the timestamp has not been written.
		The encryption overhead is also not filled in yet.

		Each buffer's data_bytes includes the X bytes of transport layer data
		as well as an additional 13 bytes of overhead that will now be filled.
	*/

	// For each datagram to send,
	for (BatchHead *node = buffers.head; node; node = node->batch_next)
	{
		// Unwrap the message data
		SendBuffer *buffer = reinterpret_cast<SendBuffer*>( node );
		u8 *msg_data = buffer->GetData();
		u32 buf_bytes = buffer->GetSize();
		u32 msg_bytes = buffer->GetSize() - AuthenticatedEncryption::OVERHEAD_BYTES;

		// Write timestamp right before the encryption overhead
		*(u16*)(msg_data + msg_bytes - 2) = timestamp;

		// Encrypt the message
		_auth_enc.Encrypt(msg_data, buf_bytes, msg_bytes);
	}

	// If write fails,
	if (!Write(buffers, _server_addr))
		return false;

	// Update the last send time to make sure we keep the channel occupied
	_last_send_msec = now;
	return true;
}

void Client::OnInternal(SphynxTLS *tls, u32 send_time, u32 recv_time, BufferStream data, u32 bytes)
{
	switch (data[0])
	{
	case IOP_S2C_MTU_SET:
		if (bytes == IOP_S2C_MTU_SET_LEN)
		{
			u16 max_payload_bytes = getLE(*reinterpret_cast<u16*>( data + 1 ));

			WARN("Client") << "Got IOP_S2C_MTU_SET.  Max payload bytes = " << max_payload_bytes;

			// If new maximum payload is greater than the previous one,
			if (max_payload_bytes > _max_payload_bytes)
			{
				// Set max payload bytes
				_max_payload_bytes = max_payload_bytes;
				_send_flow.SetMTU(max_payload_bytes);
			}
		}
		break;

	case IOP_S2C_TIME_PONG:
		if (bytes == IOP_S2C_TIME_PONG_LEN)
		{
			u32 client_now = Clock::msec();
			u32 *timestamps = reinterpret_cast<u32*>( data + 1 );
			u32 client_ping_send_time = timestamps[0];
			u32 server_ping_recv_time = getLE(timestamps[1]);
			u32 rtt = client_now - client_ping_send_time;

			// If RTT is not impossible,
			if (rtt < TIMEOUT_DISCONNECT)
			{
				s32 delta = server_ping_recv_time - client_ping_send_time - (rtt / 2);

				//WARN("Client") << "Got IOP_S2C_TIME_PONG.  rtt=" << rtt << " unbalanced by " << (s32)(rtt/2 - (FromServerTime(server_ping_recv_time) - client_ping_send_time));

				UpdateTimeSynch(rtt, delta);

				OnTimestampDeltaUpdate();
			}
		}
		break;

	case IOP_DISCO:
		if (bytes == IOP_DISCO_LEN)
		{
			WARN("Client") << "Got IOP_DISCO reason = " << (int)data[1];

			Disconnect(data[1], false);
		}
		break;
	}
}

void Client::ConnectFail(HandshakeError err)
{
	if (Atomic::Set(&_destroyed, 1) == 0)
	{
		TransportDisconnected();

		OnConnectFail(err);

		_kill_flag.Set();

		Close();
	}
}

/*
	Clock Synchronization

	--Definition of Clock Delta:

	There is a roughly linear relationship between server and client time
	because both clocks are trying to tick once per millisecond.

	Clock Delta = (Server Remote Time) - (Client Local Time)

	--Measurements:

	Clients are responsible for synchronizing their clocks with the server,
	so that the server does not need to store this state for each user.

	At least every 10 seconds the client will ping the server to collect a
	timing sample (IOP_C2S_TIME_PING and IOP_S2C_TIME_PONG).

	After initial connection, the first 8 measurements are performed at 5
	second intervals to build up confidence in the time synchronization faster.

	T0 = Client ping send time (in client units)
	T1 = Server pong send time (in server units)
	T1' = Client time when server sent the pong (in client units)
	T2 = Client pong receive time (in client units)

	Round trip time (RTT) = T2 - T0
	Clock Delta = T1 - T1'

	--Calculation of Clock Delta:

	Assuming balanced ping and pong delays:

	T1' ~= T0 + RTT / 2
	Clock Delta = T1 - ((T2 - T0) / 2 + T0)

	--Incorporating Measurement Quality:

	Not all deltas are of equal quality.  When the ping and/or pong delays
	are higher for various practical reasons, the delays become unbalanced
	and this estimate of clock delta loses accuracy.  Fortunately the RTT
	will also increase when this occurs, and so measurements with lower RTT
	may be considered more accurate.

	Throwing away 75% of the measurements, keeping the lowest RTT samples,
	should prevent bad data from affecting the clock synchronization.

	Averaging the remaining 25% seems to provide good, stable values for delta.

	--Incorporating 1st Order Drift:

	This is a bad idea.  I did take the time to try it out.  It requires a lot
	more bandwidth to collect enough data, and if the data is ever bad the
	drift calculations will amplify the error.

	Accounting for drift will defeat naive speed cheats since the increased
	clock tick rate will be corrected out just like normal drift.  However that
	only fixes timestamps and not increased rate of movement or other effects.

	The other problem is that by taking drift into account it takes a lot more
	processing time to do the calculations, and it requires a mutex to protect
	the calculated coefficients since they are no longer atomically updated.

	Furthermore, doing first order calculations end up requiring some
	consideration of timestamp rollover, where all the timestamps in the
	equations need to be taken relative to some base value.

	Incorporating drift is overkill and will cause more problems than it solves.
*/
void Client::UpdateTimeSynch(u32 rtt, s32 delta)
{
	// Increment the sample count if we haven't exhausted the array space yet
	if (_ts_sample_count < MAX_TS_SAMPLES)
		_ts_sample_count++;

	// Insert sample
	_ts_samples[_ts_next_index].delta = delta;
	_ts_samples[_ts_next_index].rtt = rtt;

	// Increment write address to next oldest entry or next empty space
	_ts_next_index = (_ts_next_index + 1) % MAX_TS_SAMPLES;

	// Find the lowest 25% RTT samples >= MIN_TS_SAMPLES
	TimesPingSample *BestSamples[MAX_TS_SAMPLES / 4 + MIN_TS_SAMPLES];
	u32 num_samples = _ts_sample_count / 4;
	if (num_samples < MIN_TS_SAMPLES) num_samples = MIN_TS_SAMPLES;

	// Find the highest RTT sample so far
	u32 highest_rtt = _ts_samples[0].rtt, highest_index = 0;
	BestSamples[0] = &_ts_samples[0];
	u32 best_count = 1;

	// Calculate the average delta and assume no drift
	s64 sum_delta = _ts_samples[0].delta;

	// While still trying to fill a minimum number of samples,
	u32 ii;
	for (ii = 1; ii < _ts_sample_count && best_count < num_samples; ++ii)
	{
		u32 rtt = _ts_samples[ii].rtt;
		sum_delta += _ts_samples[ii].delta;

		if (rtt > highest_rtt)
		{
			highest_rtt = rtt;
			highest_index = ii;
		}

		BestSamples[best_count++] = &_ts_samples[ii];
	}

	// For the remainder of the samples, fight over spots
	for (; ii < _ts_sample_count; ++ii)
	{
		u32 sample_rtt = _ts_samples[ii].rtt;

		// Replace highest RTT if the new sample has lower RTT
		if (sample_rtt >= highest_rtt)
			continue;

		// Replace it in the average calculation
		sum_delta -= BestSamples[highest_index]->delta;
		sum_delta += _ts_samples[ii].delta;

		BestSamples[highest_index] = &_ts_samples[ii];

		// Find new highest RTT entry
		highest_rtt = sample_rtt;

		for (u32 jj = 0; jj < best_count; ++jj)
		{
			u32 rtt = BestSamples[jj]->rtt;

			if (rtt <= highest_rtt)
				continue;

			highest_rtt = rtt;
			highest_index = jj;
		}
	}

	// Finalize the average, best delta
	_ts_delta = (u32)(sum_delta / best_count);
}