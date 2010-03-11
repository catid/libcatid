/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_SPHYNX_COLLEXION_HPP
#define CAT_SPHYNX_COLLEXION_HPP

#include <cat/threads/Mutex.hpp>
#include <cat/threads/RegionAllocator.hpp>

namespace cat {


namespace sphynx {


//// sphynx::Collexion

template<class T>
class CollexionIterator;

template<class T>
struct CollexionElement
{
	// Number of active enumerators using this element
	// If references are held it cannot be deleted
	// so the KILL flag is set on the 'next' member and
	// the final enumerator to reduce the reference count
	// to zero is responsible for removal.
	u32 refcnt;

	// Bitfield:
	//  1 bit: COLLISION FLAG
	//  1 bit: KILL FLAG
	//  30 bits: Table index to next element in list + 1
	u32 next;

	// Data at this table element
	T *conn;
};

struct CollexionElement2
{
	// Previous table element in list (not +1)
	u32 last;

	// Hash of data pointer from main entry (so it doesn't need to be recalculated during growth)
	u32 hash;
};

template<class T>
class Collexion
{
	static const u32 COLLIDE_MASK = 0x80000000;
	static const u32 KILL_MASK = 0x40000000;
	static const u32 NEXT_MASK = 0x3fffffff;
	static const u32 MIN_ALLOCATED = 32;

private:
	// Number of used table elements
	u32 _used;

	// Number of allocated table elements
	u32 _allocated;

	// First table index in list of active elements
	u32 _first;

	// Primary table
	CollexionElement<T> *_table;

	// Secondary table, split off so that primary table elements will
	// fit on a cache line.  Contains data that is only accessed rarely.
	CollexionElement2 *_table2;

	// Table lock
	Mutex _lock;

protected:
	// Attempt to double size of hash table (does not hold lock)
	bool DoubleTable();

	// Hash a pointer to a 32-bit table key
	static CAT_INLINE u32 HashPtr(T *ptr)
	{
		u64 key = 0xBADdecafDEADbeef;

#if defined(CAT_WORD_64)
		key ^= *(u64*)&ptr;
#else
		key ^= *(u32*)&ptr;
#endif

		key = (~key) + (key << 18);
		key = key ^ (key >> 31);
		key = key * 21;
		key = key ^ (key >> 11);
		key = key + (key << 6);
		key = key ^ (key >> 22);
		return (u32)key;
	}

public:
	// Ctor zeros everything
	Collexion()
	{
		_first = 0;
		_used = 0;
		_allocated = 0;
		_table = 0;
		_table2 = 0;
	}

	// Dtor releases dangling memory
	~Collexion();

	// Returns true if table is empty
	CAT_INLINE bool IsEmpty() { return _used == 0; }

	// Insert Connexion object, return false if already present or out of memory
	bool Insert(T *conn);

	// Remove Connexion object from list if it exists
	bool Remove(T *conn);

	// Begin iterating through list
	bool Begin(CollexionIterator<T> &iter);

