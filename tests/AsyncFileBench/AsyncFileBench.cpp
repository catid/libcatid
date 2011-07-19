#include <cat/AllAsyncIO.hpp>
using namespace cat;

class AsyncTestTLS : public IWorkerTLS
{
public:
	virtual bool Valid() { return true; }
};

class ReadTester
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
		for (BatchHead *next, *node = batch.head; node; node = next)
		{
			next = node->batch_next;
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

					CAT_WARN("AsyncFileBench") << "Total file size = " << m_file_total;
					CAT_WARN("AsyncFileBench") << "File read complete in " << s << " s : " << ms << " ms : " << delta << " us";
					CAT_WARN("AsyncFileBench") << "File read complete at " << rate << " MBPS";
					_flag->Set();
				}
				else
				{
					//CAT_WARN("AsyncFileBench") << "Read at " << buffer->offset;

					u32 offset = GetNextFileOffset();

					if (offset < m_file_total && !_file->Read(buffer, offset, data, m_file_chunk_size))
					{
						CAT_WARN("AsyncFileBench") << "Unable to read from offset " << offset;
					}
				}
			}
		}
	}

public:
	ReadTester(WaitableFlag *flag)
	{
		_buffers = 0;
		_flag = flag;
		_data = 0;
	}
	~ReadTester()
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
			CAT_WARN("AsyncFileBench") << "Out of memory allocating parallel read buffers";
			return false;
		}

		_file = new AsyncFile;

		RefObjects::ref()->Watch(_file);

		if (!_file->Open(file_path, ASYNCFILE_READ | (no_buffer ? ASYNCFILE_NOBUFFER : 0) | (seq ? ASYNCFILE_SEQUENTIAL : 0)))
		{
			CAT_WARN("AsyncFileBench") << "Unable to open specified file: " << file_path;
			return false;
		}

		m_file_total = (u32)_file->GetSize();
		m_file_progress = 0;
		m_file_offset = 0;

		for (u32 ii = 0; ii < parallelism; ++ii)
		{
			_buffers[ii].callback.SetMember<ReadTester, &ReadTester::OnRead>(this);
			_buffers[ii].worker_id = 0;

			_data[ii] = (u8*)LargeAllocator::ii->Acquire(m_file_chunk_size);

			if (!_data[ii])
			{
				CAT_WARN("AsyncFileBench") << "Out of memory allocating page-aligned read buffer.  Effective parallelism reduced by 1";
				continue;
			}

			u32 offset = GetNextFileOffset();
			if (!_file->Read(_buffers+ii, offset, _data[ii], m_file_chunk_size))
			{
				CAT_WARN("AsyncFileBench") << "Unable to read from offset " << offset;
			}
		}

		return true;
	}
};


class WriteTester
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

	WriteBuffer *_buffers;
	u8 **_data;

	bool _no_buffer;
	bool _seq;
	u32 _parallelism;
	u32 _chunk_size;
	const char *_file_path;
	ReadTester *_reader;

	CAT_INLINE u32 GetNextFileOffset()
	{
		return Atomic::Add(&m_file_offset, m_file_chunk_size);
	}

	CAT_INLINE bool AccumulateFilePiece(u32 size)
	{
		return Atomic::Add(&m_file_progress, size) >= (m_file_total - size);
	}

	void OnWrite(IWorkerTLS *tls, const BatchSet &batch)
	{
		for (BatchHead *next, *node = batch.head; node; node = next)
		{
			next = node->batch_next;
			WriteBuffer *buffer = reinterpret_cast<WriteBuffer*>( node );
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

					CAT_WARN("AsyncFileBench") << "Total file size = " << m_file_total;
					CAT_WARN("AsyncFileBench") << "File write complete in " << s << " s : " << ms << " ms : " << delta << " us";
					CAT_WARN("AsyncFileBench") << "File write complete at " << rate << " MBPS";

					_file->RequestShutdown();

					Sleep(1000);

					_reader = new ReadTester(_flag);

					if (!_reader->StartReading(_no_buffer, _seq, _parallelism, _chunk_size, _file_path))
						_flag->Set();
				}
				else
				{
					//CAT_WARN("AsyncFileBench") << "Wrote at " << buffer->offset << " bytes=" << data_bytes << " done=" << m_file_progress << "/" << m_file_total;

					u32 offset = GetNextFileOffset();

					if (offset < m_file_total && !_file->Write(buffer, offset, data, m_file_chunk_size))
					{
						CAT_WARN("AsyncFileBench") << "Unable to write to offset " << offset;
					}
				}
			}
		}
	}

