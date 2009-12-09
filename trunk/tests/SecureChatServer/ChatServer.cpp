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


CAT_INLINE u32 hash_addr(IP ip, Port port)
{
	u32 hash = ip;

	// xorshift(a=5,b=17,c=13) with period 2^32-1:
	hash ^= hash << 13;
	hash ^= hash >> 17;
	hash ^= hash << 5;

	// Add the port into the hash
	hash += port;

	// xorshift(a=3,b=13,c=7) with period 2^32-1:
	hash ^= hash << 3;
	hash ^= hash >> 13;
	hash ^= hash << 7;

	return hash;
}

int main()
{
	if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Server") << "Secure Chat Server 1.0";

	u32 hist[10000];
	CAT_OBJCLR(hist);

	for (u32 ii = 0; ii < 1000000; ++ii)
	{
		hist[hash_addr(ii, 6000 + (Port)ii % 100) % 10000]++;
	}

	for (u32 ii = 0; ii < 10000; ++ii)
	{
		INFO("Server") << ii << " -> " << hist[ii];
	}

	ChatServer *client = new ChatServer;

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	ShutdownFramework(true);

	return 0;
}