	// Iterate
	bool Next(CollexionIterator<T> &iter);
};


//// sphynx::CollexionIterator

template<class T>
class CollexionIterator
{
	static const u32 MAX_CACHE = 256;

public:
	Collexion<T> *_parent;
	u32 _offset, _total;
	T *_cache[MAX_CACHE];

public:
	CAT_INLINE T *Get() throw() { return _cache[_offset]; }
	CAT_INLINE T *operator->() throw() { return _cache[_offset]; }
	CAT_INLINE T &operator*() throw() { return *_cache[_offset]; }
	CAT_INLINE operator T*() { return _cache[_offset]; }

public:
	CollexionIterator &operator++();
};


//// sphynx::Collexion

template<class T>
bool Collexion<T>::DoubleTable()
{
	u32 new_allocated = _allocated << 1;
	if (new_allocated < MIN_ALLOCATED) new_allocated = MIN_ALLOCATED;

	u32 new_bytes2 = sizeof(CollexionElement2) * new_allocated;
	CollexionElement2 *new_table2 = reinterpret_cast<CollexionElement2*>(
		RegionAllocator::ii->Acquire(new_bytes2) );

	if (!new_table2) return false;

	u32 new_bytes = sizeof(CollexionElement<T>) * new_allocated;
	CollexionElement<T> *new_table = reinterpret_cast<CollexionElement<T> *>(
		RegionAllocator::ii->Acquire(new_bytes) );

	if (!new_table)
	{
		RegionAllocator::ii->Release(new_table2);
		return false;
	}

	CAT_CLR(new_table, new_bytes);

	u32 new_first = 0;

	if (_table && _table2)
	{
		// For each entry in the old table,
		u32 ii = _first, mask = _allocated - 1;

		while (ii)
		{
			--ii;
			CollexionElement<T> *oe = &_table[ii];
			u32 hash = _table2[ii].hash;
			u32 key = hash & mask;

			// While collisions occur,
			while (new_table[key].conn)
			{
				// Mark collision
				new_table[key].next |= COLLIDE_MASK;

				// Walk collision list
				key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & mask;
			}

			// Fill new table element
			new_table[key].conn = oe->conn;
			new_table2[key].hash = hash;
			// new_table[key].refcnt is already zero

			// Link new element to new list
			if (new_first)
			{
				new_table[key].next |= new_first;
				new_table2[new_first - 1].last = key;
			}
			// new_table[key].next is already zero so no need to zero it here
			new_first = key + 1;

			// Get next old table entry
			ii = oe->next & NEXT_MASK;
		}
	}

	// Resulting linked list starting with _first-1 will extend until e->next == 0

	if (_table2)
	{
		RegionAllocator::ii->Release(_table2);
	}

	if (_table)
	{
		RegionAllocator::ii->Release(_table);
	}

	_table = new_table;
	_table2 = new_table2;
	_allocated = new_allocated;
	_first = new_first;
	return true;
}

template<class T>
Collexion<T>::~Collexion()
{
	if (_table2)
	{
		RegionAllocator::ii->Release(_table2);
	}

	// If table doesn't exist, return
	if (!_table) return;

	// For each allocated table entry,
	for (u32 ii = 0; ii < _allocated; ++ii)
	{
		// Get Connexion object
		T *conn = _table[ii].conn;

		// If object is valid, release it
		if (conn) conn->ReleaseRef();
	}

	// Release table memory
	RegionAllocator::ii->Release(_table);
}

template<class T>
bool Collexion<T>::Insert(T *conn)
{
	u32 hash = HashPtr(conn);
	conn->AddRef();

	AutoMutex lock(_lock);

	// If more than half of the table will be used,
	if (_used >= (_allocated >> 1))
	{
		// Double the size of the table (O(1) allocation pattern)
		// Growing pains are softened by careful design
		if (!DoubleTable())
		{
			// On growth failure, return false
			lock.Release();
			conn->ReleaseRef();
			return false;
		}
	}

	// Mask off high bits to make table key from hash
	u32 mask = _allocated - 1;
	u32 key = hash & mask;

	// While empty table entry not found,
	while (_table[key].conn)
	{
		// If Connexion object is already in the table,
		if (_table[key].conn == conn)
		{
			// Return false on duplicate
			lock.Release();
			conn->ReleaseRef();
			return false;
		}

		// Mark as a collision
		_table[key].next |= COLLIDE_MASK;

		// Walk collision list
		key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & mask;
	}

	// Fill new element
	_table[key].conn = conn;
	_table[key].refcnt = 0;
	_table2[key].hash = hash;
	_table2[key].last = 0;

	// Link new element to front of list
	if (_first) _table2[_first - 1].last = key;
	_table[key].next = (_table[key].next & COLLIDE_MASK) | _first;
	_first = key + 1;

	++_used;
	return true;
}

template<class T>
bool Collexion<T>::Remove(T *conn)
{
	u32 hash = HashPtr(conn);

	AutoMutex lock(_lock);

	// Mask off high bits to make table key from hash
	u32 mask = _allocated - 1;
	u32 key = hash & mask;

	// While target table entry not found,
	for (;;)
	{
		// If target was found,
		if (_table[key].conn == conn)
		{
			if (_table[key].refcnt)
			{
				// Mark it killed so iterator can clean it up when it's finished
				_table[key].next |= KILL_MASK;
			}
			else
			{
				// Clear reference, maintaining collision flag
				_table[key].conn = 0;

				// If this key was a leaf on a collision list,
				if (0 == (_table[key].next & COLLIDE_MASK))
				{
					// TODO: Unset collision flags with multiplicative inverse
					// TODO: Implement this optimization in the two other dictionaries
				}

				lock.Release();

				conn->ReleaseRef();
			}

			// Return success
			return true;
		}

		if (0 == (_table[key].next & COLLIDE_MASK))
		{
			break; // End of collision list
		}

		// Walk collision list
		key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & mask;
	}

	// Return failure: not found
	return false;
}

template<class T>
bool Collexion<T>::Begin(CollexionIterator<T> &iter)
{
	AutoMutex lock(_lock);

	if (!_first)
	{
		iter._element = 0;
		iter._conn = 0;

		return false;
	}

	iter._element = &_table[_first];
	iter._conn = iter._element->conn;

	return true;
}

template<class T>
bool Collexion<T>::Next(CollexionIterator<T> &iter)
{
	AutoMutex lock(_lock);

	u32 next = iter._element->next & NEXT_MASK;

	if (!next)
	{
		iter._conn = 0;
		return false;
	}

	iter._element = &_table[next - 1];
	iter._conn = iter._element->conn;

	return true;
}


//// sphynx::CollexionIterator

template<class T>
CollexionIterator<T> &CollexionIterator<T>::operator++()
{
	if (++_offset >= _total)
	{
		_parent->Next(*this);
	}
}


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_COLLEXION_HPP
