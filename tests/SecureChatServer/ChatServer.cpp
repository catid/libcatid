#include <cat/AllSphynx.hpp>
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

	virtual void OnMessage(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes)
	{
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
	// Initialize system info
	InitializeSystemInfo();

	// Initialize clock subsystem
	if (!Clock::Initialize())
	{
		FatalStop("Clock subsystem failed to initialize");
	}

	// Initialize logging subsystem with INFO reporting level
	Logging::ref()->Initialize(LVL_INFO);
	//Logging::ii->EnableServiceMode("ChatServerService");

	// Initialize disk settings subsystem
	Settings::ref()->readSettingsFromFile("ChatServer.cfg");

	// Read logging subsystem settings
	Logging::ref()->ReadSettings();

	// Start the CSPRNG subsystem
	if (!FortunaFactory::ref()->Initialize())
	{
		FatalStop("CSPRNG subsystem failed to initialize");
	}

	// Start the socket subsystem
	if (!StartupSockets())
	{
		FatalStop("Socket subsystem failed to initialize");
	}

	// Start the IO threads
	IOThreads io_threads;

	if (!io_threads.Startup())
	{
		FatalStop("IOThreads subsystem failed to initialize");
	}

	// Start the Worker threads
	WorkerThreads worker_threads;

	if (!worker_threads.Startup())
	{
		FatalStop("WorkerThreads subsystem failed to initialize");
	}

	INFO("Server") << "Secure Chat Server 2.0";

	GameServer *server = new GameServer;
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

	// TODO: Need to do some kind of shutdown code here

	// Terminate Worker threads
	worker_threads.Shutdown();

	// Terminate IO threads
	io_threads.Shutdown();

	// Terminate sockets
	CleanupSockets();

	// Terminate the entropy collection thread in the CSPRNG
	FortunaFactory::ref()->Shutdown();

	// Write settings to disk
	Settings::ref()->write();

	// Cleanup clock subsystem
	Clock::Shutdown();

	return 0;
}
