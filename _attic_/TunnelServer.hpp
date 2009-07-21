// 07/14/2009 works again
// 06/13/2009 first version

#ifndef CAT_TUNNEL_SERVER_HPP
#define CAT_TUNNEL_SERVER_HPP

#include <cat/crypt/tunnel/TunnelSession.hpp>

namespace cat {


//// TunnelServerContext

class TunnelServerContext
{
	friend class TunnelServer;

	u8 shared_secret[TwistedEdwardCommon::MAX_BYTES];
	Skein challenge_mac;

public:
	u8 response[TwistedEdwardCommon::MAX_BYTES * 2];
	int bytes;
};


//// TunnelServer

class TunnelServer : public TwistedEdwardServer
{
public:
	int GetChallengeBytes() { return KeyBytes * 4; }

	// Initialize during startup.  Private key from TwistedEdwardServer::GenerateOfflineStuff
	bool Initialize(int bits, const u8 *ServerPrivateKey);

	// On success, returns true and fills the shared secret
	// Returns false if the response was invalid.  Invalid challenge should be ignored as though it were never received
	bool ValidateChallenge(u8 *input, TunnelServerContext &context,
						   const void *input_OOB_data = 0, int input_OOB_bytes = 0);

	// Keys the ciphers for this session and generates the key response so the client can key its ciphers also
	// OOB data will be included in the MAC
	void GenerateKeyResponse(u8 *input, TunnelServerContext &context, TunnelSession *session,
							 const void *output_OOB_data = 0, int output_OOB_bytes = 0);
};


} // namespace cat

#endif // CAT_TUNNEL_HPP
