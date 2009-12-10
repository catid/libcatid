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

	ScalableClient *client = new ScalableClient;

	IP ip = ResolveHostname("localhost");

	unsigned char SERVER_PUBLIC_KEY[64] = {
		226,221,230,114,71,187,214,142,227,67,68,202,247,8,76,189,
		184,21,247,1,167,15,123,128,76,228,29,110,139,226,96,182,
		207,146,240,255,172,120,251,253,10,194,213,232,200,130,248,52,
		234,70,119,124,168,97,101,81,38,243,64,207,249,171,187,39
	};

	{
		ThreadPoolLocalStorage tls;

		if (!client->Connect(&tls, ip, SERVER_PUBLIC_KEY, sizeof(SERVER_PUBLIC_KEY)))
		{
			FATAL("Client") << "Unable to connect to server";
		}
		else
		{
			while (!kbhit())
			{
				Clock::sleep(100);
			}
		}
	}

	ShutdownFramework(true);

	return 0;
}
