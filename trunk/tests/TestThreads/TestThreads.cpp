#include <cat/AllCommon.hpp>
#include <conio.h> // kbhit()
using namespace cat;

static Clock *m_clock = 0;

struct RandomBuffer : WorkerBuffer
{
	u32 worker_id;
	double usec;
	u32 x;
};

class TestWorker
{
public:
	TestWorker()
	{
	}

	void OnEvents(const BatchSet &buffers)
	{
		for (BatchHead *node = buffers.head; node; node = node->batch_next)
		{
			RandomBuffer *r = static_cast<RandomBuffer*>( node );

			for (u32 ii = 0; ii < 100000; ++ii)
			{
				r->usec += m_clock->usec();
				r->x += MurmurGenerateUnbiased(r, sizeof(RandomBuffer), 0, 1000);
			}

			WorkerThreads::ref()->DeliverBuffers(WQPRIO_LO, r->worker_id, r);
		}
	}
};






/*
	XOR engine timing
*/


static void memxor(void *voutput, const void *vinput, int bytes)
{
	u64 *output64 = reinterpret_cast<u64*>( voutput );
	const u64 *input64 = reinterpret_cast<const u64*>( vinput );

	int words = bytes / 8;
	for (int ii = 0; ii < words; ++ii)
		output64[ii] ^= input64[ii];

	u8 *output = reinterpret_cast<u8*>( output64 + words );
	const u8 *input = reinterpret_cast<const u8*>( input64 + words );

	bytes %= 8;
	while (bytes--)
		*output++ ^= *input++;
}




static void memxor2(void *voutput, const void *vinput, int bytes)
{
	u64 *output64 = reinterpret_cast<u64*>( voutput );
	const u64 *input64 = reinterpret_cast<const u64*>( vinput );

	while (bytes >= 32)
	{
		*output64++ ^= *input64++;
		*output64++ ^= *input64++;
		*output64++ ^= *input64++;
		*output64++ ^= *input64++;
		bytes -= 32;
	}

	while (bytes >= 8)
	{
		*output64++ ^= *input64++;
		bytes -= 8;
	}

	u8 *output = reinterpret_cast<u8*>( output64 );
	const u8 *input = reinterpret_cast<const u8*>( input64 );

	while (bytes--)
		*output++ ^= *input++;
}




static void memxor3(void *voutput, const void *vinput, int bytes)
{
	u64 *output64 = reinterpret_cast<u64*>( voutput );
	const u64 *input64 = reinterpret_cast<const u64*>( vinput );

	while (bytes >= 128)
	{
		output64[0] ^= input64[0];
		output64[1] ^= input64[1];
		output64[2] ^= input64[2];
		output64[3] ^= input64[3];
		output64[4] ^= input64[4];
		output64[5] ^= input64[5];
		output64[6] ^= input64[6];
		output64[7] ^= input64[7];
		output64[8] ^= input64[8];
		output64[9] ^= input64[9];
		output64[10] ^= input64[10];
		output64[11] ^= input64[11];
		output64[12] ^= input64[12];
		output64[13] ^= input64[13];
		output64[14] ^= input64[14];
		output64[15] ^= input64[15];
		output64 += 16;
		input64 += 16;
		bytes -= 128;
	}

	while (bytes >= 8)
	{
		*output64++ ^= *input64++;
		bytes -= 8;
	}

	u8 *output = reinterpret_cast<u8*>( output64 );
	const u8 *input = reinterpret_cast<const u8*>( input64 );

	while (bytes--)
		*output++ ^= *input++;
}


static void memxor(void *voutput, const void *va, const void *vb, int bytes)
{
	u64 *output64 = reinterpret_cast<u64*>( voutput );
	const u64 *a64 = reinterpret_cast<const u64*>( va );
	const u64 *b64 = reinterpret_cast<const u64*>( vb );

	int words = bytes / 8;
	for (int ii = 0; ii < words; ++ii)
		output64[ii] = a64[ii] ^ b64[ii];

	u8 *output = reinterpret_cast<u8*>( output64 + words );
	const u8 *a = reinterpret_cast<const u8*>( a64 + words );
	const u8 *b = reinterpret_cast<const u8*>( b64 + words );

	bytes %= 8;
	while (bytes--)
		*output++ = *a++ ^ *b++;
}



