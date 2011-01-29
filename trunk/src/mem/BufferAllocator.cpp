/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#include <cat/mem/BufferAllocator.hpp>
#include <cat/mem/LargeAllocator.hpp>
#include <cstdlib>
#include <cstdio>
using namespace std;
using namespace cat;

BufferAllocator::BufferAllocator(u32 buffer_min_size, u32 buffer_count)
{
	if (buffer_count < 4) buffer_count = 4;

	u32 cache_line_bytes = system_info.CacheLineBytes;
	u32 buffer_bytes = CAT_CEIL_UNIT(buffer_min_size + sizeof(BufferTail), cache_line_bytes);
	u32 total_bytes = buffer_count * buffer_bytes;
	u8 *buffers = (u8*)LargeAllocator::Acquire(total_bytes);

	_buffer_bytes = buffer_bytes;
	_buffer_count = buffer_count;
	_buffers = buffers;

	if (!buffers) return;

	// Construct linked list of free nodes
	buffers += buffer_bytes - sizeof(BufferTail);

	for (u32 ii = 0; ii < buffer_count - 1; ++ii)
	{
		BufferTail *tail = (BufferTail*)(buffers + ii * buffer_bytes);
		BufferTail *next = (BufferTail*)((u8*)tail + buffer_bytes);

		tail->next = next;
	}

	BufferTail *first = (BufferTail*)buffers;
	BufferTail *last = (BufferTail*)(buffers + (buffer_count-1) * buffer_bytes);

	_acquire_head = first;
	last->next = 0;
}

BufferAllocator::~BufferAllocator()
{
	LargeAllocator::Release(_buffers);
}

void *BufferAllocator::Acquire()
{
	_acquire_lock.Enter();

	BufferTail *head = _acquire_head;

	// If the acquire list is empty,
	if (!head)
	{
		// Grab the release list and re-use it
		_release_lock.Enter();

		head = _release_head;
		_release_head = 0;

		_release_lock.Leave();

		// If we ran out of room,
		if (!head)
		{
			_acquire_lock.Leave();
			return 0;
		}
	}

	_acquire_head = head->next;

	_acquire_lock.Leave();

	return (u8*)head - (_buffer_bytes - sizeof(BufferTail));
}

u32 BufferAllocator::AcquireMultiple(void **buffers, u32 count)
{
	u32 buffer_index = 0;
	BufferTail *head;
	u32 buffer_offset = _buffer_bytes - sizeof(BufferTail);

	_acquire_lock.Enter();

	// For each requested buffer,
	for (head = _acquire_head; head && buffer_index < count; head = head->next)
		buffers[buffer_index++] = (u8*)head - buffer_offset;

	// If there are still requests to fill,
	if (buffer_index < count)
	{
		// Grab the release list and re-use it
		_release_lock.Enter();

		head = _release_head;
		_release_head = 0;

		_release_lock.Leave();

		// For each remaining requested buffer,
		for (; buffer_index < count && head; head = head->next)
			buffers[buffer_index++] = (u8*)head - buffer_offset;
	}

	_acquire_head = head;

	_acquire_lock.Leave();

	return buffer_index;
}

void BufferAllocator::Release(void *buffer)
{
	if (!buffer) return;

	BufferTail *tail = (BufferTail*)((u8*)buffer + (_buffer_bytes - sizeof(BufferTail)));

	// Insert new released node at the head of the list
	_release_lock.Enter();

	BufferTail *head = _release_head;

	_release_head = tail;
	tail->next = head;

	_release_lock.Leave();
}

void BufferAllocator::ReleaseMultiple(void **buffers, u32 count)
{
	if (!count) return;

	u32 buffer_offset = _buffer_bytes - sizeof(BufferTail);

	BufferTail *head, *tail;

	tail = head = (BufferTail*)((u8*)buffers[0] + buffer_offset);

	for (u32 ii = 1; ii < count; ++ii)
	{
		BufferTail *node = (BufferTail*)((u8*)buffers[ii] + buffer_offset);

		tail->next = node;
		tail = node;
	}

	// Insert new released node at the head of the list
	_release_lock.Enter();

	tail->next = _release_head;
	_release_head = head;

	_release_lock.Leave();
}
