#include <cat/AllSphynx.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

class GameConnexion : public Connexion
{
	FECHugeEndpoint _ft;

	enum
	{
		OP_FTP,
		OP_TEST_FRAGMENTS,
		OP_USER_JOIN,
		OP_USER_PART
	};

public:

	CAT_INLINE const char *GetRefObjectName() { return "GameConnexion"; }

	virtual void OnDestroy();
	virtual bool OnFinalize();
	virtual void OnConnect();
	virtual void OnMessages(IncomingMessage msgs[], u32 count);
	virtual void OnDisconnectReason(u8 reason);
	virtual void OnCycle(u32 now);
};

class GameServer : public Server
{
	friend class GameConnexion;

	Collexion<GameConnexion> _collexion;

protected:

	CAT_INLINE const char *GetRefObjectName() { return "GameServer"; }

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

void GameConnexion::OnConnect()
{
	_ft.Initialize(this, OP_FTP);
	_huge_endpoint = &_ft;

	CAT_WARN("Connexion") << "-- CONNECTED";

	u8 test_msg[50000];
	Abyssinian prng;
	prng.Initialize(1);
	for (int ii = 0; ii < (int)sizeof(test_msg); ++ii)
		test_msg[ii] = (u8)(prng.Next() % 10);
	//WriteReliable(STREAM_2, OP_TEST_FRAGMENTS, test_msg, sizeof(test_msg));

	u16 key = getLE(GetMyID());

	Collexion<GameConnexion> *user_list = &GetServer<GameServer>()->_collexion;

	Transport::BroadcastReliable(user_list, STREAM_1, OP_USER_JOIN, &key, sizeof(key));

	user_list->Insert(this);
}

void GameConnexion::OnMessages(IncomingMessage msgs[], u32 count)
{
	for (u32 ii = 0; ii < count; ++ii)
	{
		BufferStream msg = msgs[ii].data;
		u32 bytes = msgs[ii].bytes;

		switch (msg[0])
		{
		case OP_TEST_FRAGMENTS:
			if (bytes != 50000 + 1)
			{
				CAT_WARN("Connexion") << "TEST FAIL : Length doesn't match expectation";
			}
			else
			{
				Abyssinian prng;
				prng.Initialize(0);

				for (u32 ii = 1; ii < bytes; ++ii)
				{
					if (msg[ii] != (u8)(prng.Next() % 10))
					{
						CAT_WARN("Connexion") << "TEST FAIL : Data mismatch =(";
					}
				}
				CAT_WARN("Connexion") << "Successfully received test fragments";
			}
			break;
		case OP_FTP:
			_ft.OnControlMessage(msg, bytes);
			break;
		default:
			CAT_WARN("Connexion") << "-- Got unknown message with " << bytes << " bytes" << HexDumpString(msg, bytes);
		}
	}
}

void GameConnexion::OnDisconnectReason(u8 reason)
{
	CAT_WARN("Connexion") << "-- DISCONNECTED REASON " << (int)reason;

	u16 key = getLE(GetMyID());

	Collexion<GameConnexion> *user_list = &GetServer<GameServer>()->_collexion;

	Transport::BroadcastReliable(user_list, STREAM_1, OP_USER_PART, &key, sizeof(key));
}

void GameConnexion::OnCycle(u32 now)
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

	return RefObjects::Create<GameConnexion>(CAT_REFOBJECT_TRACE);
}

bool GameServer::AcceptNewConnexion(const NetAddr &src)
{
	CAT_WARN("Server") << "-- Accepting a connexion from " << src.IPToString() << " : " << src.GetPort();

	return true; // allow all
}


//// Entrypoint

int main()
{
	CAT_INFO("Server") << "Secure Chat Server 2.0";

	GameServer *server;
	if (!RefObjects::Create(CAT_REFOBJECT_TRACE, server))
	{
		CAT_FATAL("Server") << "Unable to acquire server object";
		return 0;
	}

	const Port SERVER_PORT = 22000;

	TunnelKeyPair key_pair;

	if (!Server::InitializeKey(key_pair, "KeyPair.bin", "PublicKey.bin"))
	{
		CAT_FATAL("Server") << "Unable to get key pair";
	}
	else if (!server->StartServer(SERVER_PORT, key_pair, "Chat"))
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

	return 0;
}
