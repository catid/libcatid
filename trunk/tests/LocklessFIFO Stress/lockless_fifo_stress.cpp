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

#include <cat/AllCommon.hpp>
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

struct TestData
{
	u8 data[1500];
};

FIFO::Queue<TestData> *q;

class ReadJob : public Thread
{
public:
	bool ThreadFunction(void *)
	{
		CAT_FOREVER
		{
			TestData *data = q->DequeueWait();

			if (!data)
			{
				CAT_WARN("Test") << "DequeueWait returned null";
				break;
			}

			// Process data here

			RegionAllocator::ii->Delete(data);

			if (Atomic::Add(&read_ctr, 1) == 100000)
			{
				u32 now = Clock::msec_fast();

				CAT_INFO("Test") << "Read throughput: " << (read_ctr / float(now - read_ts)) << "k/sec";

				read_ts = now;
				read_ctr = 0;
			}
		}

		CAT_WARN("Test") << "ReadJob terminated";
		return true;
	}

	ReadJob()
	{
		if (!StartThread())
		{
			CAT_FATAL("Job") << "Unable to start thread!";
		}
	}

	~ReadJob()
	{
		q->Enqueue(0);

		if (!WaitForThread(1000))
			AbortThread();
	}
};

class WriteJob : public Thread
{
	WaitableFlag _kill_flag;

public:
	bool ThreadFunction(void *)
	{
		while (!_kill_flag.Wait(0))
		{
			TestData *data = RegionAllocator::ii->AcquireBuffer<TestData>();

			// Fill data here

			q->Enqueue(data);

			if (Atomic::Add(&write_ctr, 1) == 100000)
			{
				u32 now = Clock::msec_fast();

				CAT_INFO("Test") << "Write throughput: " << (write_ctr / float(now - write_ts)) << "k/sec";

				write_ts = now;
				write_ctr = 0;
			}
		}

		CAT_WARN("Test") << "WriteJob terminated";
		return true;
	}

	WriteJob()
	{
		if (!StartThread())
		{
			CAT_FATAL("Job") << "Unable to start thread!";
		}
	}

	~WriteJob()
	{
		_kill_flag.Set();

		if (!WaitForThread(1000))
			AbortThread();
	}
};

int main(int argc, const char **argv)
{
    if (!InitializeFramework("LocklessFIFOTest.txt"))
	{
		FatalStop("Unable to initialize framework!");
	}

	q = new FIFO::Queue<TestData>;

    CAT_INFO("Test") << "** Press any key to begin.";

	while (!getch())
		Sleep(100);

	read_ctr = 0;
	write_ctr = 0;
	u32 ts = Clock::msec_fast();
	read_ts = ts;
	write_ts = ts;

	{
		const int READER_COUNT = 2;
		ReadJob read_jobs[READER_COUNT];

		const int WRITER_COUNT = 1;
		WriteJob write_jobs[WRITER_COUNT];

	    CAT_INFO("Test") << "** Test in progress.  Press any key to stop.";

		while (!getch())
			Sleep(100);
	}

    CAT_INFO("Test") << "** Test aborted.  Press any key to shutdown.";

	while (!getch())
		Sleep(100);

	ShutdownFramework(true);

    CAT_INFO("Test") << "** Shutdown complete.  Press any key to terminate.";

	while (!getch())
		Sleep(100);

    return 0;
}
