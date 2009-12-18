#include <cat/AllFramework.hpp>
#include <conio.h> // kbhit()
using namespace cat;

int main()
{
	if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

	INFO("Server") << "Secure Chat Server 1.0";

	sphynx::Server *endpoint = new sphynx::Server();
	const Port SERVER_PORT = 22000;

	{
		ThreadPoolLocalStorage tls;

		if (!endpoint->Initialize(&tls, SERVER_PORT))
		{
			FATAL("Server") << "Unable to initialize";
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
