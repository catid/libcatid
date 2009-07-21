/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

// 06/11/09 part of libcat-1.0

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <cat/Singleton.hpp>
#include <fstream>

namespace cat {


enum SettingsValueFlags
{
    SETTINGS_FILLED = 1, // s[] array has been set
    SETTINGS_INT = 2, // value has been promoted to int 'i'
};

struct SettingsValue
{
    u8 flags; // sum of SettingsValueFlags
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
    bool modified; // Flag set when settings have been modified since last write

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
