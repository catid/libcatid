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

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <cat/Singleton.hpp>
#include <fstream>

namespace cat {


enum SettingsValueFlags
{
    SETTINGS_FILLED = 1, // s[] array has been set
    SETTINGS_INT = 2,    // value has been promoted to int 'i'
};

struct SettingsValue
{
    u8 flags;    // sum of SettingsValueFlags
    char s[256]; // always nul-terminated
    int i;
};


class SettingsKey
{
public:
    SettingsKey(SettingsKey *lnode, SettingsKey *gnode, const char *name);
    ~SettingsKey();

public:
    SettingsKey *lnode, *gnode;

    char name[32]; // not necessarily nul-terminated

    SettingsValue value, def; // value of key and default value of key

public:
    void write(std::ofstream &file);
};


// User settings manager
class Settings : public Singleton<Settings>
{
    CAT_SINGLETON(Settings);

protected:
    static const int SETTINGS_HASH_BINS = 256;

    SettingsKey *hbtrees[SETTINGS_HASH_BINS]; // hash table of binary trees

    bool readSettings; // Flag set when settings have been read from disk
    bool modified;     // Flag set when settings have been modified since last write

protected:
    SettingsKey *addKey(const char *name);
    SettingsKey *getKey(const char *name);

    SettingsKey *initInt(const char *name, int n);
    SettingsKey *initStr(const char *name, const char *value);

    void clear();
    
public:
    void read();
    void read(const char *data, int len);
    void write();

public:
    int getInt(const char *name);
    const char *getStr(const char *name);

    int getInt(const char *name, int init);
    const char *getStr(const char *name, const char *init);

    void setInt(const char *name, int n);
    void setStr(const char *name, const char *value);
};


} // namespace cat

#endif // SETTINGS_HPP
