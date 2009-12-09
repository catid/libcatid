#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;


class ChatServer : public UDPEndpoint
{
public:
	ChatServer()
	{
		if (!Bind(80))
		{
			WARN("Server") << "Unable to bind to port 80";
			return;
		}
	}

	~ChatServer()
	{
		return;
	}

protected:
	virtual void OnRead(ThreadPoolLocalStorage *tls, IP srcIP, Port srcPort, u8 *data, u32 bytes)
	{
	}

	virtual void OnWrite(u32 bytes)
	{
	}

	virtual void OnClose()
	{
	}
};


int main()
{
	if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Server") << "Secure Chat Server 1.0";

	HandshakeEndpoint *endpoint = new HandshakeEndpoint();

	if (endpoint->Initialize())
	{
	}

	ChatServer *client = new ChatServer;

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	ShutdownFramework(true);

	return 0;
}
