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

#include <cat/lang/LinkedLists.hpp>
using namespace cat;


//// Forward-iterating doubly-linked list

void DListForward::PushFront(DListItem *item)
{
	CAT_FDLL_PUSH_FRONT(_head, item, _next, _prev);
}

void DListForward::InsertBefore(DListItem *item, DListItem *at)
{
	CAT_FDLL_INSERT_BEFORE(_head, item, at, _next, _prev);
}

void DListForward::InsertAfter(DListItem *item, DListItem *at)
{
	CAT_FDLL_INSERT_AFTER(_head, item, at, _next, _prev);
}

void DListForward::Erase(DListItem *item)
{
	CAT_FDLL_ERASE(_head, item, _next, _prev);
}


//// Bidirectionally-iterating doubly-linked list

void DList::PushFront(DListItem *item)
{
	CAT_BDLL_PUSH_FRONT(_head, _tail, item, _next, _prev);
}

void DList::PushBack(DListItem *item)
{
	CAT_BDLL_PUSH_FRONT(_head, _tail, item, _next, _prev);
}

void DList::InsertBefore(DListItem *item, DListItem *at)
{
	CAT_BDLL_INSERT_BEFORE(_head, _tail, item, at, _next, _prev);
}

void DList::InsertAfter(DListItem *item, DListItem *at)
{
	CAT_BDLL_INSERT_AFTER(_head, _tail, item, at, _next, _prev);
}

void DList::Erase(DListItem *item)
{
	CAT_BDLL_ERASE(_head, _tail, item, _next, _prev);
}


//// Forward-iterating singly-linked list

void SList::PushFront(SListItem *item)
{
	CAT_FSLL_PUSH_FRONT(_head, item, _next, _prev);
}

void SList::InsertAfter(SListItem *item, SListItem *at)
{
	CAT_FSLL_INSERT_AFTER(_head, item, at, _next, _prev);
}

void SList::EraseAfter(SListItem *item, SListItem *at)
{
	CAT_FSLL_ERASE_AFTER(_head, item, at, _next, _prev);
}
