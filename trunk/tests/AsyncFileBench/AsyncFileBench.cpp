#include <cat/AllAsyncIO.hpp>
using namespace cat;

class AsyncTestTLS : public IWorkerTLS
{
public:
	virtual bool Valid() { return true; }
};

class Reader
{
	u8 m_padding1[CAT_DEFAULT_CACHE_LINE_SIZE];
	u32 m_file_offset;
	u8 m_padding2[CAT_DEFAULT_CACHE_LINE_SIZE];
	u32 m_file_chunk_size;
	u8 m_padding3[CAT_DEFAULT_CACHE_LINE_SIZE];
	u32 m_file_parallelism;
	u8 m_padding4[CAT_DEFAULT_CACHE_LINE_SIZE];
	u32 m_file_progress;
	u8 m_padding5[CAT_DEFAULT_CACHE_LINE_SIZE];
	u32 m_file_total;
	u8 m_padding6[CAT_DEFAULT_CACHE_LINE_SIZE];
	double m_start_time;
	u8 m_padding7[CAT_DEFAULT_CACHE_LINE_SIZE];

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
					double delta = Clock::usec() - m_start_time;

					double rate = m_file_total / delta;

					u32 s = (u32)(delta / 1000000.0);
					u32 ms = (u32)(delta / 1000.0) % 1000;
					delta -= (u32)(delta / 1000.0) * 1000.0;

					WARN("AsyncFileBench") << "Total file size = " << m_file_total;
					WARN("AsyncFileBench") << "File read complete in " << s << " s : " << ms << " ms : " << delta << " us -- at " << rate << " MBPS";
					_flag->Set();
				}
				else
				{
					u32 offset = GetNextFileOffset();

					if (offset < m_file_total && !_file->Read(buffer, offset, data, m_file_chunk_size))
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
		if (_data)
		{
			delete []_data;
			_data = 0;
		}
	}

	bool StartReading(bool no_buffer, bool seq, u32 parallelism, u32 chunk_size, const char *file_path)
	{
		// Start timing before file object is created

		m_start_time = Clock::usec();

		m_file_chunk_size = chunk_size;
		m_file_parallelism = parallelism;

		_buffers = new ReadBuffer[parallelism];
		_data = new u8*[parallelism];
		if (!_buffers || !_data)
		{
			WARN("AsyncFileBench") << "Out of memory allocating parallel read buffers";
			return false;
		}

		_file = new AsyncFile;

		RefObjectWatcher::ref()->Watch(_file);

		if (!_file->Open(file_path, ASYNCFILE_READ | (no_buffer ? ASYNCFILE_NOBUFFER : 0) | (seq ? ASYNCFILE_SEQUENTIAL : 0)))
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
			_buffers[ii].worker_id = 0;

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










#include <winioctl.h>	// DeviceIoControl()



void GetHarddiskDump()
{
	const int MAX_DRIVES = 16;

	for (int ii = 0; ii < MAX_DRIVES; ++ii)
	{
		char device_name[256];
		sprintf(device_name, "\\\\.\\PhysicalDrive%d", ii);

		// Open device
		HANDLE device = CreateFileA(device_name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
		if (device != INVALID_HANDLE_VALUE)
		{
			STORAGE_PROPERTY_QUERY query;
			CAT_OBJCLR(query);
			query.PropertyId = StorageDeviceProperty;
			query.QueryType = PropertyStandardQuery;

			u8 buffer[4096];
			CAT_OBJCLR(buffer);
			DWORD bytes;

			// Read storage device descriptor
			if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytes, 0))
			{
				STORAGE_DEVICE_DESCRIPTOR *descrip = (STORAGE_DEVICE_DESCRIPTOR *)buffer;

				const u32 DESCRIP_BYTES = offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties);

				if (bytes >= DESCRIP_BYTES && descrip->Size <= bytes)
				{
					u8 gbuffer[4096];
					CAT_OBJCLR(gbuffer);
					DWORD gbytes;

					// Read geometry
					if (DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 0, 0, gbuffer, sizeof(gbuffer), &gbytes, 0))
					{
						DISK_GEOMETRY_EX *geometry = (DISK_GEOMETRY_EX*)gbuffer;
						DISK_PARTITION_INFO *parts = DiskGeometryGetPartition(geometry);

						const u32 GEOMETRY_BYTES = offsetof(DISK_GEOMETRY_EX, Data);

						// Verify that drive is fixed
						if (geometry->Geometry.MediaType == FixedMedia)
						{
							WARN("AsyncFileBench") << "Fixed disk " << ii << ": " << (char*)(buffer + descrip->ProductIdOffset);
							WARN("AsyncFileBench") << " - Bytes per sector = " << geometry->Geometry.BytesPerSector;
							WARN("AsyncFileBench") << " - Cylinders = " << (u64)geometry->Geometry.Cylinders.QuadPart;
							WARN("AsyncFileBench") << " - Sectors per track = " << geometry->Geometry.SectorsPerTrack;
							WARN("AsyncFileBench") << " - Tracks per cylinder = " << geometry->Geometry.TracksPerCylinder;
						}
					}
				}
			}

			CloseHandle(device);
		}
	}
}

