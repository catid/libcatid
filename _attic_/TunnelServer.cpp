#include <cat/crypt/tunnel/TunnelServer.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/crypt/rand/Fortuna.hpp>
using namespace cat;

// Initialize during startup.  Private key from TwistedEdwardServer::GenerateOfflineStuff
bool TunnelServer::Initialize(int bits, const u8 *ServerPrivateKey)
{
	if (!TwistedEdwardServer::Initialize(bits))
		return false;

	SetPrivateKey(ServerPrivateKey);

	return true;
}

// On success, returns true and fills the shared secret
bool TunnelServer::ValidateChallenge(u8 *input, TunnelServerContext &context,
									 const void *input_OOB_data, int input_OOB_bytes)
{
	// Validate public key and compute shared secret
	if (!ComputeSharedSecret(input + KeyBytes, context.shared_secret))
		return false;

	// Generate expected MAC
	Skein *challenge_mac = &context.challenge_mac;
	if (!challenge_mac->BeginKey(KeyBits)) return false;
	challenge_mac->Crunch(context.shared_secret, KeyBytes);
	challenge_mac->Crunch(input, KeyBytes); // hash client seed
	challenge_mac->Crunch(input + KeyBytes, KeyBytes*2); // hash client public key
	challenge_mac->End();

	Skein mac;
	if (!mac.SetKey(challenge_mac) || !mac.BeginMAC()) return false;
	mac.CrunchString("client-challenge");
	mac.Crunch(input_OOB_data, input_OOB_bytes);
	mac.End();

	u8 expected[TwistedEdwardCommon::MAX_BYTES];
	mac.Generate(expected, KeyBytes);

	// Validate MAC
	return SecureEqual(expected, input + KeyBytes*3, KeyBytes);
}

// Keys the ciphers for this session and generates the key response so the client can key its ciphers also
void TunnelServer::GenerateKeyResponse(u8 *input, TunnelServerContext &context, TunnelSession *session,
									   const void *output_OOB_data, int output_OOB_bytes)
{
	FortunaOutput *csprng = FortunaFactory::GetLocalOutput();

	// Generate server seed
	csprng->Generate(context.response, KeyBytes);

	// Generate MAC
	Skein mac;
	mac.SetKey(&context.challenge_mac);
	mac.BeginMAC();
	mac.CrunchString("server-response");
	mac.Crunch(context.response, KeyBytes); // hash server seed
	mac.Crunch(output_OOB_data, output_OOB_bytes);
	mac.End();

	// Write MAC to output
	mac.Generate(context.response + KeyBytes, KeyBytes);

	// Key the session
	session->SetKey(KeyBytes, context.shared_secret, input, context.response, false);

	context.bytes = KeyBytes * 2;
}
