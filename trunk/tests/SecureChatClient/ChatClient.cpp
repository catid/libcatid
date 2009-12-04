#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;


class ChatClient : public UDPEndpoint
{
	bool seen_first;

public:
	IP serverIP;
	Port serverPort;

	ChatClient()
	{
		seen_first = false;

	    if (!Bind(0, false))
		{
			WARN("Client") << "Unable to bind to a random port";

			return;
		}
	}

	~ChatClient()
	{
		return;
	}

protected:
    virtual void OnRead(IP srcIP, Port srcPort, u8 *data, u32 bytes)
	{
		if (!seen_first)
		{
			seen_first = true;
			IgnoreUnreachable();
		}

		//INANE("Client") << "read " << bytes;
	}

    virtual void OnWrite(u32 bytes)
	{
		//INANE("Client") << "wrote " << bytes;
		u8 *data = GetPostBuffer(1600);
		//memset(data, 1, 1600);
		Post(serverIP, serverPort, data, 1600);
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

	client->serverIP = ResolveHostname("127.0.0.1");
	client->serverPort = 80;

	{
		u8 *data = GetPostBuffer(1600);
		//memset(data, 1, 1600);
		client->Post(client->serverIP, client->serverPort, data, 1600);
	}

	{
		u8 *data = GetPostBuffer(1600);
		//memset(data, 1, 1600);
		client->Post(client->serverIP, client->serverPort, data, 1600);
	}

	{
		u8 *data = GetPostBuffer(1600);
		//memset(data, 1, 1600);
		client->Post(client->serverIP, client->serverPort, data, 1600);
	}

	{
		u8 *data = GetPostBuffer(1600);
		//memset(data, 1, 1600);
		client->Post(client->serverIP, client->serverPort, data, 1600);
	}

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	ShutdownFramework(true);

	return 0;
}