public:
	WriteTester(WaitableFlag *flag)
	{
		_buffers = 0;
		_flag = flag;
		_data = 0;
	}
	~WriteTester()
	{
		Clear();

		if (_reader)
			delete _reader;
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

	bool StartWriting(bool no_buffer, bool seq, u32 parallelism, u32 chunk_size, const char *file_path)
	{
		// Start timing before file object is created
		_no_buffer = no_buffer;
		_seq = seq;
		_parallelism = parallelism;
		_chunk_size = chunk_size;
		_file_path = file_path;

		m_start_time = Clock::usec();

		m_file_chunk_size = chunk_size;
		m_file_parallelism = parallelism;

		_buffers = new WriteBuffer[parallelism];
		_data = new u8*[parallelism];
		if (!_buffers || !_data)
		{
			CAT_WARN("AsyncFileBench") << "Out of memory allocating parallel write buffers";
			return false;
		}

		_file = new AsyncFile;

		RefObjects::ref()->Watch(_file);

		_unlink(file_path);

		if (!_file->Open(file_path, ASYNCFILE_WRITE | (no_buffer ? ASYNCFILE_NOBUFFER : 0) | (seq ? ASYNCFILE_SEQUENTIAL : 0)))
		{
			CAT_WARN("AsyncFileBench") << "Unable to open specified file: " << file_path;
			return false;
		}

		m_file_total = 200000000;
		m_file_total -= m_file_total % _chunk_size;
		m_file_progress = 0;
		m_file_offset = 0;

		_file->SetSize(m_file_total);

		for (u32 ii = 0; ii < parallelism; ++ii)
		{
			_buffers[ii].callback.SetMember<WriteTester, &WriteTester::OnWrite>(this);
			_buffers[ii].worker_id = 0;

			_data[ii] = (u8*)LargeAllocator::ii->Acquire(m_file_chunk_size);

			if (!_data[ii])
			{
				CAT_WARN("AsyncFileBench") << "Out of memory allocating page-aligned write buffer.  Effective parallelism reduced by 1";
				continue;
			}

			u32 offset = GetNextFileOffset();
			if (!_file->Write(_buffers+ii, offset, _data[ii], m_file_chunk_size))
			{
				CAT_WARN("AsyncFileBench") << "Unable to write to offset " << offset;
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
							CAT_WARN("AsyncFileBench") << "Fixed disk " << ii << ": " << (char*)(buffer + descrip->ProductIdOffset);
							CAT_WARN("AsyncFileBench") << " - Bytes per sector = " << geometry->Geometry.BytesPerSector;
							CAT_WARN("AsyncFileBench") << " - Cylinders = " << (u64)geometry->Geometry.Cylinders.QuadPart;
							CAT_WARN("AsyncFileBench") << " - Sectors per track = " << geometry->Geometry.SectorsPerTrack;
							CAT_WARN("AsyncFileBench") << " - Tracks per cylinder = " << geometry->Geometry.TracksPerCylinder;
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
				CAT_WARN("AsyncFileBench") << "CD-ROM disc 0: " << (char*)(buffer + descrip->ProductIdOffset);

				u8 gbuffer[4096];
				CAT_OBJCLR(gbuffer);
				DWORD gbytes;

				// Read geometry
				if (DeviceIoControl(device, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 0, 0, gbuffer, sizeof(gbuffer), &gbytes, 0))
				{
					DISK_GEOMETRY_EX *geometry = (DISK_GEOMETRY_EX*)gbuffer;
					DISK_PARTITION_INFO *parts = DiskGeometryGetPartition(geometry);

					const u32 GEOMETRY_BYTES = offsetof(DISK_GEOMETRY_EX, Data);

					CAT_WARN("AsyncFileBench") << " - Bytes per sector = " << geometry->Geometry.BytesPerSector;
					CAT_WARN("AsyncFileBench") << " - Cylinders = " << (u64)geometry->Geometry.Cylinders.QuadPart;
					CAT_WARN("AsyncFileBench") << " - Sectors per track = " << geometry->Geometry.SectorsPerTrack;
					CAT_WARN("AsyncFileBench") << " - Tracks per cylinder = " << geometry->Geometry.TracksPerCylinder;
				}
				else
				{
					CAT_WARN("AsyncFileBench") << " - Unable to get geometry";
				}
			}
		}

		CloseHandle(device);
	}
}












