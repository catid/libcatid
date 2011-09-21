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

int main()
{
	m_clock = Clock::ref();

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
