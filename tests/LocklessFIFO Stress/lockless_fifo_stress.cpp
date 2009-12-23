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

u32 data[64];
RWLock data_lock;

u32 read_ts;
u32 read_ctr;

u32 write_ts;
u32 write_ctr;

class ReadJob : public LoopThread
{
public:
	bool ThreadFunction(void *)
	{
		for (;;)
		{
			AutoReadLock lock(data_lock);

			u32 copy[64];

			memcpy(copy, data, sizeof(data));

			int x = 1;
			for (int ii = 0; ii < 1000; ++ii)
			{
				x *= copy[ii % 64];
			}

			if (memcmp(copy, data, sizeof(data)))
			{
				FATAL("Read") << "Write detected during read lock";
			}

			if (Atomic::Add(&read_ctr, 1) == 10000000)
			{
				u32 now = Clock::msec_fast();

				INFO("Test") << "Read throughput: " << (read_ctr / float(now - read_ts)) << "k/sec";

				read_ts = now;
				read_ctr = 0;
			}
		}

		return true;
	}

	ReadJob()
	{
		if (!StartThread())
		{
			FATAL("Job") << "Unable to start thread!";
		}
	}

	~ReadJob()
	{
		if (!StopThread())
		{
			FATAL("Job") << "Unable to stop thread!";
		}
	}
};

class WriteJob : public LoopThread
{
public:
	bool ThreadFunction(void *)
	{
		for (;;)
		{
			AutoWriteLock lock(data_lock);

			for (int ii = 0; ii < 64; ++ii)
			{
				data[ii]++;
			}

			if (Atomic::Add(&write_ctr, 1) == 1000000)
			{
				u32 now = Clock::msec_fast();

				INFO("Test") << "Write throughput: " << (write_ctr / float(now - write_ts)) << "k/sec";

				write_ts = now;
				write_ctr = 0;
			}
		}

		return true;
	}

	WriteJob()
	{
		if (!StartThread())
		{
			FATAL("Job") << "Unable to start thread!";
		}
	}

	~WriteJob()
	{
		if (!StopThread())
		{
			FATAL("Job") << "Unable to stop thread!";
		}
	}
};

int main(int argc, const char **argv)
{
    if (!InitializeFramework())
	{
		FatalStop("Unable to initialize framework!");
	}

    INFO("Test") << "** Press any key to begin.";

	while (!getch())
		Sleep(100);

	read_ctr = 0;
	write_ctr = 0;
	u32 ts = Clock::msec_fast();
	read_ts = ts;
	write_ts = ts;

	{
		const int READER_COUNT = 4;
		ReadJob read_jobs[READER_COUNT];

		const int WRITER_COUNT = 1;
		WriteJob write_jobs[WRITER_COUNT];

	    INFO("Test") << "** Test in progress.  Press any key to stop.";

		while (!getch())
			Sleep(100);
	}

    INFO("Test") << "** Test aborted.  Press any key to shutdown.";

	while (!getch())
		Sleep(100);

	ShutdownFramework(true);

    return 0;
}
