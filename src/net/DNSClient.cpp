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

#include <cat/net/DNSClient.hpp>
#include <cat/io/Logging.hpp>
#include <cat/time/Clock.hpp>
#include <cat/lang/Strings.hpp>
using namespace cat;


//// DNSClient

bool DNSClient::GetServerAddr()
{
	// Mark server address as invalid
	_server_addr.Invalidate();

#if defined(CAT_OS_WINDOWS) || defined(CAT_OS_WINDOWS_CE)

	// Based on approach used in Tiny Asynchronous DNS project by
	// Sergey Lyubka <valenok@gmail.com>.  I owe you a beer! =)
	const int SUBKEY_NAME_MAXLEN = 512;
	const int SUBKEY_DATA_MAXLEN = 512;

	// Open Tcpip Interfaces key
	HKEY key;
	LSTATUS err = RegOpenKey(HKEY_LOCAL_MACHINE,
		"SYSTEM\\ControlSet001\\Services\\Tcpip\\Parameters\\Interfaces", &key);

	// Handle errors opening the key
	if (err != ERROR_SUCCESS)
	{
		WARN("DNS") << "Initialization: Unable to open registry key for Tcpip interfaces: " << err;
		return false;
	}

	// For each subkey,
	char subkey_name[SUBKEY_NAME_MAXLEN];
	for (int ii = 0; ERROR_SUCCESS == RegEnumKey(key, ii, subkey_name, sizeof(subkey_name)); ++ii)
	{
		HKEY subkey;

		// Open interface subkey
		if (ERROR_SUCCESS == RegOpenKey(key, subkey_name, &subkey))
		{
			BYTE data[SUBKEY_DATA_MAXLEN];
			DWORD type, data_len = sizeof(data);

			// Get subkey's DhcpNameServer value
			if (ERROR_SUCCESS == RegQueryValueEx(subkey, "DhcpNameServer", 0, &type, data, &data_len))
			{
				// If type is a string,
				if (type == REG_EXPAND_SZ || type == REG_SZ)
				{
					// Insure it is nul-terminated
					data[sizeof(data) - 1] = '\0';

					// Convert address string to binary address
					NetAddr addr((const char*)data, 53);

					// If address is routable,
					if (addr.IsRoutable())
					{
						// Set server address to the last valid one in the enumeration
						_server_addr = addr;
					}
				}
			}
		}
	}

	RegCloseKey(key);

#else // POSIX version:

	// TODO

#endif

	// Return success if server address is now valid
	return _server_addr.Valid();
}

bool DNSClient::PostDNSPacket(DNSRequest *req, u32 now)
{
	req->last_post_time = now;

	int bytes = 32;
	u8 *dns_packet = GetPostBuffer(bytes);

	// TODO

	return Post(_server_addr, dns_packet, bytes);
}

bool DNSClient::PerformLookup(DNSRequest *req)
{
	AutoMutex lock(_request_lock);

	u32 now = Clock::msec_fast();

	if (!PostDNSPacket(req, now))
		return false;

	req->first_post_time = now;

	// Insert at end of list
	req->next = 0;
	req->last = _request_tail;
	if (_request_tail) _request_tail->next = req;
	else _request_head = req;
	_request_tail = req;

	return true;
}

void DNSClient::CacheAdd(DNSRequest *req)
{
	AutoMutex lock(_cache_lock);

	// If still growing cache,
	if (_cache_size < DNSCACHE_MAX_REQS)
		_cache_size++;
	else
	{
		// Remove oldest one from cache
		DNSRequest *tokill = _cache_tail;

		if (tokill)
		{
			DNSRequest *last = tokill->last;

			_cache_tail = last;
			if (last) last->next = 0;
			else _cache_head = 0;

			delete tokill;
		}
	}

	// Insert at head
	req->next = _cache_head;
	req->last = 0;
	if (_cache_head) _cache_head->last = req;
	else _cache_tail = req;
	_cache_head = req;

	// Set update time
	req->last_post_time = Clock::msec_fast();
}

DNSRequest *DNSClient::CacheGet(const char *hostname)
{
	// For each cache entry,
	for (DNSRequest *req = _cache_head; req; req = req->next)
	{
		// If hostname of cached request equals the new request,
		if (iStrEqual(req->hostname, hostname))
		{
			// If the cache has not expired,
			if (Clock::msec_fast() - req->last_post_time < DNSCACHE_TIMEOUT)
			{
				// Return the cache entry
				return req;
			}
			else
			{
				// Unlink remainder of list (they will all be old)
				DNSRequest *last = req->last;

				_cache_tail = last;
				if (last) last->next = 0;
				else _cache_head = 0;

				// For each item that was unlinked,
				for (DNSRequest *next, *tokill = req; tokill; tokill = next)
				{
					next = tokill->next;

					// Delete each item
					delete tokill;

					// Reduce the cache size
					--_cache_size;
				}

				// Return indicating that the cache did not contain the hostname
				return 0;
			}
		}
	}

	return 0;
}

