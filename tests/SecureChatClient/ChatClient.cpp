#include <cat/AllSphynx.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

#include <fstream>
using namespace std;


class GameClient : public Client
{
public:
	virtual void OnShutdownRequest()
	{
		WARN("Client") << "-- Shutdown Requested";

		Client::OnShutdownRequest();
	}
	virtual bool OnZeroReferences()
	{
		WARN("Client") << "-- Zero References";

		return Client::OnZeroReferences();
	}
	virtual void OnConnectFail(sphynx::HandshakeError err)
	{
		WARN("Client") << "-- CONNECT FAIL ERROR " << GetHandshakeErrorString(err);
	}
	virtual void OnConnect(SphynxTLS *tls)
	{
		WARN("Client") << "-- CONNECTED";
	}
	virtual void OnMessages(SphynxTLS *tls, IncomingMessage msgs[], u32 count)
	{
		for (u32 ii = 0; ii < count; ++ii)
		{
			BufferStream msg = msgs[ii].msg;
			u32 bytes = msgs[ii].bytes;
			u32 send_time = msgs[ii].send_time;

			//INFO("Client") << "Got message with " << bytes << " bytes";

			switch (msg[0])
			{
			case 0:
				{
					WARN("Client") << "-- Got request for transmit";

					static char STR[65534];

					for (int ii = 0; ii < sizeof(STR); ++ii)
						STR[ii] = (char)ii;
/*
					for (int jj = 0; jj < 10; ++jj)
						WriteReliable(STREAM_UNORDERED, 1, STR, sizeof(STR)/4);
					for (int jj = 0; jj < 1000; ++jj)
						WriteReliable(STREAM_1, 1, STR, sizeof(STR));
					for (int jj = 0; jj < 1000; ++jj)
						WriteReliable(STREAM_2, 1, STR, sizeof(STR));
					WriteReliable(STREAM_2, 2, STR, sizeof(STR));
*/
					WriteReliable(STREAM_UNORDERED, 0, STR, sizeof(STR));

					for (int ii = 0; ii < 100; ++ii)
					{
						WriteReliable(STREAM_UNORDERED, 1, STR, sizeof(STR));
					}
				}
				break;
			}
		}
	}
	virtual void OnPartialHuge(u32 total_bytes, u32 offset, u32 size, BufferStream data)
	{

	}
	virtual void OnDisconnectReason(u8 reason)
	{
		WARN("Client") << "-- DISCONNECTED REASON " << (int)reason;
	}
	virtual void OnTick(SphynxTLS *tls, u32 now)
	{
		//WARN("Client") << "-- TICK " << now;
	}
};

int main()
{
	SphynxLayer layer;

	if (!layer.Startup("ChatClient.cfg"))
	{
		FATAL("Client") << "Unable to start IOLayer";
		return 1;
	}

	INFO("Client") << "Secure Chat Client 2.0";

	SphynxTLS tls;

	TunnelPublicKey public_key;

	if (!public_key.LoadFile("PublicKey.bin"))
	{
		FATAL("Client") << "Unable to load server public key from disk";
	}

	GameClient *client = new GameClient;

	// loopback: 127.0.0.1
	// desktop: 10.1.1.142
	// linux: 10.1.1.146
	// netbook: 10.1.1.110
	// coldfront: 68.84.166.22
	// workstation: 10.15.40.161
	// Patrick: 10.15.40.77
	if (!client->Connect(&layer, &tls, "68.84.166.22", 22000, public_key, "Chat"))
	{
		FATAL("Client") << "Unable to connect to server";
	}
	else
	{
		INFO("Client") << "Press a key to terminate";

		while (!kbhit())
		{
			Clock::sleep(100);
		}
	}

	layer.Shutdown();

	return 0;
}
