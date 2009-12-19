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

#ifndef CAT_DNS_CLIENT_HPP
#define CAT_DNS_CLIENT_HPP

#include <cat/net/ThreadPoolSockets.hpp>
#include <cat/threads/LoopThread.hpp>
#include <cat/port/FastDelegate.h>

namespace cat {


static const int HOSTNAME_MAXLEN = 128; // Max characters in a hostname request
static const int DNSREQ_TIMEOUT = 3000; // DNS request timeout interval
static const int DNSCACHE_MAX_REQS = 8; // Maximum number of requests to cache
static const int DNSCACHE_MAX_RESP = 8; // Maximum number of responses to cache
static const int DNSCACHE_TIMEOUT = 60000; // Time until a cached response is dropped

// Prototype: bool MyResultCallback(const char *hostname, NetAddr array[], int array_length);
typedef fastdelegate::FastDelegate3<const char *, NetAddr[], int, bool> DNSResultCallback;


//// DNSRequest

struct DNSRequest
{
	DNSRequest *last, *next;

	// Timestamp for last update, used for resolve timeout and cache timeout
	u32 last_update_time;

	// Our copy of the hostname string and callback
	char hostname[HOSTNAME_MAXLEN+1];
	DNSResultCallback cb;
	ThreadRefObject *ref;

	// For caching purposes
	NetAddr responses[DNSCACHE_MAX_RESP];
	int num_responses;
};


//// DNSClient

class DNSClient : LoopThread, protected UDPEndpoint, public Singleton<DNSClient>
{
	CAT_SINGLETON(DNSClient)
	{
		_dns_unavailable = true;
	}

public:
	virtual ~DNSClient();

	/*
		In your startup code, call Initialize() and check the return value.
		In your shutdown code, call Shutdown().  This will delete the DNSClient object.
	*/
	bool Initialize();
	void Shutdown();

	/*
		If hostname is numeric or in the cache, the callback function will be invoked
		immediately from the requesting thread, rather than from another thread.

		First attempts numerical resolve of hostname, then queries the DNS server.

		Hostname string length limited to HOSTNAME_MAXLEN characters.
		Caches the most recent DNSCACHE_MAX_REQS requests.
		Returns up to DNSCACHE_MAX_RESP addresses per resolve request.
		Performs DNS lookup on a cached request after DNSCACHE_TIMEOUT.
		Gives up on DNS lookup after DNSREQ_TIMEOUT.

		If holdRef is valid, the reference will be held until the callback completes.

		If no results were found, array_length == 0.
		If the callback returns false, the result will not be entered into the cache.

		The resolved addresses may need to be promoted to IPv6.

		If Resolve() returns false, no callback will be generated.
	*/
	bool Resolve(const char *hostname, DNSResultCallback, ThreadRefObject *holdRef = 0);

private:
	NetAddr _server_addr;
	bool _dns_unavailable;

private:
	Mutex _list_lock;
	DNSRequest *_request_head;
	DNSRequest *_cache_head;
	int _cache_size;

private:
	bool GetServerAddr();
	bool PerformLookup(DNSRequest *req);

private:
	// These may be called from multiple threads simultaneously
	DNSRequest *CacheGet(const char *hostname);
	void CacheKill(DNSRequest *req);

private:
	bool ThreadFunction(void *param);

protected:
	void OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes);
	void OnWrite(u32 bytes);
	void OnClose();
	void OnUnreachable(const NetAddr &src);
};


} // namespace cat

#endif // CAT_DNS_CLIENT_HPP
