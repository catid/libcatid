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

	IP ip = ResolveHostname("localhost");

	unsigned char SERVER_PUBLIC_KEY[64] = {
		22,199,117,20,222,42,234,236,255,135,146,220,155,171,216,234,
		101,237,77,128,48,105,15,18,23,27,238,43,94,231,56,230,
		19,195,97,48,6,29,244,217,246,231,243,243,201,26,176,190,
		175,110,168,206,18,8,177,122,129,189,48,39,177,200,114,76
	};

	{
		ThreadPoolLocalStorage tls;

		for (int ii = 0; ii < 1500; ++ii)
		{
			ScalableClient *client = new ScalableClient;

			if (!client->Connect(&tls, ip, SERVER_PUBLIC_KEY, sizeof(SERVER_PUBLIC_KEY)))
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
