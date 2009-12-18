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
	83,150,130,26,45,236,186,31,139,86,20,93,248,156,146,27,
	9,76,3,182,193,0,216,58,182,161,232,63,192,83,191,160,
	62,155,119,200,204,125,200,214,28,203,137,109,91,104,155,105,
	166,154,226,115,221,181,146,247,140,100,162,71,119,165,182,121
	};

	{
		ThreadPoolLocalStorage tls;

		for (int ii = 0; ii < 1500; ++ii)
		{
			sphynx::Client *client = new sphynx::Client;

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
