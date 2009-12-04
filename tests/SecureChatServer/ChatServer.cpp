#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;


template<class T> class IPMap
{
public:
	IPMap()
	{
	}

	~IPMap()
	{
	}
};


class ChatSheep
{
	ChatServer *_server;
	IP _ip;
	Port _port;

public:
	ChatSheep(ChatServer *server, IP ip, Port port)
	{
		_server = server;
		_ip = ip;
		_port = port;
	}

	void Post(u8 *data, u32 bytes)
	{
		u8 *buffer = GetPostBuffer(bytes);

		if (buffer)
		{
			memcpy(buffer, data, bytes);

			_server->Post(_ip, _port, buffer, bytes);
		}
	}
};


class ChatServer : public UDPEndpoint
{
public:
	ChatServer()
	{
	    if (!Bind(80))
		{
			FATAL("Server") << "Unable to bind to port";

			return;
		}
	}

protected:
    virtual void OnRead(IP srcIP, Port srcPort, u8 *data, u32 bytes)
	{
		INANE("Server") << "read " << bytes;

		u8 *response = GetPostBuffer(bytes);

		memcpy(response, data, bytes);

		Post(srcIP, srcPort, response, bytes);
	}

    virtual void OnWrite(u32 bytes)
	{
		INANE("Server") << "wrote " << bytes;
	}

    virtual void OnClose()
	{
		INFO("Server") << "CONNECTION TERMINATED";
	}

    virtual void OnUnreachable(IP srcIP)
	{
		WARN("Server") << "DETINATION UNREACHABLE";
	}
};

int main()
{
	RegionAllocator::ref();
	Logging::ref();
	SocketManager::ref();

	INFO("Client") << "Secure Chat Server 1.0";

	SocketManager::ref()->Startup();

	ChatServer *server = new ChatServer;

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	server->ReleaseRef();

	SocketManager::ref()->Shutdown();

	return 0;
}
