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
		OP_FILE_UPLOAD_START,
		OP_TEST_FRAGMENTS,
		OP_USER_JOIN,
		OP_USER_PART
	};

public:
	virtual void OnShutdownRequest();
	virtual bool OnZeroReferences();
	virtual void OnConnect(SphynxTLS *tls);
	virtual void OnMessages(SphynxTLS *tls, IncomingMessage msgs[], u32 count);
	virtual void OnDisconnectReason(u8 reason);
	virtual void OnTick(SphynxTLS *tls, u32 now);
};

class GameServer : public Server
{
	friend class GameConnexion;

	Collexion<GameConnexion> _collexion;

protected:
	virtual void OnShutdownRequest();
	virtual bool OnZeroReferences();
	virtual Connexion *NewConnexion();
	virtual bool AcceptNewConnexion(const NetAddr &src);
};


//// GameConnexion

void GameConnexion::OnShutdownRequest()
{
	WARN("Connexion") << "-- Shutdown Requested";

	GetServer<GameServer>()->_collexion.Remove(this);

	Connexion::OnShutdownRequest();
}

bool GameConnexion::OnZeroReferences()
{
	WARN("Connexion") << "-- Zero References";

	return Connexion::OnZeroReferences();
}

void GameConnexion::OnConnect(SphynxTLS *tls)
{
	WARN("Connexion") << "-- CONNECTED";

	//u8 test_msg[50000];
	//WriteReliable(STREAM_UNORDERED, OP_TEST_FRAGMENTS, test_msg, sizeof(test_msg));

	u16 key = getLE(GetKey());

	for (sphynx::CollexionIterator<GameConnexion> ii = GetServer<GameServer>()->_collexion; ii; ++ii)
		ii->WriteReliable(STREAM_1, OP_USER_JOIN, &key, sizeof(key));

	GetServer<GameServer>()->_collexion.Insert(this);
}

void GameConnexion::OnMessages(SphynxTLS *tls, IncomingMessage msgs[], u32 count)
{
	for (u32 ii = 0; ii < count; ++ii)
	{
		BufferStream msg = msgs[ii].data;
		u32 bytes = msgs[ii].bytes;

		if (msgs[ii].huge_fragment)
		{
			WARN("Connexion") << "Huge read stream " << msgs[ii].stream << " of size = " << bytes;

			_fsink.OnReadHuge(msgs[ii].stream, msg, bytes);
		}
		else
		switch (msg[0])
		{
		case OP_TEST_FRAGMENTS:
			WARN("Connexion") << "Successfully received test fragments";
			break;
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
			WARN("Connexion") << "-- Got unknown message with " << bytes << " bytes" << HexDumpString(msg, bytes);
		}
	}
}

void GameConnexion::OnDisconnectReason(u8 reason)
{
	WARN("Connexion") << "-- DISCONNECTED REASON " << (int)reason;

	u16 key = getLE(GetKey());
	for (sphynx::CollexionIterator<GameConnexion> ii = GetServer<GameServer>()->_collexion; ii; ++ii)
		ii->WriteReliable(STREAM_1, OP_USER_PART, &key, sizeof(key));
}

void GameConnexion::OnTick(SphynxTLS *tls, u32 now)
{
	//WARN("Connexion") << "-- TICK " << now;
}


//// GameServer

void GameServer::OnShutdownRequest()
{
	WARN("Server") << "-- Shutdown Requested";

	Server::OnShutdownRequest();
}

bool GameServer::OnZeroReferences()
{
	WARN("Server") << "-- Zero References";

	return Server::OnZeroReferences();
}

Connexion *GameServer::NewConnexion()
{
	WARN("Server") << "-- Allocating a new Connexion";

	return new GameConnexion;
}

bool GameServer::AcceptNewConnexion(const NetAddr &src)
{
	WARN("Server") << "-- Accepting a connexion from " << src.IPToString() << " : " << src.GetPort();

	return true; // allow all
}


//// Entrypoint

int main()
{
	if (!SphynxLayer::ref()->Startup("Server.cfg"))
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
	else if (!server->StartServer(&tls, SERVER_PORT, key_pair, "Chat"))
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

	SphynxLayer::ref()->Shutdown();

	return 0;
}
