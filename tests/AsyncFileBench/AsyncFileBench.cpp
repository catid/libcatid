#include <cat/AllAsyncIO.hpp>
using namespace cat;

class AsyncTestTLS : public IWorkerTLS
{
public:
	virtual bool Valid() { return true; }
};

class Reader
{
	static u8 m_padding1[CAT_DEFAULT_CACHE_LINE_SIZE];
	static u32 m_file_offset;
	static u8 m_padding2[CAT_DEFAULT_CACHE_LINE_SIZE];
	static u32 m_file_chunk_size;
	static u8 m_padding3[CAT_DEFAULT_CACHE_LINE_SIZE];
	static u32 m_file_parallelism;
	static u8 m_padding4[CAT_DEFAULT_CACHE_LINE_SIZE];
	static u32 m_file_progress;
	static u8 m_padding5[CAT_DEFAULT_CACHE_LINE_SIZE];
	static u32 m_file_total;
	static u8 m_padding6[CAT_DEFAULT_CACHE_LINE_SIZE];
	static double m_start_time;
	static u8 m_padding7[CAT_DEFAULT_CACHE_LINE_SIZE];

	AsyncFile *_file;

	WaitableFlag *_flag;

	ReadBuffer *_buffers;
	u8 **_data;

	CAT_INLINE u32 GetNextFileOffset()
	{
		return Atomic::Add(&m_file_offset, m_file_chunk_size);
	}

	CAT_INLINE bool AccumulateFilePiece(u32 size)
	{
		return Atomic::Add(&m_file_progress, size) == (m_file_total - size);
	}

	void OnRead(IWorkerTLS *tls, const BatchSet &batch)
	{
		for (BatchHead *node = batch.head; node; node = node->batch_next)
		{
			ReadBuffer *buffer = reinterpret_cast<ReadBuffer*>( node );
			void *data = buffer->data;
			u32 data_bytes = buffer->data_bytes;

			if (data_bytes)
			{
				if (AccumulateFilePiece(data_bytes))
				{
					WARN("AsyncFileBench") << "File read complete in " << Clock::usec() - m_start_time << " usec";
					_flag->Set();
				}
				else
				{
					u32 offset = GetNextFileOffset();
					if (!_file->Read(buffer, offset, data, m_file_chunk_size))
					{
						WARN("AsyncFileBench") << "Unable to read from offset " << offset;
					}
				}
			}
		}
	}

public:
	Reader(WaitableFlag *flag)
	{
		_buffers = 0;
		_flag = flag;
	}
	~Reader()
	{
		Clear();
	}

	void Clear()
	{
		if (_buffers)
		{
			delete []_buffers;
			_buffers = 0;
		}
	}

	bool StartReading(IOLayer *layer, u32 parallelism, u32 chunk_size, const char *file_path)
	{
		// Start timing before file object is created

		m_start_time = Clock::usec();

		m_file_chunk_size = chunk_size;
		m_file_parallelism = parallelism;

		_buffers = new ReadBuffer[parallelism];
		if (!_buffers)
		{
			WARN("AsyncFileBench") << "Out of memory allocating parallel read buffers";
			return false;
		}

		_file = new AsyncFile;

		layer->Watch(_file);

		if (!_file->Open(layer, file_path, ASYNCFILE_READ | ASYNCFILE_NOBUFFER))
		{
			WARN("AsyncFileBench") << "Unable to open specified file: " << file_path;
			return false;
		}

		m_file_total = (u32)_file->GetSize();
		m_file_progress = 0;
		m_file_offset = 0;

		for (u32 ii = 0; ii < parallelism; ++ii)
		{
			_buffers[ii].callback.SetMember<Reader, &Reader::OnRead>(this);

			_data[ii] = (u8*)LargeAllocator::ii->Acquire(m_file_chunk_size);

			if (!_data[ii])
			{
				WARN("AsyncFileBench") << "Out of memory allocating page-aligned read buffer.  Effective parallelism reduced by 1";
				continue;
			}

			u32 offset = GetNextFileOffset();
			if (!_file->Read(_buffers+ii, offset, _data[ii], m_file_chunk_size))
			{
				WARN("AsyncFileBench") << "Unable to read from offset " << offset;
			}
		}

		return true;
	}
};


bool Main(IOLayer *layer, Reader *reader, char **argc, int argv)
{
	const char *file_path;
	int chunk_size = 0;
	int parallelism = 0;

	if (argv >= 3)
	{
		parallelism = atoi(argc[1]);
		chunk_size = atoi(argc[2]);
		file_path = argc[3];
	}
	else
	{
		WARN("AsyncFileBench") << "Please supply a path to the file to read";
		return false;
	}

	if (parallelism <= 0)
	{
		WARN("AsyncFileBench") << "Parallelism needs to be greater than 0";
		return false;
	}

	if (chunk_size < 0 || !CAT_IS_POWER_OF_2(chunk_size))
	{
		WARN("AsyncFileBench") << "Chunk size needs to be a power of 2";
		return false;
	}

	return reader->StartReading(layer, parallelism, chunk_size, file_path);
}

int main(char **argc, int argv)
{
	IOLayer layer;

	if (!layer.Startup<AsyncTestTLS>("AsyncFileBench.cfg"))
	{
		FatalStop("Unable to initialize framework!");
		return 1;
	}

	WaitableFlag flag;
	Reader reader(&flag);

	if (Main(&layer, &reader, argc, argv))
	{
		flag.Wait();
	}

	layer.Shutdown();

	return 0;
}
