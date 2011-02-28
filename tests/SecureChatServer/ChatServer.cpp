#include <cat/AllSphynx.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

class GameConnexion : public Connexion
{
	FileTransferSource _fsource;
	FileTransferSink _fsink;

	enum
	{
		OP_FILE_UPLOAD_START
	};

public:
	virtual void OnShutdownRequest()
	{
		WARN("Connexion") << "-- Shutdown Requested";

		Connexion::OnShutdownRequest();
	}
	virtual bool OnZeroReferences()
	{
		WARN("Connexion") << "-- Zero References";

		return Connexion::OnZeroReferences();
	}
	virtual void OnConnect(SphynxTLS *tls)
	{
		WARN("Connexion") << "-- CONNECTED";

		if (_fsource.WriteFile(OP_FILE_UPLOAD_START, "source_file.txt", "sink_file.txt", this))
		{
			WARN("Connexion") << "-- File upload starting";
		}
		else
		{
			WARN("Connexion") << "-- File upload FAILED";
		}
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
			case OP_FILE_UPLOAD_START:
				if (_fsink.OnFileStart(msg, bytes))
				{
					WARN("Connexion") << "-- File upload from remote peer starting";
				}
				else
				{
					WARN("Connexion") << "-- File upload from remote peer NOT ACCEPTED";
				}
				break;
			default:
				WARN("Connexion") << "-- Got unknown message with " << bytes << " bytes" << HexDumpString(msg, min(16, bytes));
			}
		}
	}
	virtual void OnReadHuge(StreamMode stream, BufferStream data, u32 size)
	{
		WARN("Connexion") << "Huge read stream " << stream << " of size = " << size;

		_fsink.OnReadHuge(stream, data, size);
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

class GameServer : public Server
{
protected:
	virtual void OnShutdownRequest()
	{
		WARN("Server") << "-- Shutdown Requested";

		Server::OnShutdownRequest();
	}
	virtual bool OnZeroReferences()
	{
		WARN("Server") << "-- Zero References";

		return Server::OnZeroReferences();
	}
	virtual sphynx::Connexion *NewConnexion()
	{
		WARN("Server") << "-- Allocating a new Connexion";

		return new GameConnexion;
	}
	virtual bool AcceptNewConnexion(const NetAddr &src)
	{
		WARN("Server") << "-- Accepting a connexion from " << src.IPToString() << " : " << src.GetPort();

		return true; // allow all
	}
};

int main()
{
	SphynxLayer layer;

	if (!layer.Startup("Server.cfg"))
	{
		FatalStop("Unable to initialize framework!");
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
