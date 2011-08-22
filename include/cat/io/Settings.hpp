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

#include <cat/threads/Mutex.hpp>
#include <cat/io/MappedFile.hpp>
#include <fstream>

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


static const int SETTINGS_STRMAX = 32;

class CAT_EXPORT SettingsHashKey
{
protected:
	NulTermFixedStr<SETTINGS_STRMAX> _key;
	u32 _hash;

public:
	SettingsHashKey(const char *key, int len)
	{
		_key.SetFromRangeString(key, len);

		_hash = MurmurHash(key, len).Get32();
	}

	//CAT_INLINE virtual ~SettingsHashKey() {}

	CAT_INLINE u32 Hash() { return _hash; }

	bool operator==(SettingsHashKey &rhs)
	{
		return iStrEqual(rhs._key, _key);
	}
};

class CAT_EXPORT SettingsHashItem : public SettingsHashKey
{
	friend class SettingsHashTable;

protected:
	NulTermFixedStr<SETTINGS_STRMAX> _value;
	SettingsHashTable _children;

public:
	SettingsHashItem(const char *key, int len, const char *value)
		: SettingsHashKey(key, len)
	{
		_value.SetFromRangeString(value, len);
	}

	//CAT_INLINE virtual ~SettingsHashItem() {}

	CAT_INLINE SettingsHashTable &GetChildren() { return _children; }

	CAT_INLINE int GetValueInt() { return atoi(_value); }

	CAT_INLINE const char *GetValueStr() { return _value; }

	CAT_INLINE void SetValueStr(const char *value)
	{
		_value.SetFromNulTerminatedString(value);
	}

	CAT_INLINE void SetValueInt(int ivalue)
	{
		char value[13];

		SetValueStr(itoa(ivalue, value, 10));
	}
};

class CAT_EXPORT SettingsHashTable
{
	CAT_NO_COPY(SettingsHashTable);

	static const u32 PREALLOC = 16;
	static const u32 GROW_THRESH = 2;
	static const u32 GROW_RATE = 2;

	u32 _allocated, _used;
	SettingsHashItem **_items;

	void Grow()
	{
		u32 old_size = _allocated;

		// Allocate larger array
		u32 new_size = old_size * GROW_RATE;
		if (new_size < PREALLOC) new_size = PREALLOC;
		SettingsHashItem **new_items = new SettingsHashItem*[new_size];
		if (!new_items) return;

		// Zero new items
		for (u32 ii = 0; ii < new_size; ++ii)
			new_items[ii] = 0;

		// Move old items to the new array
		u32 mask = new_size - 1;
		for (u32 ii = 0; ii < old_size; ++ii)
		{
			SettingsHashItem *item = _items[ii];
			if (item) new_items[item->Hash() & mask] = item;
		}

		// Free old array
		if (_items) delete []_items;

		_items = new_items;
		_allocated = new_size;
	}

public:
	SettingsHashTable()
	{
		_items = 0;
		_allocated = 0;
		_used = 0;
	}

	~SettingsHashTable()
	{
		if (_items)
		{
			// Free any items
			for (u32 ii = 0; ii < _allocated; ++ii)
			{
				SettingsHashItem *item = _items[ii];
				if (item) delete item;
			}

			// Free the array
			delete []_items;
		}
	}

	void Insert(SettingsHashItem *new_item)
	{
		// Grow if needed
		if (_used * GROW_THRESH >= _allocated)
			Grow();

		// Insert at first open table index after hash
		u32 hash = new_item->Hash();
		u32 mask = _allocated - 1;
		u32 index = hash & mask;

		while (_items[index])
			index = (index + 1) & mask;

		_items[index] = new_item;
	}

	SettingsHashItem *Lookup(SettingsHashKey &key)
	{
		if (!_allocated) return 0;

		// Search used table indices after hash
		u32 hash = key.Hash();
		u32 mask = _allocated - 1;
		u32 index = hash & mask;

		SettingsHashItem *item;
		while ((item = _items[index]))
		{
			if (hash == item->Hash() &&
				key == *item)
			{
				return item;
			}

			index = (index + 1) & mask;
		}
	}
};




enum SettingsValueFlags
{
	CAT_SETTINGS_FILLED = 1,	// s[] array has been set
	CAT_SETTINGS_INT = 2,		// value has been promoted to int 'i'
};

struct SettingsValue
{
	u8 flags;		// sum of SettingsValueFlags
	char s[256];	// always nul-terminated
	int i;
};


class CAT_EXPORT SettingsKey
{
	CAT_NO_COPY(SettingsKey);

public:
	SettingsKey(SettingsKey *lnode, SettingsKey *gnode, const char *name);
	~SettingsKey();

public:
	SettingsKey *lnode, *gnode;

	char name[64]; // not necessarily nul-terminated

	SettingsValue value;

public:
	void write(std::ofstream &file);
};


// User settings manager
class CAT_EXPORT Settings : public RefSingleton<Settings>
{
	void OnInitialize();
	void OnFinalize();

	Mutex _lock;

	static const u32 KEY_HASH_SALT = 0xbaddecaf;
	static const int SETTINGS_HASH_BINS = 256;

	SettingsKey *_hbtrees[SETTINGS_HASH_BINS]; // hash table of binary trees

	bool _readSettings;	// Flag set when settings have been read from disk
	bool _modified;		// Flag set when settings have been modified since last write

	std::string _settings_file;

	SettingsKey *addKey(const char *name);
	SettingsKey *getKey(const char *name);

	SettingsKey *initInt(const char *name, int n, bool overwrite);
	SettingsKey *initStr(const char *name, const char *value, bool overwrite);

	void clear();

public:
	void readSettingsFromFile(const char *file_path = "settings.txt", const char *override_file = "override.txt");
	void readSettingsFromBuffer(SequentialFileReader &sfile);
	void write();

	int getInt(const char *name);
	const char *getStr(const char *name);

	int getInt(const char *name, int init);
	const char *getStr(const char *name, const char *init);

	void setInt(const char *name, int n);
	void setStr(const char *name, const char *value);
};


} // namespace cat

#endif // CAT_SETTINGS_HPP
