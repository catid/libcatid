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
		OP_FILE_UPLOAD_START
	};

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

			switch (msg[0])
			{
			case OP_FILE_UPLOAD_START:
				if (_fsink.OnFileStart(msg, bytes))
				{
					WARN("Client") << "-- File upload from remote peer starting";
				}
				else
				{
					WARN("Client") << "-- File upload from remote peer NOT ACCEPTED";
				}
				break;
			default:
				WARN("Client") << "-- Got unknown message with " << bytes << " bytes" << HexDumpString(msg, min(16, bytes));
			}
		}
	}
	virtual void OnReadHuge(StreamMode stream, BufferStream data, u32 size)
	{
		WARN("Client") << "Huge read stream " << stream << " of size = " << size;

		_fsink.OnReadHuge(stream, data, size);
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

int main(int argc, char *argv[])
{
	SphynxLayer layer;

	if (!layer.Startup("Client.cfg"))
	{
		FatalStop("Unable to initialize framework!");
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
	// stew 2 caws: 80.3.22.26
	char *hostname = "127.0.0.1";
	if (argc >= 2) hostname = argv[1];

	if (!client->Connect(&layer, &tls, hostname, 22000, public_key, "Chat"))
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
