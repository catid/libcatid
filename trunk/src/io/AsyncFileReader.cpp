#include <cat/io/AsyncFileReader.hpp>
#include <cat/port/SystemInfo.hpp>
using namespace cat;

void AsyncFileReader::OnRead(IWorkerTLS *tls, BatchSet &buffers)
{

}

AsyncFileReader::AsyncFileReader()
{
	_file = 0;

	CAT_OBJCLR(_read_buffers);

	// Pick a read buffer size that is some multiple of the page size
	// and isn't too big (4 MB) or too small (4 KB)
	u32 buffer_size = system_info.PageSize * 16;

	if (buffer_size < 4096)
		buffer_size = 4096;
	else if (buffer_size > 1048576)
		buffer_size = 1048576 * 4;

	_read_buffer_size = buffer_size;

	_offset = 0;
	_size = 0;
}

AsyncFileReader::~AsyncFileReader()
{

}

bool AsyncFileReader::Open(const char *path)
{

}

void AsyncFileReader::Close()
{

}

bool AsyncFileReader::Poll(u8 *buffer, u32 &bytes)
{

}
