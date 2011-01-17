#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;

#include <fstream>
using namespace std;


class GameClient : public sphynx::Client
{
public:
	virtual void OnClose()
	{
		WARN("Connexion") << "-- CLOSED";
	}

	virtual void OnConnectFail(sphynx::HandshakeError err)
	{
		WARN("Connexion") << "-- CONNECT FAIL ERROR " << sphynx::GetHandshakeErrorString(err);
	}

	virtual void OnConnect(ThreadPoolLocalStorage *tls)
	{
		WARN("Connexion") << "-- CONNECTED";
	}

	virtual void OnMessage(ThreadPoolLocalStorage *tls, BufferStream msg, u32 bytes)
	{
		WARN("Connexion") << "Got message with " << bytes << " bytes";
	}

	virtual void OnDisconnect(u8 reason)
	{
		WARN("Connexion") << "-- DISCONNECTED REASON " << (int)reason;
	}
	virtual void OnTick(ThreadPoolLocalStorage *tls, u32 now)
	{
		//WARN("Connexion") << "-- TICK " << now;
	}
};

int main()
{
	if (!InitializeFramework("ChatClient.txt"))
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Client") << "Secure Chat Client 1.1";

	{
		ThreadPoolLocalStorage tls;

		ifstream keyfile("PublicKeyFile.txt");

		if (!keyfile)
		{
			FATAL("Client") << "Unable to open public key file";
		}

		u8 server_public_key[sphynx::PUBLIC_KEY_BYTES];

		string key_str;
		keyfile >> key_str;

		if (ReadBase64(key_str.c_str(), (int)key_str.length(), server_public_key, sizeof(server_public_key)) != sizeof(server_public_key))
		{
			FATAL("Client") << "Public key from file is wrong length";
		}

		for (int ii = 0; ii < 1; ++ii)
		{
			GameClient *client = new GameClient;

			const char *SessionKey = "Chat";

			if (!client->SetServerKey(&tls, server_public_key, sizeof(server_public_key), SessionKey))
			{
				FATAL("Client") << "Provided server key invalid";
			}

			// 10.1.1.146
			if (!client->Connect("68.84.166.22", 22000))
			{
				FATAL("Client") << "Unable to connect to server";
			}
		}

		while (!kbhit())
		{
			Clock::sleep(100);
		}
	}

	ShutdownFramework(true);

	return 0;
}
