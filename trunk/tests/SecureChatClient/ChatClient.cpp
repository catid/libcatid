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

	NetAddr server_addr("127.0.0.1", 22000);

	if (!server_addr.Valid())
	{
		FatalStop("Invalid address specified for server");
	}

	unsigned char SERVER_PUBLIC_KEY[64] = {
		87,147,94,32,179,79,225,106,189,138,1,75,159,26,70,47,
		233,255,228,177,23,40,61,255,25,202,211,179,115,93,31,26,
		157,251,120,9,144,95,160,129,83,243,35,210,251,255,151,96,
		231,89,181,215,21,217,200,37,32,113,163,102,190,118,102,118
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
