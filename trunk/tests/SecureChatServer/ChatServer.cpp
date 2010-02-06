#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;

class GameConnexion : public sphynx::Connexion
{
public:
	virtual void OnConnect(ThreadPoolLocalStorage *tls)
	{
		WARN("Connexion") << "-- CONNECTED";
	}
	virtual void OnDestroy()
	{
		WARN("Connexion") << "-- DESTROYED";
	}
	virtual void OnDisconnect()
	{
		WARN("Connexion") << "-- DISCONNECTED";
	}
	virtual void OnMessage(ThreadPoolLocalStorage *tls, u8 *msg, u32 bytes)
	{
		WARN("Connexion") << "Got message with " << bytes << " bytes";
	}
	virtual void OnTick(ThreadPoolLocalStorage *tls, u32 now)
	{
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

	INFO("Server") << "Secure Chat Server 1.0";

	GameServer *server = new GameServer();
	const Port SERVER_PORT = 22000;

	{
		ThreadPoolLocalStorage tls;
		u8 public_key[sphynx::PUBLIC_KEY_BYTES];
		u8 private_key[sphynx::PRIVATE_KEY_BYTES];

		const char *SessionKey = "Chat";

		if (!sphynx::Server::GenerateKeyPair(&tls, "PublicKeyFile.txt", "PrivateKeyFile.bin", public_key, sizeof(public_key), private_key, sizeof(private_key)))
		{
			FATAL("Server") << "Unable to get key pair";
		}
		else if (!server->Initialize(&tls, SERVER_PORT, public_key, sizeof(public_key), private_key, sizeof(private_key), SessionKey))
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