static void memxor4(void *voutput, const void *va, const void *vb, int bytes)
{
	u64 *output64 = reinterpret_cast<u64*>( voutput );
	const u64 *a64 = reinterpret_cast<const u64*>( va );
	const u64 *b64 = reinterpret_cast<const u64*>( vb );

	while (bytes >= 128)
	{
		output64[0] = a64[0] ^ b64[0];
		output64[1] = a64[1] ^ b64[1];
		output64[2] = a64[2] ^ b64[2];
		output64[3] = a64[3] ^ b64[3];
		output64[4] = a64[4] ^ b64[4];
		output64[5] = a64[5] ^ b64[5];
		output64[6] = a64[6] ^ b64[6];
		output64[7] = a64[7] ^ b64[7];
		output64[8] = a64[8] ^ b64[8];
		output64[9] = a64[9] ^ b64[9];
		output64[10] = a64[10] ^ b64[10];
		output64[11] = a64[11] ^ b64[11];
		output64[12] = a64[12] ^ b64[12];
		output64[13] = a64[13] ^ b64[13];
		output64[14] = a64[14] ^ b64[14];
		output64[15] = a64[15] ^ b64[15];
		output64 += 16;
		a64 += 16;
		b64 += 16;
		bytes -= 128;
	}

	while (bytes >= 8)
	{
		*output64++ = *a64++ ^ *b64++;
		bytes -= 8;
	}

	u8 *output = reinterpret_cast<u8*>( output64 );
	const u8 *a = reinterpret_cast<const u8*>( a64 );
	const u8 *b = reinterpret_cast<const u8*>( b64 );

	while (bytes--)
		*output++ = *a++ ^ *b++;
}



struct Block
{
	u8 data[1431];
};


void XORTest()
{
	Block *blocks = new Block[65536];

	CatsChoice prng;
	prng.Initialize(m_clock->msec_fast());

	double start, end;


	start = m_clock->usec();

	for (int ii = 0; ii < 2000000; ++ii)
	{
		int a = prng.Next() % 65536;
		int to = prng.Next() % 65536;

		memxor(&blocks[to], &blocks[a], sizeof(Block));
	}

	end = m_clock->usec();

	CAT_INFO("XOR") << "memxor took " << (end - start) / 1000.f << " ms";


	start = m_clock->usec();

	for (int ii = 0; ii < 2000000; ++ii)
	{
		int a = prng.Next() % 65536;
		int to = prng.Next() % 65536;

		memxor2(&blocks[to], &blocks[a], sizeof(Block));
	}

	end = m_clock->usec();

	CAT_INFO("XOR") << "memxor2 took " << (end - start) / 1000.f << " ms";


	start = m_clock->usec();

	for (int ii = 0; ii < 2000000; ++ii)
	{
		int a = prng.Next() % 65536;
		int to = prng.Next() % 65536;

		memxor3(&blocks[to], &blocks[a], sizeof(Block));
	}

	end = m_clock->usec();

	CAT_INFO("XOR") << "memxor3 took " << (end - start) / 1000.f << " ms";


	start = m_clock->usec();

	for (int ii = 0; ii < 2000000; ++ii)
	{
		int a = prng.Next() % 65536;
		int b = prng.Next() % 65536;
		int to = prng.Next() % 65536;

		memxor(&blocks[to], &blocks[a], &blocks[b], sizeof(Block));
	}

	end = m_clock->usec();

	CAT_INFO("XOR") << "memxor(2 input) took " << (end - start) / 1000.f << " ms";


	start = m_clock->usec();

	for (int ii = 0; ii < 2000000; ++ii)
	{
		int a = prng.Next() % 65536;
		int b = prng.Next() % 65536;
		int to = prng.Next() % 65536;

		memxor4(&blocks[to], &blocks[a], &blocks[b], sizeof(Block));
	}

	end = m_clock->usec();

	CAT_INFO("XOR") << "memxor4(2 input) took " << (end - start) / 1000.f << " ms";


	delete []blocks;
}



int main()
{
	m_clock = Clock::ref();

	XORTest();

	CAT_INFO("TestThreads") << "TestThreads 1.0";

	TestWorker worker;

	WorkerThreads *threads = WorkerThreads::ref();

	for (u32 ii = 0, count = threads->GetWorkerCount(); ii < count; ++ii)
	{
		RandomBuffer *buffer = new RandomBuffer;

		buffer->worker_id = ii;
		buffer->callback.SetMember<TestWorker, &TestWorker::OnEvents>(&worker);

		threads->DeliverBuffers(WQPRIO_LO, ii, buffer);
	}

	CAT_INFO("Server") << "Press a key to terminate";

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	return 0;
}
