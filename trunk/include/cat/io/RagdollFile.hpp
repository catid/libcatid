/*
	Copyright (c) 2009-2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_RAGDOLL_FILE_HPP
#define CAT_RAGDOLL_FILE_HPP

#include <cat/lang/LinkedLists.hpp>
#include <cat/lang/Strings.hpp>
#include <string>

/*
	Ragdoll file format:

	A human-readable/writable hierarchical key-value data store via text files.

	I felt the itch to write a new settings file format for my online game code.
	The goals are hierarchical key-value pairs, increased performance, smaller
	file sizes, and clean and simple look&feel.  I'll compare it to XML.

	Currently the standard for interpreted text data files is XML.
	XML has a few nice properties:

		+ XSD files allow for verification of an XML according to a template.
		+ Multi-line data can be easily represented.
		+ Data is stored in nested key-value pairs, providing hierarchical data.
		+ As a standard it is easy to reuse existing code to parse the XML files.
		+ It is somewhat readable for human beings.

	I've decided to write my own format that requires a lot less typing to enter in by hand.
	Here's an example settings file using my format:

		; I/O Threads variables:
		IOThreads 8

			; The buffer count represents the number of buffers for worker threads
			BufferCount 1000

			; Maximum CPU time in percentage
			MaxCPUTime 80

	Inside of my application I can do:

		int buffer_count = Settings::ref()->getInt("IOThreads.BufferCount");

	My settings file format has the following deficiencies:

		- Nothing like XSD to validate the file format.
		- Multi-line data cannot be represented.
		- Not a standard so not a lot of existing code to parse the format.

	However, XSD validation isn't so important for a lot of applications, and
	I don't intend to use this format to store website content or whatnot.
	So the disadvantages of this custom format are outweighed by the benefits:

		+ Faster parsing / shorter load times.
		+ Easier to write by hand.
		+ Cleaner and easier to read with just a text editor.

	My settings file wrapper will maintain existing comments and capitalization in the file.
	It makes a copy of the file data during reading so that if changes need to be made,
	most of the original file is kept intact.  Key-value pairs are kept in a custom hash table
	using closed (chained) addressing for speedy lookup, and most algorithms used are zero-copy.

	The file format has been made generic enough for reuse for applications other than settings.
*/

namespace cat {

namespace ragdoll {


static const int MAX_CHARS = 256;


//// ragdoll::KeyInput

class CAT_EXPORT KeyInput
{
	u32 _hash;
	const char *_key;
	int _len;

public:
	KeyInput(const char *key);
	KeyInput(const char *key, int len);

	CAT_INLINE u32 Hash() const { return _hash; }
	CAT_INLINE const char *Key() const { return _key; }
	CAT_INLINE int Length() const { return _len; }
};


//// ragdoll::HashKey

class CAT_EXPORT HashKey
{
protected:
	NulTermFixedStr<MAX_CHARS> _key;
	int _len;
	u32 _hash;

public:
	HashKey(const KeyInput &key);

	//CAT_INLINE virtual ~HashKey() {}

	CAT_INLINE u32 Hash() { return _hash; }

	CAT_INLINE bool operator==(const KeyInput &key)
	{
		return _hash == key.Hash() &&
			   _len == key.Length() &&
			   _key.CaseCompare(key.Key(), key.Length());
	}
};


//// ragdoll::HashValue

class CAT_EXPORT HashValue
{
protected:
	NulTermFixedStr<MAX_CHARS> _value;

public:
	CAT_INLINE HashValue() {}
	HashValue(const char *key, int len);

	//CAT_INLINE virtual ~HashValue() {}

	CAT_INLINE int GetValueInt() { return atoi(_value); }

	CAT_INLINE const char *GetValueStr() { return _value; }

	CAT_INLINE void SetValueRangeStr(const char *value, int len)
	{
		_value.SetFromRangeString(value, len);
	}

	CAT_INLINE void SetValueStr(const char *value)
	{
		_value.SetFromNulTerminatedString(value);
	}

	CAT_INLINE void SetValueInt(int ivalue)
	{
		_value.SetFromInteger(ivalue);
	}
};


//// ragdoll::HashItem

class CAT_EXPORT HashItem : public HashKey, public HashValue, public SListItem
{
	friend class HashTable;

public:
	HashItem(const KeyInput &key);
	HashItem(const KeyInput &key, const char *value, int value_len);

	//CAT_INLINE virtual ~HashItem() {}
};


//// ragdoll::HashTable

class CAT_EXPORT HashTable
{
	friend class Iterator;

	CAT_NO_COPY(HashTable);

	static const u32 PREALLOC = 16;
	static const u32 GROW_THRESH = 2;
	static const u32 GROW_RATE = 2;

	u32 _allocated, _used;
	SList *_buckets;
	typedef SList::Iterator<HashItem> iter;

	bool Grow();

public:
	HashTable();
	~HashTable();

	HashItem *Lookup(const KeyInput &key); // Returns 0 if key not found
	HashItem *Create(const KeyInput &key); // Creates if it does not exist yet

	// Iterator
	class CAT_EXPORT Iterator
	{
		u32 _remaining;
		SList *_bucket;
		iter _ii;

		void IterateNext();

	public:
		Iterator(HashTable &head);

		CAT_INLINE operator HashItem *()
		{
			return _ii;
		}

		CAT_INLINE HashItem *operator->()
		{
			return _ii;
		}

		CAT_INLINE Iterator &operator++() // pre-increment
		{
			IterateNext();
			return *this;
		}

		CAT_INLINE Iterator &operator++(int) // post-increment
		{
			return ++*this;
		}
	};
};


//// ragdoll::Parser

class CAT_EXPORT Parser
{
	CAT_NO_COPY(Parser);

	static const int MAX_LINE_SIZE = 2048; // Maximum number of bytes per line in the file
	static const int MAX_TAB_RECURSION_DEPTH = 16; // Maximum number of layers in a key
	static const int MAX_FILE_SIZE = 4000000; // Maximum number of bytes in file allowed

	// File data
	u8 *_file_data;
	u32 _file_offset, _file_size;

	// Parser data
	char _line[MAX_LINE_SIZE];
	char _root_key[MAX_CHARS+1];
	char *_first, *_second;
	int _first_len, _second_len, _depth;

	// Output data
	bool _store_offsets;
	HashTable *_table;

protected:
	bool NextLine();
	int ReadTokens(int root_key_len, int root_depth);

public:
	// Do not pass in the file data or file size pointers if the file is the override file
	bool Read(const char *file_path, HashTable *output_table, u8 **file_data = 0, u32 *file_size = 0);
};


//// ragdoll::RagdollFile

class CAT_EXPORT File
{
	HashTable _table;

	bool _readSettings;	// Flag set when settings have been read from disk
	bool _modified;		// Flag set when settings have been modified since last write

	std::string _settings_path;
	u8 *_file_data;		// Pointer to settings file data in memory
	u32 _file_size;		// Number of bytes in settings file

public:
	File();
	~File();

	bool Read(const char *file_path);
	bool Override(const char *file_path);

	CAT_INLINE HashTable *GetTable() { return &_table; }
};


} // namespace ragdoll

} // namespace cat

#endif // CAT_RAGDOLL_FILE_HPP
