#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;


class ChatClient : public UDPEndpoint
{
	bool seen_first;

public:
	ChatClient()
	{
		seen_first = false;

	    if (!Bind(0, false))
		{
			WARN("Client") << "Unable to bind to a random port";

			return;
		}
	}

protected:
    virtual void OnRead(IP srcIP, Port srcPort, u8 *data, u32 bytes)
	{
		if (!seen_first)
		{
			seen_first = true;
			IgnoreUnreachable();
		}

		INANE("Client") << "read " << bytes;
	}

    virtual void OnWrite(u32 bytes)
	{
		INANE("Client") << "wrote " << bytes;
	}

    virtual void OnClose()
	{
		INFO("Client") << "CONNECTION TERMINATED";
	}

    virtual void OnUnreachable(IP srcIP)
	{
		INFO("Client") << "DESTINATION UNREACHABLE";

		Close();
	}
};


int main()
{
	if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Client") << "Secure Chat Client 1.0";

	ChatClient *client = new ChatClient;

	while (!kbhit())
	{
		u8 *data = GetPostBuffer(64);

		memset(data, 1, 64);

		client->Post(ResolveHostname("127.0.0.1"), 80, data, 64);

		Clock::sleep(100);
	}

	client->ReleaseRef();

	ShutdownFramework(true);

	return 0;
}
