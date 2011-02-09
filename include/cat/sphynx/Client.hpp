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

#ifndef CAT_SPHYNX_CLIENT_HPP
#define CAT_SPHYNX_CLIENT_HPP

#include <cat/sphynx/Transport.hpp>
#include <cat/crypt/tunnel/KeyAgreementInitiator.hpp>
#include <cat/threads/Thread.hpp>
#include <cat/threads/WaitableFlag.hpp>

namespace cat {


namespace sphynx {


// Base class for a Sphynx client
class Client : public UDPEndpoint, public Transport, public WorkerCallbacks
{
	static const int HANDSHAKE_TICK_RATE = 100; // milliseconds
	static const int INITIAL_HELLO_POST_INTERVAL = 200; // milliseconds
	static const int CONNECT_TIMEOUT = 6000; // milliseconds
	static const u32 MTU_PROBE_INTERVAL = 8000; // seconds
	static const int CLIENT_THREAD_KILL_TIMEOUT = 10000; // seconds

	static const int SESSION_KEY_BYTES = 32;
	char _session_key[SESSION_KEY_BYTES];

	KeyAgreementInitiator _key_agreement_initiator;
	TunnelPublicKey _server_public_key;
	u8 _cached_challenge[CHALLENGE_BYTES];

	WaitableFlag _kill_flag;

	u32 _last_send_msec;
	NetAddr _server_addr;
	bool _connected;
	u32 _worker_id;
	AuthenticatedEncryption _auth_enc;

	// Last time a packet was received from the server -- for disconnect timeouts
	u32 _last_recv_tsc;

	// Clock Synchronization
	static const int TIME_SYNC_INTERVAL = 10000; // Normal time synch interval, milliseconds
	static const int TIME_SYNC_FAST_COUNT = 8; // Number of fast measurements at the start, milliseconds
	static const int TIME_SYNC_FAST = 5000; // Interval during fast measurements, milliseconds
	static const int MAX_TS_SAMPLES = 16; // Maximum timestamp sample memory
	static const int MIN_TS_SAMPLES = 1; // Minimum number of timestamp samples

	struct TimesPingSample {
		u32 rtt;
		s32 delta;
	} _ts_samples[MAX_TS_SAMPLES];
	u32 _ts_sample_count, _ts_next_index;

	void UpdateTimeSynch(u32 rtt, s32 delta);

	bool PostHello();
	bool PostTimePing();

	// Return false to remove resolve from cache
	bool OnResolve(const char *hostname, const NetAddr *array, int array_length);

	virtual bool PostDatagrams(const BatchSet &buffers);
	virtual void OnInternal(SphynxTLS *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes);

	void ConnectFail(HandshakeError err);

	bool InitialConnect(SphynxLayer *layer, SphynxTLS *tls, TunnelPublicKey &public_key, const char *session_key);
	bool FinalConnect(const NetAddr &addr);

public:
	Client();
	CAT_INLINE virtual ~Client() {}

	// Once you call Connect(), the object may be deleted at any time.  If you want to keep a reference to it, AddRef() before calling
	bool Connect(SphynxLayer *layer, SphynxTLS *tls, const char *hostname, Port port, TunnelPublicKey &public_key, const char *session_key);
	bool Connect(SphynxLayer *layer, SphynxTLS *tls, const NetAddr &addr, TunnelPublicKey &public_key, const char *session_key);

	// Will not notify remote host on DISCO_SILENT
	void Disconnect(u8 reason = DISCO_SILENT);

protected:
	virtual void OnShutdownRequest();
	virtual bool OnZeroReferences();

	virtual void OnReadRouting(const BatchSet &buffers);
	virtual void OnUnreachable(const NetAddr &src);

	virtual void OnWorkerRead(IWorkerTLS *tls, const BatchSet &buffers);
	virtual void OnWorkerTick(IWorkerTLS *tls, u32 now);

	CAT_INLINE bool IsConnected() { return _connected; }

	virtual void OnConnectFail(sphynx::HandshakeError err) = 0;
	virtual void OnConnect(SphynxTLS *tls) = 0;
	//virtual void OnMessages(SphynxTLS *tls, UserMessage msgs[], u32 count) = 0;
	virtual void OnDisconnectReason(u8 reason) = 0; // Called when the server provides a reason for disconnection
	virtual void OnTick(SphynxTLS *tls, u32 now) = 0;
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_CLIENT_HPP