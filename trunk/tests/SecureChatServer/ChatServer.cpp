#include <cat/AllSphynx.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

class GameConnexion : public Connexion
{
public:
	virtual void OnShutdownRequest()
	{
		WARN("Connexion") << "-- Shutdown Requested";

		Connexion::OnZeroReferences();
	}
	virtual bool OnZeroReferences()
	{
		WARN("Connexion") << "-- Zero References";

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

			//INFO("Connexion") << "-- Got message with " << bytes << " bytes" << HexDumpString(msg, min(16, bytes));

			switch (msg[0])
			{
			case 0:
				//WriteReliable(STREAM_BULK, 0);
				{
					WARN("Connexion") << "-- Got request for transmit";
					static char STR[65534];
					for (int ii = 0; ii < sizeof(STR); ++ii)
						STR[ii] = (char)ii/(4000/256);
					WriteReliable(STREAM_BULK, 0, STR, sizeof STR);
				}
				break;
			case 2:
				WriteReliable(STREAM_1, 0);
			}
		}
	}
	virtual void OnPartialHuge(StreamMode stream, BufferStream data, u32 size)
	{
		WARN("Connexion") << "Got partial huge with " << size;
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
		INFO("Server") << "Press a key to terminate";

		while (!kbhit())
		{
			Clock::sleep(100);
		}
	}

	layer.Shutdown();

	return 0;
}
