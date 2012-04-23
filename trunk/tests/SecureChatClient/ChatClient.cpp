#include <cat/sphynx/Wrapper.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

#include <fstream>
using namespace std;


class GameClient : public Client
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
	CAT_INLINE const char *GetRefObjectName() { return "GameClient"; }

	virtual void OnConnectFail(sphynx::SphynxError err)
	{
		CAT_WARN("Client") << "-- CONNECT FAIL ERROR " << GetSphynxErrorString(err);
	}
	virtual void OnConnect()
	{
		CAT_WARN("Client") << "-- CONNECTED";

		_ft.Initialize(this, OP_FTP);
		_huge_endpoint = &_ft;

		_ft.Request("ChatServer.cpp");

/*
		if (_fsource.TransferFile(GetWorkerID(), OP_FILE_UPLOAD_START, "test.tmp", "sink.tmp", this))
		{
			CAT_WARN("Client") << "-- File upload starting";
		}
		else
		{
			CAT_WARN("Client") << "-- File upload FAILED";
		}*/

		u8 test_msg[50000];
		Abyssinian prng;
		prng.Initialize(0);
		for (int ii = 0; ii < sizeof(test_msg); ++ii)
			test_msg[ii] = (u8)(prng.Next() % 10);
		//WriteReliable(STREAM_2, OP_TEST_FRAGMENTS, test_msg, sizeof(test_msg));
	}
	virtual void OnMessages(IncomingMessage msgs[], u32 count)
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
					CAT_WARN("Client") << "TEST FAIL : Length doesn't match expectation";
				}
				else
				{
					Abyssinian prng;
					prng.Initialize(1);

					for (u32 ii = 1; ii < bytes; ++ii)
					{
						if (msg[ii] != (u8)(prng.Next() % 10))
						{
							CAT_WARN("Client") << "TEST FAIL : Data mismatch =(";
						}
					}
					CAT_WARN("Client") << "Successfully received test fragments";


				}
				break;
			case OP_FTP:
				_ft.OnControlMessage(msg, bytes);
				break;
			case OP_USER_JOIN:
				CAT_WARN("Client") << "-- User joined: " << getLE(*(u16*)(msg + 1));
				break;
			case OP_USER_PART:
				CAT_WARN("Client") << "-- User quit: " << getLE(*(u16*)(msg + 1));
				break;
			default:
				CAT_WARN("Client") << "-- Got unknown message type " << (int)msg[0] << " with " << bytes << " bytes";
			}
		}
	}
	virtual void OnDisconnectReason(u8 reason)
	{
		CAT_WARN("Client") << "-- DISCONNECTED REASON " << (int)reason;
	}
	virtual void OnCycle(u32 now)
	{
		//CAT_WARN("Client") << "-- TICK " << now;
	}
};

int main(int argc, char *argv[])
{
	CAT_INFO("Client") << "Secure Chat Client 2.0";

	TunnelPublicKey public_key;

	if (!public_key.LoadFile("PublicKey.bin"))
	{
		CAT_FATAL("Client") << "Unable to load server public key from disk";
		return 1;
	}

	char *hostname = "127.0.0.1";
	static const int PORT = 22000;
	static const char *SESSION_KEY = "Chat";
	if (argc >= 2) hostname = argv[1];

	for (int ii = 0; ii < 1; ++ii)
	{
		GameClient *client;
		if (!RefObjects::Create(CAT_REFOBJECT_TRACE, client))
		{
			CAT_FATAL("Client") << "Unable to create game client object";
			return 2;
		}

		// loopback: 127.0.0.1
		// desktop: 10.1.1.142
		// linux: 10.1.1.146
		// netbook: 10.1.1.110
		// coldfront: 68.84.166.22
		// workstation: 10.15.40.161
		// Patrick: 10.15.40.77
		// stew 2 caws: 80.3.22.26

		if (!client->Connect(hostname, PORT, public_key, SESSION_KEY))
		{
			CAT_FATAL("Client") << "Unable to connect to server";
			return 3;
		}
	}

	CAT_INFO("Client") << "Press a key to terminate";

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	return 0;
}
