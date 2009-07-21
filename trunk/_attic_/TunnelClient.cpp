#include <cat/crypt/tunnel/TunnelClient.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/rand/Fortuna.hpp>
using namespace cat;

// Initialize during startup.  Private key from TwistedEdwardServer::GenerateOfflineStuff
bool TunnelClient::Initialize(int bits, const u8 *ServerPublicKey)
{
	if (!TwistedEdwardClient::Initialize(bits))
		return false;

	memcpy(server_public_key, ServerPublicKey, KeyBytes*4);

	return true;
}

// Generate a new key challenge while waiting for the server's cookie
bool TunnelClient::GenerateChallenge()
{
	if (!ComputeSharedSecret(server_public_key, client_public_key, shared_secret))
		return false;

	FortunaOutput *csprng = FortunaFactory::GetLocalOutput();

	csprng->Generate(client_seed, KeyBytes);

	challenge_mac.BeginKey(KeyBits);
	challenge_mac.Crunch(shared_secret, KeyBytes);
	challenge_mac.Crunch(client_seed, KeyBytes);
	challenge_mac.Crunch(client_public_key, KeyBytes*2);
	challenge_mac.End();

	return true;
}

// Copy challenge over
void TunnelClient::FillChallenge(TunnelClientContext &context, const void *OOB_data, int OOB_bytes)
{
	memcpy(context.challenge, client_seed, KeyBytes);

	memcpy(context.challenge + KeyBytes, client_public_key, KeyBytes*2);

	Skein mac;
	mac.SetKey(&challenge_mac);
	mac.BeginMAC();
	mac.CrunchString("client-challenge");
	mac.Crunch(OOB_data, OOB_bytes);
	mac.End();

	mac.Generate(context.challenge + KeyBytes*3, KeyBytes);

	context.bytes = KeyBytes * 4;
}

// Initialize the tunnel session from the server's key response
bool TunnelClient::ProcessKeyResponse(u8 *buffer, const void *OOB_data, int OOB_bytes)
{
	Skein mac;
	if (!mac.SetKey(&challenge_mac) || !mac.BeginMAC()) return false;
	mac.CrunchString("server-response");
	mac.Crunch(buffer, KeyBytes); // hash server seed
	mac.Crunch(OOB_data, OOB_bytes);
	mac.End();

	u8 expected[MAX_BYTES];
	mac.Generate(expected, KeyBytes);

	if (!SecureEqual(expected, buffer + KeyBytes, KeyBytes))
		return false;

	SetKey(KeyBytes, shared_secret, client_seed, buffer, true);

	return true;
}
