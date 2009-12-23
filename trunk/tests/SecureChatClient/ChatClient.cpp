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

	unsigned char SERVER_PUBLIC_KEY[64] = {
		47,67,32,200,135,167,37,58,82,175,176,37,23,62,129,149,
		152,8,128,185,227,150,89,116,9,161,104,160,33,36,209,89,
		146,27,5,89,150,221,179,30,248,172,161,116,207,94,40,252,
		195,126,124,188,27,221,133,19,99,196,60,204,12,130,176,29
	};


	{
		ThreadPoolLocalStorage tls;

		for (int ii = 0; ii < 1500; ++ii)
		{
			sphynx::Client *client = new sphynx::Client;

			if (!client->SetServerKey(&tls, SERVER_PUBLIC_KEY, sizeof(SERVER_PUBLIC_KEY)))
			{
				FATAL("Client") << "Provided server key invalid";
			}

			if (!client->Connect("127.0.0.1", 22000))
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
