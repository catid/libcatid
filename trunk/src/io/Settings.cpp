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

#include <cat/io/Settings.hpp>
#include <cat/parse/BufferTok.hpp>
#include <cat/io/Logging.hpp>
#include <cstring>
#include <cstdlib>
#include <cat/hash/Murmur.hpp>
using namespace cat;
using namespace std;


//// SettingsHashKey

SettingsHashKey::SettingsHashKey(const char *key, int len)
{
	_key.SetFromRangeString(key, len);

	_hash = MurmurHash(key, len).Get32();
}


//// SettingsHashItem

SettingsHashItem::SettingsHashItem(const char *key, int len, const char *value)
	: SettingsHashKey(key, len)
{
	_value.SetFromRangeString(value, len);
}


//// SettingsHashTable

void SettingsHashTable::Grow()
{
	u32 old_size = _allocated;

	// Allocate larger bucket array
	u32 new_size = old_size * GROW_RATE;
	if (new_size < PREALLOC) new_size = PREALLOC;
	DListForward *new_buckets = new DListForward[new_size];
	if (!new_buckets) return;

	// For each bucket,
	u32 mask = new_size - 1;
	for (u32 jj = 0; jj < old_size; ++jj)
	{
		// For each bucket item,
		for (iter next = 0, ii = _buckets[jj].head(); ii; ii = next)
		{
			next = ii.GetNext();

			new_buckets[ii->Hash() & mask].PushFront(ii);
		}
	}

	// Free old array
	if (_buckets) delete []_buckets;

	_buckets = new_buckets;
	_allocated = new_size;
}

SettingsHashTable::SettingsHashTable()
{
	_buckets = 0;
	_allocated = 0;
	_used = 0;
}

SettingsHashTable::~SettingsHashTable()
{
	// If any buckets are allocated,
	if (_buckets)
	{
		// For each allocated bucket,
		for (u32 ii = 0; ii < _allocated; ++ii)
		{
			DListForward &bucket = _buckets[ii];

			// If bucket is not empty,
			if (!bucket.empty())
			{
				// For each item in the bucket,
				for (iter next = 0, ii = bucket.head(); ii; ii = next)
				{
					next = ii.GetNext();

					// Free item
					delete ii;
				}
			}
		}

		// Free the array
		delete []_buckets;
	}
}

void SettingsHashTable::Insert(SettingsHashItem *new_item)
{
	// If time to grow,
	if (_used * GROW_THRESH >= _allocated)
		Grow();

	// Insert at first open table index after hash
	u32 hash = new_item->Hash();
	u32 mask = _allocated - 1;
	u32 ii = hash & mask;

	// Push into the bucket list
	_buckets[ii].PushFront(new_item);

	++_used;
}

SettingsHashItem *SettingsHashTable::Lookup(SettingsHashKey &key)
{
	// If nothing allocated,
	if (!_allocated) return 0;

	// Search used table indices after hash
	u32 hash = key.Hash();
	u32 mask = _allocated - 1;
	u32 ii = hash & mask;

	// For each item in the selected bucket,
	for (iter jj = _buckets[ii].head(); jj; ++jj)
	{
		// If the hash matches and the key matches,
		if (jj->Hash() == hash &&
			*jj == key)
		{
			// Found it!
			return jj;
		}
	}
}


//// SettingsHashTable::Iterator

void SettingsHashTable::Iterator::IterateNext()
{
	if (_ii)
	{
		++_ii;

		if (_ii) return;
	}

	while (_remaining)
	{
		--_remaining;
		++_bucket;

		_ii = _bucket->head();

		if (_ii) return;
	}
}

SettingsHashTable::Iterator::Iterator(SettingsHashTable &head)
{
	_remaining = head._allocated;
	_bucket = head._buckets;
	_ii = _bucket->head();

	if (!_ii)
	{
		IterateNext();
	}
}


//// Settings

CAT_REF_SINGLETON(Settings);

void Settings::OnInitialize()
{
	_readSettings = false;
	_modified = false;

	readSettingsFromFile();
}

void Settings::OnFinalize()
{
	write();
}

void Settings::clear()
{

}

void Settings::readSettingsFromBuffer(SequentialFileReader &sfile)
{

}