bool Main(WriteTester *writer, char **argv, int argc)
{
	const char *file_path;
	int chunk_size = 0;
	int parallelism = 0;
	int no_buffer = 0;
	int seq = 0;

	if (argc >= 5)
	{
		no_buffer = atoi(argv[1]);
		seq = atoi(argv[2]);
		parallelism = atoi(argv[3]);
		chunk_size = atoi(argv[4]);
		file_path = "writer.tst";
	}
	else
	{
		CAT_WARN("AsyncFileBench") << "Expected arguments: <no_buffer(1/0)> <seq(1/0)> <parallelism> <chunk size>";
		return false;
	}

	if (parallelism <= 0)
	{
		CAT_WARN("AsyncFileBench") << "Parallelism needs to be greater than 0";
		return false;
	}

	if (chunk_size < 0 || !CAT_IS_POWER_OF_2(chunk_size))
	{
		CAT_WARN("AsyncFileBench") << "Chunk size needs to be a power of 2";
		return false;
	}

	return writer->StartWriting(no_buffer != 0, seq != 0, parallelism, chunk_size, file_path);
}


// SD card CRC-7 implementation

#include <iostream>
using namespace std;

unsigned char CRC7_fast(unsigned char crc, unsigned char data)
{
	data ^= crc << 1;

	if (data & 0x80)
		data ^= 9;

	crc = data ^ (crc & 0x78) ^ (crc << 4) ^ ((crc >> 3) & 15);

	return crc & 0x7f;
}

unsigned char CRC7f_fast(unsigned char crc)
{
	crc = (crc << 1) ^ (crc << 4) ^ (crc & 0x70) ^ ((crc >> 3) & 0x0f);
	return crc | 1;
}

unsigned char CRC7_naive(unsigned char crc, unsigned char data)
{
	for (int ii = 0; ii <= 7; ++ii)
	{
		crc = (crc << 1) | (data >> 7);
		data <<= 1;

		if (crc & 0x80)
		{
			crc ^= 9;
		}
	}

	return crc & 0x7f;
}

unsigned char CRC7f_naive(unsigned char crc)
{
	for (int ii = 0; ii <= 6; ++ii)
	{
		crc <<= 1;

		if (crc & 0x80)
		{
			crc ^= 9;
		}
	}

	return (crc << 1) | 1;
}

unsigned char m_block[256];
unsigned char m_crc_fast, m_crc_naive;

void timing_fast()
{
	unsigned char crc = 0;

	for (int ii = 0; ii < 256; ++ii)
		crc = CRC7_fast(crc, m_block[ii]);

	m_crc_fast = CRC7f_fast(crc);
}

void timing_naive()
{
	unsigned char crc = 0;

	for (int ii = 0; ii < 256; ++ii)
		crc = CRC7_naive(crc, m_block[ii]);

	m_crc_naive = CRC7f_naive(crc);
}

int RunCRC7Tests()
{
	for (int ii = 0; ii <= 255; ++ii)
	{
		unsigned char crc = (unsigned char)ii;

		for (int jj = 0; jj <= 255; ++jj)
		{
			unsigned char data = (unsigned char)jj, r_fast, r_naive;

			r_fast = CRC7_fast(crc, data);
			r_naive = CRC7_naive(crc, data);

			if (r_fast != r_naive)
			{
				cout << "CRC body failure with crc=" << ii << " and data=" << jj << '.' << endl;
				return 1;
			}
		}

		if (CRC7f_fast(crc) != CRC7f_naive(crc))
		{
			cout << "Finalization failure with crc=" << ii << '.' << endl;
			return 1;
		}

		m_block[ii] = crc;
	}

#if defined(CAT_HAS_TOYS)

	// Optional timing section (uses libcat)
	u32 clocks_fast = Clock::MeasureClocks(10000, &timing_fast);
	u32 clocks_naive = Clock::MeasureClocks(10000, &timing_naive);

	cout << "Fast algorithm takes about " << clocks_fast << " cycles" << endl;
	cout << "Naive algorithm takes about " << clocks_naive << " cycles" << endl;

#endif

	cout << "They match!" << endl;
	return 0;
}

int main(int argc, char **argv)
{
	if (!IOLayer::ref()->Startup<AsyncTestTLS>("AsyncFileBench.cfg"))
	{
		FatalStop("Unable to initialize framework!");
		return 1;
	}

	RunCRC7Tests();

	CAT_WARN("AsyncFileBench") << "Allocation granularity = " << system_info.AllocationGranularity;
	CAT_WARN("AsyncFileBench") << "Cache line bytes = " << system_info.CacheLineBytes;
	CAT_WARN("AsyncFileBench") << "Page size = " << system_info.PageSize;
	CAT_WARN("AsyncFileBench") << "Processor count = " << system_info.ProcessorCount;

	GetHarddiskDump();
	GetCdRomDump();
	
	WaitableFlag flag;
	WriteTester writer(&flag);

	if (Main(&writer, argv, argc))
	{
		flag.Wait();
	}

	IOLayer::ref()->Shutdown();

	return 0;
}
