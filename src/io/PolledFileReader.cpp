#include <cat/io/PolledFileReader.hpp>
#include <cat/port/SystemInfo.hpp>
using namespace cat;

PolledFileReader::PolledFileReader()
{
	// Pick a read buffer size that is some multiple of the page size
	// and isn't too big (>4 MB) or too small (<4 KB)
	u32 cache_size = system_info.PageSize * 32;

	if (cache_size < 4096)
		cache_size = 4096;
	else if (cache_size > 1048576)
		cache_size = 1048576 * 4;

	_cache_size = cache_size;

	_remaining = 0;
}

PolledFileReader::~PolledFileReader()
{

}

bool PolledFileReader::Open(IOLayer *layer, const char *file_path)
{
	if (!AsyncFile::Open(layer, file_path, ASYNCFILE_READ))
	{
		WARN("PolledFileReader") << "Unable to open " << file_path;
		return false;
	}

	AsyncFile::Read()
}

bool PolledFileReader::Read(void *data, u32 &bytes)
{

}

void PolledFileReader::OnRead(IWorkerTLS *tls, BatchSet &buffers)
{

}
