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
#include <cat/lang/LinkedLists.hpp>
#include <cat/lang/Strings.hpp>
#include <cat/lang/RefSingleton.hpp>
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


//// SettingsHashKey

class CAT_EXPORT SettingsHashKey
{
protected:
	NulTermFixedStr<SETTINGS_STRMAX> _key;
	u32 _hash;

public:
	SettingsHashKey(const char *key, int len);

	//CAT_INLINE virtual ~SettingsHashKey() {}

	CAT_INLINE u32 Hash() { return _hash; }

	CAT_INLINE bool operator==(SettingsHashKey &rhs)
	{
		return iStrEqual(rhs._key, _key);
	}
};


//// SettingsHashItem

class CAT_EXPORT SettingsHashItem : public SettingsHashKey, public SListItem
{
	friend class SettingsHashTable;

protected:
	NulTermFixedStr<SETTINGS_STRMAX> _value;

public:
	SettingsHashItem(const char *key, int len, const char *value);

	//CAT_INLINE virtual ~SettingsHashItem() {}

	CAT_INLINE int GetValueInt() { return atoi(_value); }

	CAT_INLINE const char *GetValueStr() { return _value; }

	CAT_INLINE void SetValueStr(const char *value)
	{
		_value.SetFromNulTerminatedString(value);
	}

	CAT_INLINE void SetValueInt(int ivalue)
	{
		char value[13];

		DecToString(ivalue, value);

		SetValueStr(value);
	}
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

	void Grow();

public:
	SettingsHashTable();
	~SettingsHashTable();

	void Insert(SettingsHashItem *new_item);
	SettingsHashItem *Lookup(SettingsHashKey &key);

	// Iterator
	class CAT_EXPORT Iterator
	{
		u32 _remaining;
		DListForward *_bucket;
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


//// Settings

class CAT_EXPORT Settings : public RefSingleton<Settings>
{
	void OnInitialize();
	void OnFinalize();

	Mutex _lock;

	SettingsHashTable _table;

	bool _readSettings;	// Flag set when settings have been read from disk
	bool _modified;		// Flag set when settings have been modified since last write

	std::string _settings_file;

	void clear();
	void readSettingsFromBuffer(SequentialFileReader &sfile);

public:
	void readSettingsFromFile(const char *file_path = CAT_SETTINGS_FILE, const char *override_file = CAT_SETTINGS_OVERRIDE_FILE);
	void write();

	int getInt(const char *name, int default_value = 0);
	const char *getStr(const char *name, const char *default_value = "");

	void setInt(const char *name, int value);
	void setStr(const char *name, const char *value);
};


} // namespace cat

#endif // CAT_SETTINGS_HPP
