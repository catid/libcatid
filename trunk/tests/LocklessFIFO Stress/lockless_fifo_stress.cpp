/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
    Unit test for lockless FIFO
*/

#include <cat/AllFramework.hpp>
#include <iostream>
#include <conio.h> // getch()
using namespace std;
using namespace cat;

class Job : public LoopThread
{
public:
	bool ThreadFunction(void *)
	{
		char message[3000];

		int ii = 0;

		while (WaitForQuitSignal())
		{
			message[ii++] = 'A';
			message[ii] = '\0';

			if (ii == sizeof(message)-1)
				ii = 0;

			INFO("Job") << message;
		}

		return true;
	}

	Job()
	{
		if (!StartThread())
		{
			FATAL("Job") << "Unable to start thread!";
		}
	}

	~Job()
	{
		if (!StopThread())
		{
			FATAL("Job") << "Unable to stop thread!";
		}
	}
};

void handle_job_message(const char *severity, const char *source, region_ostringstream &msg)
{
}

int main(int argc, const char **argv)
{
    if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

    INFO("Test") << "** Press any key to begin.";

	while (!getch())
		Sleep(100);

	Logging::ref()->SetLogCallback(handle_job_message);

	{
		const int JOB_COUNT = 256;
		Job jobs[JOB_COUNT];

		while (!getch())
			Sleep(100);
	}

	while (!getch())
		Sleep(100);

	ShutdownFramework(true);

    return 0;
}
