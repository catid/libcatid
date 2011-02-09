#include <cat/AllSphynx.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

class GameConnexion : public Connexion
{
public:
	virtual void OnShutdownRequest()
	{
		WARN("Client") << "-- Shutdown Requested";

		Connexion::OnZeroReferences();
	}
	virtual bool OnZeroReferences()
	{
		WARN("Client") << "-- Zero References";

		return Connexion::OnZeroReferences();
	}

	virtual void OnConnect(SphynxTLS *tls)
	{
		WARN("Connexion") << "-- CONNECTED";

		WriteReliable(STREAM_1, 0);
	}
	virtual void OnMessages(SphynxTLS *tls, IncomingMessage msgs[], u32 count)
	{
		for (u32 ii = 0; ii < count; ++ii)
		{
			BufferStream msg = msgs[ii].msg;
			u32 bytes = msgs[ii].bytes;
			u32 send_time = msgs[ii].send_time;

			switch (msg[0])
			{
			case 0:
				{
					INFO("Connexion") << "Got request for transmit";
					static char STR[4000];
					for (int ii = 0; ii < sizeof(STR); ++ii)
						STR[ii] = (char)ii/(4000/256);
				}
				break;
			case 2:
				WriteReliable(STREAM_1, 0);
			default:
				INFO("Connexion") << "Got message with " << bytes << " bytes";
			}
		}
	}
	virtual void OnDisconnectReason(u8 reason)
	{
		WARN("Connexion") << "-- DISCONNECTED REASON " << (int)reason;
	}
	virtual void OnTick(SphynxTLS *tls, u32 now)
	{
		//WARN("Connexion") << "-- TICK " << now;
	}
};

class GameServer : public sphynx::Server
{
public:
	virtual sphynx::Connexion *NewConnexion()
	{
		return new GameConnexion;
	}
	virtual bool AcceptNewConnexion(const NetAddr &src)
	{
		return true; // allow all
	}
};

int main()
{
	SphynxLayer layer;

	if (!layer.Startup("Server.cfg"))
	{
		FATAL("Server") << "Unable to initialize SphynxLayer";
		return 1;
	}

	INFO("Server") << "Secure Chat Server 2.0";

	GameServer *server = new GameServer;
	const Port SERVER_PORT = 22000;

	SphynxTLS tls;

	TunnelKeyPair key_pair;

	if (!Server::InitializeKey(&tls, key_pair, "KeyPair.bin", "PublicKey.bin"))
	{
		FATAL("Server") << "Unable to get key pair";
	}
	else if (!server->StartServer(&layer, &tls, SERVER_PORT, key_pair, "Chat"))
	{
		FATAL("Server") << "Unable to start server";
	}
	else
	{
		INFO("Client") << "Press a key to terminate";

		while (!kbhit())
		{
			Clock::sleep(100);
		}
	}

	layer.Shutdown();

	return 0;
}
