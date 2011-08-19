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


//#define SETTINGS_VERBOSE /* dump extra settings information to the console for debugging */


SettingsKey::SettingsKey(SettingsKey *lnodei, SettingsKey *gnodei, const char *namei)
{
	gnode = gnodei;
	lnode = lnodei;
	CAT_STRNCPY(name, namei, sizeof(name));

	value.flags = 0;
}

SettingsKey::~SettingsKey()
{
	if (lnode) delete lnode;
	if (gnode) delete gnode;
}

void SettingsKey::write(std::ofstream &file)
{
	if (lnode) lnode->write(file);

	// Only write keys that had a default value and
	// have been changed from the default value.
	if (value.flags & CAT_SETTINGS_FILLED)
	{
		if (value.flags & CAT_SETTINGS_INT)
		{
			file << name << " = " << value.i << endl;
		}
		else
		{
			file << name << " = " << value.s << endl;
		}
	}

	if (gnode) gnode->write(file);
}


Settings::Settings()
{
	CAT_OBJCLR(_hbtrees);

	_readSettings = false;
	_modified = false;
}

bool Settings::OnRefObjectInitialize()
{

}

void Settings::OnRefObjectDestroy()
{

}

bool Settings::OnRefObjectFinalize()
{

}

SettingsKey *Settings::addKey(const char *name)
{
	u32 treekey = MurmurHash(name, (int)strlen(name)+1, KEY_HASH_SALT).Get32() % SETTINGS_HASH_BINS;
	SettingsKey *key = _hbtrees[treekey];

	if (!key)
		return _hbtrees[treekey] = new SettingsKey(0, 0, name);

	CAT_FOREVER
	{
		int cmp = strncmp(key->name, name, sizeof(key->name));
		if (!cmp) return key;

		if (cmp > 0)
		{
			if (!key->lnode)
				return key->lnode = new SettingsKey(0, 0, name);
			else
				key = key->lnode;
		}
		else
		{
			if (!key->gnode)
				return key->gnode = new SettingsKey(0, 0, name);
			else
				key = key->gnode;
		}
	}
}

SettingsKey *Settings::getKey(const char *name)
{
	u32 treekey = MurmurHash(name, (int)strlen(name)+1, KEY_HASH_SALT).Get32() % SETTINGS_HASH_BINS;
	SettingsKey *key = _hbtrees[treekey];

	while (key)
	{
		int cmp = strncmp(key->name, name, sizeof(key->name));
		if (!cmp) return key;

		key = cmp > 0 ? key->lnode : key->gnode;
	}

	return 0;
}

void Settings::clear()
{
	for (int ii = 0; ii < SETTINGS_HASH_BINS; ++ii)
	{
		SettingsKey *root = _hbtrees[ii];
		if (root)
		{
			_hbtrees[ii] = 0;
			delete root;
		}
	}
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
#ifdef SETTINGS_VERBOSE
			CAT_INANE("Settings") << "Read: (" << key->name << ") = (" << key->value.s << ")";
#endif
		}
	}
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

#ifdef SETTINGS_VERBOSE
	CAT_INANE("Settings") << "Read: " << file_path;
#endif

	readSettingsFromBuffer(sfile);

	// If override file exists,
	if (sfile.Open(override_file))
	{
#ifdef SETTINGS_VERBOSE
		CAT_INANE("Settings") << "Read: " << override_file;
#endif

		readSettingsFromBuffer(sfile);
	}

	// Delete the override settings file if settings request it
	if (getInt("override.unlink", 0))
	{
		_unlink(override_file);
		setInt("override.unlink", 0);
	}

	_readSettings = true;
}

void Settings::write()
{
	AutoMutex lock(_lock);

	if (_readSettings && !_modified)
	{
#ifdef SETTINGS_VERBOSE
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

#ifdef SETTINGS_VERBOSE
	CAT_INANE("Settings") << "Write: Saved " << _settings_file;
#endif

	_modified = false;
}

int Settings::getInt(const char *name)
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
