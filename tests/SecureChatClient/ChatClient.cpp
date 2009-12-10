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
		148,244,167,88,146,86,214,238,6,132,196,235,75,120,98,4,
		64,164,10,25,169,124,250,129,228,69,210,192,146,219,29,183,
		23,248,3,23,45,43,101,14,216,143,69,178,178,86,161,240,
		210,209,169,161,176,221,223,59,154,161,202,10,108,19,34,237
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
