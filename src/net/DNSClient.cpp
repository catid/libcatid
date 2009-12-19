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
using namespace cat;


void DNSClient::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{

}
void DNSClient::OnWrite(u32 bytes)
{

}
void DNSClient::OnClose()
{

}
void DNSClient::OnUnreachable(const NetAddr &src)
{

}

bool DNSClient::GetServerAddr()
{
	// Mark server address as invalid
	_server_addr.Invalidate();

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
		char data[SUBKEY_DATA_MAXLEN];
		DWORD data_len = sizeof(data);

		// Get subkey's DhcpNameServer value
		if (ERROR_SUCCESS == RegGetValue(key, subkey_name, "DhcpNameServer", data,
								RRF_RT_REG_MULTI_SZ | RRF_RT_REG_SZ, &type, data, &data_len))
		{
			// Convert address string to binary address
			NetAddr addr(data, 53);

			// If address is routable,
			if (addr.IsRoutable())
			{
				// Set server address to the last one in the enumeration
				_server_addr = addr;
			}
		}
	}

	RegCloseKey(key);

	// Return success if server address is now valid
	return _server_addr.Valid();
}

DNSRequest *DNSClient::CacheGet(const char *hostname)
{

}

void DNSClient::CacheKill(DNSRequest *req)
{

}

bool DNSClient::ThreadFunction(void *param)
{
	const int TICK_RATE = 200; // milliseconds

	// Wait for quit signal
	while (WaitForQuitSignal(TICK_RATE))
	{
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
		WARN("DNS") << "Initialization failure: Unable to discover DNS server";
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
	request->last_update_time = Clock::msec_fast();
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
		WARN("DNS") << "Failed to connect: ICMP error received from server address";
		Close();
	}
}

void DNSClient::OnRead(ThreadPoolLocalStorage *tls, const NetAddr &src, u8 *data, u32 bytes)
{
	// If packet source is not the server, ignore this packet
	if (_server_addr != src)
		return;
}

void DNSClient::OnWrite(u32 bytes)
{

}