void DNSClient::CacheKill(DNSRequest *req)
{
	// Unlink from doubly-linked list
	DNSRequest *last = req->last;
	DNSRequest *next = req->next;

	if (last) last->next = next;
	else _cache_head = next;
	if (next) next->last = last;
	else _cache_tail = last;

	--_cache_size;

	// Free memory
	delete req;
}

bool DNSClient::ThreadFunction(void *param)
{
	const int TICK_RATE = 200; // milliseconds

	// Check for timeouts
	while (WaitForQuitSignal(TICK_RATE))
	{
		AutoMutex lock(_request_lock);

		// Cache current time
		u32 now = Clock::msec_fast();

		// For each pending request,
		for (DNSRequest *req_next, *req = _request_head; req; req = req_next)
		{
			req_next = req->next; // cached for deletion

			// If the request has timed out or reposting failed,
			if ((now - req->first_post_time >= DNSREQ_TIMEOUT) ||
				(now - req->last_post_time >= DNSREQ_REPOST_TIME && !PostDNSPacket(req, now)))
			{
				// Invoke callback with number of responses = 0 to indicate error
				req->cb(req->hostname, req->responses, 0);

				// Release reference if desired
				if (req->ref) req->ref->ReleaseRef();

				// Unlink from doubly-linked list
				DNSRequest *next = req->next;
				DNSRequest *last = req->last;

				if (next) next->last = last;
				else _request_tail = last;
				if (last) last->next = next;
				else _request_head = next;

				// Free memory
				delete req;
			}
		}
	}

	return true;
}

DNSClient::~DNSClient()
{
	if (!StopThread())
	{
		WARN("DNS") << "Unable to stop timer thread.  Was it started?";
	}
}

bool DNSClient::Initialize()
{
	// Add a reference so that DNSClient cannot be destroyed
	DNSClient::ii->AddRef();

	_dns_unavailable = true;

	// Attempt to bind to any port and accept ICMP errors initially
	if (!Bind(0, false))
	{
		WARN("DNS") << "Initialization failure: Unable to bind to any port";
		return false;
	}

	// Attempt to get server address from operating system
	if (!GetServerAddr())
	{
		WARN("DNS") << "Initialization failure: Unable to discover DNS server address";
		Close();
		return false;
	}

	// Attempt to start the timer thread
	if (!StartThread())
	{
		WARN("DNS") << "Initialization failure: Unable to start timer thread";
		Close();
		return false;
	}

	_dns_unavailable = false;

	return true;
}
void DNSClient::Shutdown()
{
	// NOTE: Does not remove artificial reference added on Initialize(), so
	// the object is not actually destroyed.  We allow the ThreadPool to
	// destroy this object after all the worker threads are dead.

	Close();
}

bool DNSClient::Resolve(const char *hostname, DNSResultCallback callback, ThreadRefObject *holdRef)
{
	// Try to interpret hostname as numeric
	NetAddr addr(hostname);

	// If it was interpreted,
	if (addr.Valid())
	{
		// Immediately invoke callback
		callback(hostname, &addr, 1);

		return true;
	}

	AutoMutex lock(_cache_lock);

	// Check cache
	DNSRequest *cached_request = CacheGet(hostname);

	// If it was in the cache,
	if (cached_request)
	{
		// Immediately invoke callback
		if (!callback(hostname, cached_request->responses, cached_request->num_responses))
		{
			// Kill cached request when asked
			CacheKill(cached_request);
		}

		return true;
	}

	lock.Release();

	// If DNS lookup is unavailable,
	if (_dns_unavailable)
		return false;

	// If the hostname was too long,
	if (strlen(hostname) > HOSTNAME_MAXLEN)
		return false;

	// Create a new request
	DNSRequest *request = new DNSRequest;
	if (!request) return false;

	// Fill request
	CAT_STRNCPY(request->hostname, hostname, sizeof(request->hostname));
	request->ref = holdRef;
	request->cb = callback;

	if (holdRef) holdRef->AddRef();

	// If lookup could not be performed,
	if (!PerformLookup(request))
	{
		if (holdRef) holdRef->ReleaseRef();
		return false;
	}

	return true;
}

void DNSClient::OnUnreachable(const NetAddr &src)
{
	// If IP matches the server and we're not connected yet,
	if (_server_addr.EqualsIPOnly(src))
	{
		WARN("DNS") << "Failed to contact DNS server: ICMP error received from server address";

		// Close socket so that DNS resolves will be squelched
		Close();
	}
}

void DNSClient::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{
	// If packet source is not the server, ignore this packet
	if (_server_addr != src)
		return;

	// TODO
}

void DNSClient::OnWrite(u32 bytes)
{

}

void DNSClient::OnClose()
{
	// Marks DNS as unavailable OnClose() so that further resolve requests are squelched.
	_dns_unavailable = true;
}
