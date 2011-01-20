#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

class GameConnexion : public Connexion
{
public:
	virtual void OnConnect(ThreadPoolLocalStorage *tls)
	{
		WARN("Connexion") << "-- CONNECTED";

		WriteReliable(STREAM_1, 0);
	}

	virtual void OnDisconnect(u8 reason)
	{
		WARN("Connexion") << "-- DISCONNECTED REASON " << (int)reason;
	}

	virtual void OnTick(ThreadPoolLocalStorage *tls, u32 now)
	{
		//WARN("Connexion") << "-- TICK " << now;
	}

	virtual void OnMessage(ThreadPoolLocalStorage *tls, BufferStream msg, u32 bytes)
	{
		switch (msg[0])
		{
		case 0:
			{
				WARN("Connexion") << "Got request for transmit";
				static const char STR[4000];
				WriteReliable(STREAM_1, 0, STR, sizeof(STR));
			}
			break;
		default:
			WARN("Connexion") << "Got message with " << bytes << " bytes";
		}
	}

	virtual void OnDestroy()
	{
		WARN("Connexion") << "-- DESTROYED";
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
	if (!InitializeFramework("ChatServer.txt"))
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Server") << "Secure Chat Server 1.1";

	GameServer *server = new GameServer();
	const Port SERVER_PORT = 22000;

	{
		ThreadPoolLocalStorage tls;
		u8 public_key[sphynx::PUBLIC_KEY_BYTES];
		u8 private_key[sphynx::PRIVATE_KEY_BYTES];

		const char *SessionKey = "Chat";

		if (!Server::GenerateKeyPair(&tls, "PublicKeyFile.txt", "PrivateKeyFile.bin", public_key, sizeof(public_key), private_key, sizeof(private_key)))
		{
			FATAL("Server") << "Unable to get key pair";
		}
		else if (!server->StartServer(&tls, SERVER_PORT, public_key, sizeof(public_key), private_key, sizeof(private_key), SessionKey))
		{
			FATAL("Server") << "Unable to initialize";
		}
		else
		{
			while (!kbhit())
			{
				Clock::sleep(100);
			}
		}
	}

	ShutdownFramework(true);

	return 0;
}
