#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;


int main()
{
	if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Client") << "Secure Chat Client 1.0";

	sockaddr_in6 server_addr;

	if (!StringToAddress6(CAT_IP6_LOOPBACK, server_addr))
	{
		FatalStop("Invalid address specified for server");
	}

	unsigned char SERVER_PUBLIC_KEY[64] = {
		8,167,253,18,142,178,65,205,211,188,73,161,54,141,129,237,
		159,48,62,236,174,35,254,37,89,246,39,147,54,226,22,143,
		39,237,140,190,139,114,55,120,252,204,176,249,157,12,195,93,
		162,202,165,11,13,200,211,31,61,176,7,129,238,152,17,3
	};

	{
		ThreadPoolLocalStorage tls;

		for (int ii = 0; ii < 1500; ++ii)
		{
			ScalableClient *client = new ScalableClient;

			if (!client->Connect(&tls, server_addr, SERVER_PUBLIC_KEY, sizeof(SERVER_PUBLIC_KEY)))
			{
				FATAL("Client") << "Unable to connect to server";
			}
		}

		while (!kbhit())
		{
			Clock::sleep(100);
		}
	}

	ShutdownFramework(true);

	return 0;
}
