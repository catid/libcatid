#include <cat/AllCommon.hpp>
#include <conio.h> // kbhit()
using namespace cat;

struct RandomBuffer : WorkerBuffer
{
	u32 worker_id;
	double usec;
	u32 x;
};

class TestWorkerTLS : public IWorkerTLS
{
public:
	virtual bool Valid() { return true; }
};

class TestWorker
{
public:
	TestWorker()
	{
	}

	void OnEvents(IWorkerTLS *tls, const BatchSet &buffers)
	{
		for (BatchHead *node = buffers.head; node; node = node->batch_next)
		{
			RandomBuffer *r = static_cast<RandomBuffer*>( node );

			for (u32 ii = 0; ii < 100000; ++ii)
			{
				r->usec += Clock::usec();
				r->x += MurmurGenerateUnbiased(r, sizeof(RandomBuffer), 0, 1000);
			}

			WorkerThreads::ref()->DeliverBuffers(WQPRIO_LO, r->worker_id, r);
		}
	}
};

int main()
{
	CommonLayer layer;

	if (!layer.Startup<TestWorkerTLS>("TestThreads.cfg"))
	{
		FatalStop("Unable to initialize framework!");
		return 1;
	}

	INFO("TestThreads") << "TestThreads 1.0";

	TestWorker worker;

	WorkerThreads *threads = WorkerThreads::ref();

	for (u32 ii = 0, count = threads->GetWorkerCount(); ii < count; ++ii)
	{
		RandomBuffer *buffer = new RandomBuffer;

		buffer->worker_id = ii;
		buffer->callback.SetMember<TestWorker, &TestWorker::OnEvents>(&worker);

		threads->DeliverBuffers(WQPRIO_LO, ii, buffer);
	}

	INFO("Server") << "Press a key to terminate";

	while (!kbhit())
	{
		Clock::sleep(100);
	}

	layer.Shutdown();

	return 0;
}
