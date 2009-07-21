// 07/14/2009 works again
// 06/13/2009 first version

#ifndef CAT_TUNNEL_CLIENT_HPP
#define CAT_TUNNEL_CLIENT_HPP

#include <cat/crypt/tunnel/TunnelSession.hpp>

namespace cat {


//// TunnelClientContext

class TunnelClientContext
{
	friend class TunnelClient;

public:
	static const int MAX_OVERHEAD_BYTES = 16;

	u8 challenge[TwistedEdwardCommon::MAX_BYTES * 4 + MAX_OVERHEAD_BYTES];
	int bytes;
};


//// TunnelClient

class TunnelClient : public TunnelSession, public TwistedEdwardClient
{
	u8 server_public_key[MAX_BYTES*4];
	u8 client_public_key[MAX_BYTES*2];
	u8 shared_secret[MAX_BYTES];
	u8 client_seed[MAX_BYTES];
	Skein challenge_mac;

public:
	int GetResponseBytes() { return KeyBytes * 2; }

	bool Initialize(int bits, const u8 *ServerPublicKey);

	// Generate a new key challenge while waiting for the server's cookie
	// Generate a new challenge every time you reconnect to the server!
	bool GenerateChallenge();

	// Copy challenge over
	// OOB data will be included in the MAC
	void FillChallenge(TunnelClientContext &context, const void *OOB_data = 0, int OOB_bytes = 0);

	// Initialize the tunnel session from the server's response
	// Returns false if the response was invalid.  Invalid response should be ignored as though it were never received
	bool ProcessKeyResponse(u8 *response_buffer, const void *OOB_data = 0, int OOB_bytes = 0);
};


} // namespace cat

#endif // CAT_TUNNEL_CLIENT_HPP