void Settings::readSettingsFromFile(const char *file_path, const char *override_file)
{
	AutoMutex lock(_lock);

	_settings_file = file_path;

	SequentialFileReader sfile;

	if (!sfile.Open(file_path))
	{
		CAT_WARN("Settings") << "Read: Unable to open " << file_path;
		return;
	}

#ifdef CAT_SETTINGS_VERBOSE
	CAT_INANE("Settings") << "Read: " << file_path;
#endif

	readSettingsFromBuffer(sfile);

	// If override file exists,
	if (sfile.Open(override_file))
	{
#ifdef CAT_SETTINGS_VERBOSE
		CAT_INANE("Settings") << "Read: " << override_file;
#endif

		readSettingsFromBuffer(sfile);
	}

	// Delete the override settings file if settings request it
	if (getInt("Override.Unlink", 0))
	{
		_unlink(override_file);
		setInt("Override.Unlink", 0);
	}

	_readSettings = true;
}

void Settings::write()
{

}

int Settings::getInt(const char *name, int default_value)
{
	AutoMutex lock(_lock);

	SettingsKey *key = getKey(name);
	if (!key) return 0;

	if (!(key->value.flags & CAT_SETTINGS_INT))
	{
		key->value.i = atoi(key->value.s);
		key->value.flags |= CAT_SETTINGS_INT;
	}

	return key->value.i;
}

const char *Settings::getStr(const char *name, const char *default_value)
{

}

void Settings::setInt(const char *name, int value)
{

}

void Settings::setStr(const char *name, const char *value)
{

}

void Settings::readSettingsFromBuffer(SequentialFileReader &sfile)
{
	AutoMutex lock(_lock);

	u32 length = (u32)sfile.GetLength();
	BufferTok bt((char*)sfile.Read(length), length);

	char keyName[256];

	while (!!bt)
	{
		bt['='] >> keyName;

		if (*keyName && !bt.onNewline())
		{
			SettingsKey *key = addKey(keyName);
			if (!key) continue;

			key->value.flags |= CAT_SETTINGS_FILLED;
			bt() >> key->value.s;
#ifdef CAT_SETTINGS_VERBOSE
			CAT_INANE("Settings") << "Read: (" << key->name << ") = (" << key->value.s << ")";
#endif
		}
	}
}

void Settings::write()
{
	AutoMutex lock(_lock);

	if (_readSettings && !_modified)
	{
#ifdef CAT_SETTINGS_VERBOSE
		CAT_INANE("Settings") << "Skipped writing unmodified settings";
#endif
		return;
	}

	ofstream file(_settings_file.c_str());
	if (!file)
	{
		CAT_WARN("Settings") << "Write: Unable to open " << _settings_file;
		return;
	}

	file << "This file is regenerated on shutdown and reread on startup" << endl << endl;

	for (int ii = 0; ii < SETTINGS_HASH_BINS; ++ii)
		if (_hbtrees[ii]) _hbtrees[ii]->write(file);

#ifdef CAT_SETTINGS_VERBOSE
	CAT_INANE("Settings") << "Write: Saved " << _settings_file;
#endif

	_modified = false;
}

int Settings::getInt(const char *name)
{
}

const char *Settings::getStr(const char *name)
{
	AutoMutex lock(_lock);

	SettingsKey *key = getKey(name);
	return key ? key->value.s : "";
}

int Settings::getInt(const char *name, int init)
{
	AutoMutex lock(_lock);

	SettingsKey *key = initInt(name, init, false);
	return key ? key->value.i : 0;
}

const char *Settings::getStr(const char *name, const char *init)
{
	AutoMutex lock(_lock);

	SettingsKey *key = initStr(name, init, false);
	return key ? key->value.s : "";
}

void Settings::setInt(const char *name, int n)
{
	AutoMutex lock(_lock);

	initInt(name, n, true);
}

void Settings::setStr(const char *name, const char *value)
{
	AutoMutex lock(_lock);

	initStr(name, value, true);
}

SettingsKey *Settings::initInt(const char *name, int n, bool overwrite)
{
	SettingsKey *key = addKey(name);

	if (overwrite || !(key->value.flags & CAT_SETTINGS_FILLED))
	{
		key->value.i = n;
		key->value.flags = CAT_SETTINGS_FILLED|CAT_SETTINGS_INT;

		_modified = true;
	}
	else if (!(key->value.flags & CAT_SETTINGS_INT))
	{
		key->value.i = atoi(key->value.s);
		key->value.flags = CAT_SETTINGS_FILLED|CAT_SETTINGS_INT;
	}

	return key;
}

SettingsKey *Settings::initStr(const char *name, const char *value, bool overwrite)
{
	SettingsKey *key = addKey(name);

	if (overwrite || !(key->value.flags & CAT_SETTINGS_FILLED))
	{
		CAT_STRNCPY(key->value.s, value, sizeof(key->value.s));
		key->value.flags = CAT_SETTINGS_FILLED;

		_modified = true;
	}

	return key;
}
