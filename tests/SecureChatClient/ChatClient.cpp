#include <cat/sphynx/Wrapper.hpp>
#include <conio.h> // kbhit()
using namespace cat;
using namespace sphynx;

#include <fstream>
using namespace std;


class GameClient : public Client
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
	CAT_INLINE const char *GetRefObjectName() { return "GameClient"; }

	virtual void OnConnectFail(sphynx::SphynxError err)
	{
		CAT_WARN("Client") << "-- CONNECT FAIL ERROR " << GetSphynxErrorString(err);
	}
	virtual void OnConnect()
	{
		CAT_WARN("Client") << "-- CONNECTED";

		if (_fsource.TransferFile(GetWorkerID(), OP_FILE_UPLOAD_START, "test.tmp", "sink.tmp", this))
		{
			CAT_WARN("Client") << "-- File upload starting";
		}
		else
		{
			CAT_WARN("Client") << "-- File upload FAILED";
		}

		//u8 test_msg[50000];
		//WriteReliable(STREAM_UNORDERED, OP_TEST_FRAGMENTS, test_msg, sizeof(test_msg));
	}
	virtual void OnMessages(IncomingMessage msgs[], u32 count)
	{
		for (u32 ii = 0; ii < count; ++ii)
		{
			BufferStream msg = msgs[ii].data;
			u32 bytes = msgs[ii].bytes;

			if (msgs[ii].huge_fragment)
			{
				CAT_WARN("Client") << "Huge read stream " << msgs[ii].stream << " of size = " << bytes;

				_fsink.OnReadHuge(msgs[ii].stream, msg, bytes);
			}
			else
			switch (msg[0])
			{
			case OP_TEST_FRAGMENTS:
				CAT_WARN("Client") << "Successfully received test fragments";
				break;
			case OP_FILE_UPLOAD_START:
				if (_fsink.OnFileStart(GetWorkerID(), msg, bytes))
				{
					CAT_WARN("Client") << "-- File upload from remote peer starting";
				}
				else
				{
					CAT_WARN("Client") << "-- File upload from remote peer NOT ACCEPTED";
				}
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
		//WARN("Client") << "-- TICK " << now;
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

	for (int ii = 0; ii < 10; ++ii)
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
