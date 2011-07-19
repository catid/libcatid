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
	virtual void OnDestroy();
	virtual bool OnFinalize();
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
	virtual void OnDestroy();
	virtual bool OnFinalize();
	virtual Connexion *NewConnexion();
	virtual bool AcceptNewConnexion(const NetAddr &src);
};


//// GameConnexion

void GameConnexion::OnDestroy()
{
	CAT_WARN("Connexion") << "-- Shutdown Requested";

	GetServer<GameServer>()->_collexion.Remove(this);

	Connexion::OnDestroy();
}

bool GameConnexion::OnFinalize()
{
	CAT_WARN("Connexion") << "-- Zero References";

	return Connexion::OnFinalize();
}

void GameConnexion::OnConnect(SphynxTLS *tls)
{
	CAT_WARN("Connexion") << "-- CONNECTED";

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
			CAT_INFO("Connexion") << "Huge read stream " << msgs[ii].stream << " of size = " << bytes;

			_fsink.OnReadHuge(msgs[ii].stream, msg, bytes);
		}
		else
		switch (msg[0])
		{
		case OP_TEST_FRAGMENTS:
			CAT_WARN("Connexion") << "Successfully received test fragments";
			break;
		case OP_FILE_UPLOAD_START:
			if (_fsink.OnFileStart(GetWorkerID(), msg, bytes))
			{
				CAT_WARN("Connexion") << "-- File upload from remote peer starting";
			}
			else
			{
				CAT_WARN("Connexion") << "-- File upload from remote peer NOT ACCEPTED";
			}
			break;
		default:
			CAT_WARN("Connexion") << "-- Got unknown message with " << bytes << " bytes" << HexDumpString(msg, bytes);
		}
	}
}

void GameConnexion::OnDisconnectReason(u8 reason)
{
	CAT_WARN("Connexion") << "-- DISCONNECTED REASON " << (int)reason;

	u16 key = getLE(GetKey());
	for (sphynx::CollexionIterator<GameConnexion> ii = GetServer<GameServer>()->_collexion; ii; ++ii)
		ii->WriteReliable(STREAM_1, OP_USER_PART, &key, sizeof(key));
}

void GameConnexion::OnTick(SphynxTLS *tls, u32 now)
{
	//WARN("Connexion") << "-- TICK " << now;
}


//// GameServer

void GameServer::OnDestroy()
{
	CAT_WARN("Server") << "-- Shutdown Requested";

	Server::OnDestroy();
}

bool GameServer::OnFinalize()
{
	CAT_WARN("Server") << "-- Zero References";

	return Server::OnFinalize();
}

Connexion *GameServer::NewConnexion()
{
	CAT_WARN("Server") << "-- Allocating a new Connexion";

	return new GameConnexion;
}

bool GameServer::AcceptNewConnexion(const NetAddr &src)
{
	CAT_WARN("Server") << "-- Accepting a connexion from " << src.IPToString() << " : " << src.GetPort();

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

	CAT_INFO("Server") << "Secure Chat Server 2.0";

	GameServer *server = new GameServer;
	const Port SERVER_PORT = 22000;

	SphynxTLS tls;

	TunnelKeyPair key_pair;

	if (!Server::InitializeKey(&tls, key_pair, "KeyPair.bin", "PublicKey.bin"))
	{
		CAT_FATAL("Server") << "Unable to get key pair";
	}
	else if (!server->StartServer(&tls, SERVER_PORT, key_pair, "Chat"))
	{
		CAT_FATAL("Server") << "Unable to start server";
	}
	else
	{
		CAT_INFO("Server") << "Press a key to terminate";

		while (!kbhit())
		{
			Clock::sleep(100);
		}
	}

	SphynxLayer::ref()->Shutdown();

	return 0;
}
