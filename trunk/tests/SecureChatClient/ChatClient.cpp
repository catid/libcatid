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
	sphynx::IOLayer iolayer;

	iolayer.Startup("ChatClient.cfg");

	INFO("Client") << "Secure Chat Client 2.0";

	GameClient *client;

	{
		// TODO: Make the crypto stuff easier to use in this context
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
		iolayer.Watch(client);

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

	iolayer.Shutdown();

	return 0;
}
