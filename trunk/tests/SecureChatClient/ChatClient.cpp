#include <cat/AllSphynx.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

#include <fstream>
using namespace std;


class GameClient : public Client
{
public:
	virtual void OnClose()
	{
		WARN("Client") << "-- CLOSED";
	}

	virtual void OnConnectFail(HandshakeError err)
	{
		WARN("Client") << "-- CONNECT FAIL ERROR " << GetHandshakeErrorString(err);
	}

	virtual void OnConnect(ThreadPoolLocalStorage *tls)
	{
		WARN("Client") << "-- CONNECTED";
	}

	virtual void OnMessage(ThreadPoolLocalStorage *tls, u32 send_time, u32 recv_time, BufferStream msg, u32 bytes)
	{
		switch (msg[0])
		{
		case 0:
			{
				WARN("Client") << "Got request for transmit";

				static char STR[4000];

				for (int ii = 0; ii < sizeof(STR); ++ii)
					STR[ii] = (char)ii;

				for (int jj = 0; jj < 10; ++jj)
					WriteReliable(STREAM_UNORDERED, 1, STR, sizeof(STR)/4);
				for (int jj = 0; jj < 1000; ++jj)
					WriteReliable(STREAM_1, 1, STR, sizeof(STR));
				for (int jj = 0; jj < 1000; ++jj)
					WriteReliable(STREAM_2, 1, STR, sizeof(STR));
				WriteReliable(STREAM_2, 2, STR, sizeof(STR));

				WriteReliable(STREAM_3, 0, STR, sizeof(STR));
			}
			break;
		default:
			INFO("Client") << "Got message with " << bytes << " bytes";
		}
	}

	virtual void OnDisconnect(u8 reason)
	{
		WARN("Client") << "-- DISCONNECTED REASON " << (int)reason;
	}
	virtual void OnTick(ThreadPoolLocalStorage *tls, u32 now)
	{
		//WARN("Client") << "-- TICK " << now;
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
	//Logging::ii->EnableServiceMode("ChatClientService");

	// Initialize disk settings subsystem
	Settings::ref()->readSettingsFromFile("ChatClient.cfg");

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

	// Watcher for shutdown events
	RefObjectWatcher watcher;

	INFO("Client") << "Secure Chat Client 2.0";

	GameClient *client;

	{
		ThreadPoolLocalStorage tls;

		ifstream keyfile("PublicKeyFile.txt");

		if (!keyfile)
		{
			FATAL("Client") << "Unable to open public key file";
		}

		u8 server_public_key[PUBLIC_KEY_BYTES];

		string key_str;
		keyfile >> key_str;

		if (ReadBase64(key_str.c_str(), (int)key_str.length(), server_public_key, sizeof(server_public_key)) != sizeof(server_public_key))
		{
			FATAL("Client") << "Public key from file is wrong length";
		}

		client = new GameClient;
		watcher.Watch(client);

		const char *SessionKey = "Chat";

		if (!client->SetServerKey(&tls, server_public_key, sizeof(server_public_key), SessionKey))
		{
			FATAL("Client") << "Provided server key invalid";
		}

		// loopback: 127.0.0.1
		// desktop: 10.1.1.142
		// linux: 10.1.1.146
		// netbook: 10.1.1.110
		// coldfront: 68.84.166.22
		if (!client->Connect("68.84.166.22", 22000))
		{
			FATAL("Client") << "Unable to connect to server";
		}

		while (!kbhit())
		{
			Clock::sleep(100);
		}
	}

	if (!watcher.WaitForShutdown())
	{
		WARN("ChatClient") << "Wait for shutdown expired";
	}

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
