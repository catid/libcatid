/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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
#include <cat/io/MMapFile.hpp>
#include <cat/io/Logging.hpp>
#include <cstring>
using namespace cat;
using namespace std;


#define ONLY_WRITE_NONDEFAULT_KEYS
#define SETTINGS_VERBOSE /* dump extra settings information to the console for debugging */
#define SETTINGS_FILE "settings.txt"
#define OVERRIDE_SETTINGS_FILE "override.txt"


SettingsKey::SettingsKey(SettingsKey *lnodei, SettingsKey *gnodei, const char *namei)
{
    gnode = gnodei;
    lnode = lnodei;
    CAT_STRNCPY(name, namei, sizeof(name));

    value.flags = 0;
    def.flags = 0;
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
    if (value.flags & def.flags & CAT_SETTINGS_FILLED)
    {
        if (value.flags & CAT_SETTINGS_INT)
        {
#ifdef ONLY_WRITE_NONDEFAULT_KEYS
            if (def.i != value.i)
#endif
                file << name << " = " << value.i << endl;
        }
        else
        {
#ifdef ONLY_WRITE_NONDEFAULT_KEYS
            if (strncmp(value.s, def.s, sizeof(value.s)))
#endif
                file << name << " = " << value.s << endl;
        }
    }

    if (gnode) gnode->write(file);
}


Settings::Settings()
{
    CAT_OBJCLR(hbtrees);

    readSettings = false;
    modified = false;
}

SettingsKey *Settings::addKey(const char *name)
{
    u8 treekey = (u8)name[0] % SETTINGS_HASH_BINS;
    SettingsKey *key = hbtrees[treekey];

    if (!key)
        return hbtrees[treekey] = new SettingsKey(0, 0, name);

    for (;;)
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
    u8 treekey = (u8)name[0] % SETTINGS_HASH_BINS;
    SettingsKey *key = hbtrees[treekey];

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
        SettingsKey *root = hbtrees[ii];
        if (root)
        {
            hbtrees[ii] = 0;
            delete root;
        }
    }
}

void Settings::read(const char *data, int len)
{
	AutoMutex lock(_lock);

    BufferTok bt(data, len);

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
            INANE("Settings") << "Read: (" << key->name << ") = (" << key->value.s << ")";
#endif
        }
    }
}

void Settings::read()
{
	AutoMutex lock(_lock);

    MMapFile sfile(SETTINGS_FILE);
    if (!sfile.good())
    {
        WARN("Settings") << "Read: Unable to open " << SETTINGS_FILE;
        return;
    }

#ifdef SETTINGS_VERBOSE
    INANE("Settings") << "Read: " << SETTINGS_FILE;
#endif

    read((const char*)sfile.look(), sfile.size());

    MMapFile ofile(OVERRIDE_SETTINGS_FILE);
    if (ofile.good())
    {
#ifdef SETTINGS_VERBOSE
        INANE("Settings") << "Read: " << OVERRIDE_SETTINGS_FILE;
#endif

        read((const char*)ofile.look(), ofile.size());
    }

    // Delete the override settings file if settings request it
    if (getInt("override.unlink", 0))
    {
        _unlink(OVERRIDE_SETTINGS_FILE);
        setInt("override.unlink", 0);
    }

    readSettings = true;
}

void Settings::write()
{
	AutoMutex lock(_lock);

#ifdef ONLY_WRITE_NONDEFAULT_KEYS
    if (readSettings && !modified)
    {
#ifdef SETTINGS_VERBOSE
        INANE("Settings") << "Skipped writing unmodified settings";
#endif
        return;
    }
#endif

    ofstream file(SETTINGS_FILE);
    if (!file)
    {
        WARN("Settings") << "Write: Unable to open settings.txt";
        return;
    }

    file << "This file is regenerated on shutdown and reread on startup" << endl << endl;

    for (int ii = 0; ii < SETTINGS_HASH_BINS; ++ii)
        if (hbtrees[ii]) hbtrees[ii]->write(file);

#ifdef SETTINGS_VERBOSE
    INANE("Settings") << "Write: Saved settings.txt";
#endif

    modified = false;
}

int Settings::getInt(const char *name)
{
	AutoMutex lock(_lock);

    SettingsKey *key = getKey(name);
    return key ? key->value.i : 0;
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

    SettingsKey *key = initInt(name, init);
    return key ? key->value.i : 0;
}

const char *Settings::getStr(const char *name, const char *init)
{
	AutoMutex lock(_lock);

    SettingsKey *key = initStr(name, init);
    return key ? key->value.s : "";
}

void Settings::setInt(const char *name, int n)
{
	AutoMutex lock(_lock);

    SettingsKey *key = getKey(name);
    if (key)
    {
        key->value.i = n;
        modified = true;
    }
}

void Settings::setStr(const char *name, const char *value)
{
	AutoMutex lock(_lock);

    SettingsKey *key = getKey(name);
    if (key)
    {
        CAT_STRNCPY(key->value.s, value, sizeof(key->value.s));
        modified = true;
    }
}

SettingsKey *Settings::initInt(const char *name, int n)
{
    SettingsKey *key = addKey(name);

    if (!(key->value.flags & CAT_SETTINGS_FILLED))
        key->value.i = n;
    else if (!(key->value.flags & CAT_SETTINGS_INT))
        key->value.i = atoi(key->value.s);
    key->value.flags = CAT_SETTINGS_FILLED|CAT_SETTINGS_INT;

    key->def.i = n;
    key->def.flags = CAT_SETTINGS_FILLED|CAT_SETTINGS_INT;

    return key;
}

SettingsKey *Settings::initStr(const char *name, const char *value)
{
    SettingsKey *key = addKey(name);

    if (!(key->value.flags & CAT_SETTINGS_FILLED))
        CAT_STRNCPY(key->value.s, value, sizeof(key->value.s));
    key->value.flags = CAT_SETTINGS_FILLED;

    CAT_STRNCPY(key->def.s, value, sizeof(key->def.s));
    key->def.flags = CAT_SETTINGS_FILLED;

    return key;
}
