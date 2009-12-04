#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;


class ChatServer : public UDPEndpoint
{
	bool seen_first;
	u32 in_bytes;
	u32 milliseconds;

public:
	ChatServer()
	{
		in_bytes = 0;
		seen_first = false;
		milliseconds = Clock::msec();

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
	virtual void OnRead(IP srcIP, Port srcPort, u8 *data, u32 bytes)
	{
		if (!seen_first)
		{
			seen_first = true;
			IgnoreUnreachable();
		}

		u8 *response = GetPostBuffer(bytes);
		if (response)
		{
			memcpy(response, data, bytes);

			Post(srcIP, srcPort, response, bytes);
		}

		//INANE("Server") << "read " << bytes << " and " << (int)data << " from " << GetCurrentThreadId();

		if ((Atomic::Add(&in_bytes, bytes) % (1600 * 10000)) == 0)
		{
			u32 now = Clock::msec();

			INANE("Server") << "Read rate = " << in_bytes / (double)(now - milliseconds) / 1000.0 << " MB/s";

			milliseconds = now;
			Atomic::Set(&in_bytes, 1600);
		}
	}

	virtual void OnWrite(u32 bytes)
	{
		//INANE("Server") << "wrote " << bytes;
	}

	virtual void OnClose()
	{
		INFO("Server") << "CONNECTION TERMINATED";
	}

	virtual void OnUnreachable(IP srcIP)
	{
		INFO("Server") << "DESTINATION UNREACHABLE";

		Close();
	}
};


int main()
{
	if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Server") << "Secure Chat Server 1.0";

	ChatServer *client = new ChatServer;

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	ShutdownFramework(true);

	return 0;
}
