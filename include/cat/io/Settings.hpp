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

#ifndef CAT_SETTINGS_HPP
#define CAT_SETTINGS_HPP

#include <cat/threads/RWLock.hpp>
#include <cat/lang/LinkedLists.hpp>
#include <cat/lang/Strings.hpp>
#include <cat/lang/RefSingleton.hpp>

/*
	The settings file format is designed to be both human and machine readable.

	Override CAT_SETTINGS_FILE in your project settings to change the file path.

	Settings are stored in a hierarchy.  Tabs indicate the tree depth of a key.  Whitespace separates keys from values.

	For example: Settings::ref()->getInt("IOThreadPools.BufferCount", IOTHREADS_BUFFER_COUNT)

	Settings.cfg:
		IOThreadPools = 10
			BufferCount = 5
*/

namespace cat {


static const int SETTINGS_STRMAX = 256;


//// SettingsKeyInput

class SettingsKeyInput
{
	u32 _hash;
	const char *_key;
	int _len;

public:
	SettingsKeyInput(const char *key);
	SettingsKeyInput(const char *key, int len);

	CAT_INLINE u32 Hash() const { return _hash; }
	CAT_INLINE const char *Key() const { return _key; }
	CAT_INLINE int Length() const { return _len; }
};


//// SettingsHashKey

class CAT_EXPORT SettingsHashKey
{
protected:
	NulTermFixedStr<SETTINGS_STRMAX> _key;
	int _len;
	u32 _hash;

public:
	SettingsHashKey(const SettingsKeyInput &key);

	//CAT_INLINE virtual ~SettingsHashKey() {}

	CAT_INLINE u32 Hash() { return _hash; }

	CAT_INLINE bool operator==(const SettingsKeyInput &key)
	{
		return _hash == key.Hash() &&
			   _len == key.Length() &&
			   _key.CaseCompare(key.Key(), key.Length());
	}
};


//// SettingsHashValue

class CAT_EXPORT SettingsHashValue
{
protected:
	NulTermFixedStr<SETTINGS_STRMAX> _value;

public:
	CAT_INLINE SettingsHashValue() {}
	SettingsHashValue(const char *key, int len);

	//CAT_INLINE virtual ~SettingsHashValue() {}

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


//// SettingsHashItem

class CAT_EXPORT SettingsHashItem : public SettingsHashKey, public SettingsHashValue, public SListItem
{
	friend class SettingsHashTable;

public:
	SettingsHashItem(const SettingsKeyInput &key);
	SettingsHashItem(const SettingsKeyInput &key, const char *value, int value_len);

	//CAT_INLINE virtual ~SettingsHashItem() {}
};


//// SettingsHashTable

class CAT_EXPORT SettingsHashTable
{
	friend class Iterator;

	CAT_NO_COPY(SettingsHashTable);

	static const u32 PREALLOC = 16;
	static const u32 GROW_THRESH = 2;
	static const u32 GROW_RATE = 2;

	u32 _allocated, _used;
	SList *_buckets;
	typedef SList::Iterator<SettingsHashItem> iter;

	bool Grow();

public:
	SettingsHashTable();
	~SettingsHashTable();

	SettingsHashItem *Lookup(const SettingsKeyInput &key); // Returns 0 if key not found
	SettingsHashItem *Create(const SettingsKeyInput &key); // Creates if it does not exist yet

	// Iterator
	class CAT_EXPORT Iterator
	{
		u32 _remaining;
		SList *_bucket;
		iter _ii;

		void IterateNext();

	public:
		Iterator(SettingsHashTable &head);

		CAT_INLINE operator SettingsHashItem *()
		{
			return _ii;
		}

		CAT_INLINE SettingsHashItem *operator->()
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


//// SettingsParser

class CAT_EXPORT SettingsParser
{
	CAT_NO_COPY(SettingsParser);

	static const int MAX_LINE_SIZE = 2048; // Maximum number of bytes per line in the file
	static const int MAX_TAB_RECURSION_DEPTH = 16; // Maximum number of layers in a settings key
	static const int MAX_SETTINGS_FILE_SIZE = 4000000; // Maximum number of bytes in file allowed

	// File data
	u8 *_file_data;
	u32 _file_offset, _file_size;

	// Parser data
	char _line[MAX_LINE_SIZE];
	char _root_key[SETTINGS_STRMAX+1];
	char *_first, *_second;
	int _first_len, _second_len, _depth;

	// Output data
	bool _store_offsets;
	SettingsHashTable *_table;

protected:
	int readLine(char *outs, int len);
	bool nextLine();
	int readTokens(int root_key_len, int root_depth);

public:
	// Do not pass in the file data or file size pointers if the file is the override file
	bool readSettingsFile(const char *file_path, SettingsHashTable *output_table, u8 **file_data = 0, u32 *file_size = 0);
};


//// Settings

class CAT_EXPORT Settings : public RefSingleton<Settings>
{
	void OnInitialize();
	void OnFinalize();

	RWLock _lock;

	SettingsHashTable _table;

	bool _readSettings;	// Flag set when settings have been read from disk
	bool _modified;		// Flag set when settings have been modified since last write

	std::string _settings_path, _override_path;
	u8 *_file_data;		// Pointer to settings file data in memory
	u32 _file_size;		// Number of bytes in settings file

	void read(const char *settings_path = CAT_SETTINGS_FILE, const char *override_path = CAT_SETTINGS_OVERRIDE_FILE);
	void write();

public:
	int getInt(const char *name, int default_value = 0);
	const char *getStr(const char *name, const char *default_value = "");

	void setInt(const char *name, int value);
	void setStr(const char *name, const char *value);
};


} // namespace cat

#endif // CAT_SETTINGS_HPP