void GetCdRomDump()
{
	// Open device
	HANDLE device = CreateFileW(L"\\\\.\\CdRom0", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (device != INVALID_HANDLE_VALUE)
	{
		STORAGE_PROPERTY_QUERY query;
		CAT_OBJCLR(query);
		query.PropertyId = StorageDeviceProperty;
		query.QueryType = PropertyExistsQuery;

		u8 buffer[4096];
		CAT_OBJCLR(buffer);
		DWORD bytes = sizeof(buffer);

		// Read storage device descriptor
		if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytes, 0))
		{
			STORAGE_DEVICE_DESCRIPTOR *descrip = (STORAGE_DEVICE_DESCRIPTOR *)buffer;

			const u32 DESCRIP_BYTES = offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties);

			// Don't query the geometry for CD-ROM drives since it only exists if a disk is in the drive
			if (bytes >= DESCRIP_BYTES && descrip->Size <= bytes)
			{
				WARN("AsyncFileBench") << "CD-ROM disc 0: " << (char*)(buffer + descrip->ProductIdOffset);

				u8 gbuffer[4096];
				CAT_OBJCLR(gbuffer);
				DWORD gbytes;

				// Read geometry
				if (DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 0, 0, gbuffer, sizeof(gbuffer), &gbytes, 0))
				{
					DISK_GEOMETRY_EX *geometry = (DISK_GEOMETRY_EX*)gbuffer;
					DISK_PARTITION_INFO *parts = DiskGeometryGetPartition(geometry);

					const u32 GEOMETRY_BYTES = offsetof(DISK_GEOMETRY_EX, Data);

					WARN("AsyncFileBench") << " - Bytes per sector = " << geometry->Geometry.BytesPerSector;
					WARN("AsyncFileBench") << " - Cylinders = " << (u64)geometry->Geometry.Cylinders.QuadPart;
					WARN("AsyncFileBench") << " - Sectors per track = " << geometry->Geometry.SectorsPerTrack;
					WARN("AsyncFileBench") << " - Tracks per cylinder = " << geometry->Geometry.TracksPerCylinder;
				}
				else
				{
					WARN("AsyncFileBench") << " - Unable to get geometry";
				}
			}
		}

		CloseHandle(device);
	}
}












bool Main(Reader *reader, char **argv, int argc)
{
	const char *file_path;
	int chunk_size = 0;
	int parallelism = 0;
	int no_buffer = 0;
	int seq = 0;

	if (argc >= 6)
	{
		no_buffer = atoi(argv[1]);
		seq = atoi(argv[2]);
		parallelism = atoi(argv[3]);
		chunk_size = atoi(argv[4]);
		file_path = argv[5];
	}
	else
	{
		WARN("AsyncFileBench") << "Expected arguments: <no_buffer(1/0)> <seq(1/0)> <parallelism> <chunk size> <file path>";
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

	return reader->StartReading(no_buffer != 0, seq != 0, parallelism, chunk_size, file_path);
}

int main(int argc, char **argv)
{
	IOLayer layer;

	if (!layer.Startup<AsyncTestTLS>("AsyncFileBench.cfg"))
	{
		FatalStop("Unable to initialize framework!");
		return 1;
	}

	WARN("AsyncFileBench") << "Allocation granularity = " << system_info.AllocationGranularity;
	WARN("AsyncFileBench") << "Cache line bytes = " << system_info.CacheLineBytes;
	WARN("AsyncFileBench") << "Page size = " << system_info.PageSize;
	WARN("AsyncFileBench") << "Processor count = " << system_info.ProcessorCount;

	GetHarddiskDump();
	GetCdRomDump();

	WaitableFlag flag;
	Reader reader(&flag);

	if (Main(&reader, argv, argc))
	{
		flag.Wait();
	}

	layer.Shutdown();

	return 0;
}
