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


//// SettingsHashValue

SettingsHashValue::SettingsHashValue(const char *value, int len)
{
	_value.SetFromRangeString(value, len);
}


//// SettingsHashItem

SettingsHashItem::SettingsHashItem(const char *key, int key_len, const char *value, int value_len)
	: SettingsHashKey(key, key_len), SettingsHashValue(value, value_len)
{
}


//// SettingsHashTable

void SettingsHashTable::Grow()
{
	CAT_INANE("SettingsHashTable") << "Growing";

	u32 old_size = _allocated;

	// Allocate larger bucket array
	u32 new_size = old_size * GROW_RATE;
	if (new_size < PREALLOC) new_size = PREALLOC;
	SList *new_buckets = new SList[new_size];
	if (!new_buckets) return;

	// For each bucket,
	u32 mask = new_size - 1;
	for (u32 jj = 0; jj < old_size; ++jj)
	{
		// For each bucket item,
		for (iter ii = _buckets[jj]; ii; ++ii)
		{
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
			SList &bucket = _buckets[ii];

			// If bucket is not empty,
			if (!bucket.empty())
			{
				// For each item in the bucket,
				for (iter ii = bucket; ii; ++ii)
				{
					// Free item
					delete ii;
				}
			}
		}

		// Free the array
		delete []_buckets;
	}
}

void SettingsHashTable::Set(const char *key, int key_len, const char *value, int value_len)
{
	CAT_INANE("SettingsHashTable") << "Set[" << key << "] = " << value;

	u32 hash = MurmurHash(key, key_len).Get32();
	u32 mask = _allocated - 1;
	u32 ii = hash & mask;

	// For each item in the selected bucket,
	for (iter jj = _buckets[ii]; jj; ++jj)
	{
		// If the hash matches and the key matches,
		if (jj->Hash() == hash &&
			*jj == key)
		{
			// Found it!
			jj->SetValueStr(value);
			return;
		}
	}

	SettingsHashItem *item = new SettingsHashItem(key, key_len, value, value_len);
	if (!item) return;

	// If time to grow,
	if (_used * GROW_THRESH >= _allocated)
	{
		Grow();

		mask = _allocated - 1;
		ii = hash & mask;
	}

	// Insert at first open table index after hash
	_buckets[ii].PushFront(item);

	++_used;
}

SettingsHashItem *SettingsHashTable::Get(const char *key, int key_len)
{
	CAT_INANE("SettingsHashTable") << "Get[" << key << "]";

	// If nothing allocated,
	if (!_allocated) return 0;

	// Search used table indices after hash
	u32 hash = MurmurHash(key, key_len).Get32();
	u32 mask = _allocated - 1;
	u32 ii = hash & mask;

	// For each item in the selected bucket,
	for (iter jj = _buckets[ii]; jj; ++jj)
	{
		// If the hash matches and the key matches,
		if (jj->Hash() == hash &&
			*jj == key)
		{
			// Found it!
			return jj;
		}
	}

	return 0;
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

		_ii = *_bucket;

		if (_ii) return;
	}
}

SettingsHashTable::Iterator::Iterator(SettingsHashTable &head)
{
	_remaining = head._allocated;
	_bucket = head._buckets;
	_ii = *_bucket;

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

	read();
}

void Settings::OnFinalize()
{
	write();
}

void Settings::read(const char *file_path, const char *override_file)
{
	AutoWriteLock lock(_lock);

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

	readFile(sfile);

	// If override file exists,
	if (sfile.Open(override_file))
	{
#ifdef CAT_SETTINGS_VERBOSE
		CAT_INANE("Settings") << "Read: " << override_file;
#endif

		readFile(sfile);
	}

	// Delete the override settings file if settings request it
	if (getInt("Override.Unlink", 0))
	{
		_unlink(override_file);
		setInt("Override.Unlink", 0);
	}

	_readSettings = true;
}

bool Settings::readLine(SequentialFileReader &sfile, ParsedLine &parsed_line)
{
	static char line[512];
	int count;

	// For each line in the file,
	if ((count = sfile.ReadLine(line, (int)sizeof(line))) < 0)
		return false;

	// Ignore blank lines (fairly common)
	if (count == 0)
	{
		// Set first_len to zero to indicate no data
		parsed_line.first_len = 0;
		return true;
	}

	// Count the number of tabs and spaces at the front
	char *first = line;
	int tab_count = 0, space_count = 0;

	// While EOL not encountered,
	char ch;
	while ((ch = *first))
	{
		if (ch == ' ')
		{
			++space_count;
			++first;
		}
		else if (ch == '\t')
		{
			++tab_count;
			++first;
		}
		else
		{
			break;
		}
	}

	// If EOL found or non-data line, skip this line
	if (!ch || !IsAlpha(ch))
	{
		// Set first_len to zero to indicate no data
		parsed_line.first_len = 0;
		return true;
	}

	/*
		Calculate depth from tab and space count

		Round up front 2 spaces to an extra tab in case just
		the last tab is replaced by spaces and the tab stops
		are set at 2 characters (attempting to be clever about it)
	*/
	int depth = tab_count + (space_count + 2) / 4;
	if (depth > MAX_TAB_RECURSION_DEPTH) depth = MAX_TAB_RECURSION_DEPTH;
	parsed_line.depth = depth;

	// Find the start of whitespace after first token
	char *second = first + 1;

	while ((ch = *second))
	{
		if (ch == ' ' || ch == '\t')
			break;

		++second;
	}

	// Store first token
	parsed_line.first_len = (int)(second - first);
	parsed_line.first = first;

	// If a second token is possible,
	// NOTE: Second token is left pointing at an empty string here if no whitespace was found
	int second_len = 0;

	if (ch)
	{
		// Terminate the first token
		*second++ = '\0';

		// Search for end of whitespace between tokens
		while ((ch = *second))
		{
			if (ch == ' ' || ch == '\t')
				++second;
			else
				break;
		}

		// If second token exists,
		if (ch)
		{
			// Search for end of whitespace between tokens
			char *end = second + 1;

			// For each character until the end of the line,
			while ((ch = *end))
			{
				// On the first whitespace character encountered,
				if (ch == ' ' || ch == '\t')
				{
					// Terminate the second token and ignore the rest
					*end = '\0';
					break;
				}
			}

			// Get length of first token
			second_len = (int)(end - second);
		}
	}

	// First and second tokens, their lengths, and the new depth are determined.
	// The second token may be an empty string but is always a valid string.

	parsed_line.second = second;
	parsed_line.second_len = second_len;

	return true;
}

int Settings::readTokens(SequentialFileReader &sfile, ParsedLine &parsed_line, char *root_key, int root_key_len, int root_depth)
{
	int eof = 0;

	// If there is not enough space to append the first token to the end of the root key,
	if (root_key_len + 1 + parsed_line.first_len > SETTINGS_STRMAX)
	{
		// Signal EOF here to avoid mis-attributing keys
		CAT_WARN("Settings") << "Long line caused settings processing to abort early";
		return 0;
	}

	// Append first token to root key
	int key_len = root_key_len + parsed_line.first_len;
	char *write_key = root_key + root_key_len;

	if (root_key_len > 0)
	{
		root_key[root_key_len] = '.';
		++write_key;
	}

	memcpy(write_key, parsed_line.first, parsed_line.first_len);
	write_key[parsed_line.first_len] = '\0';

	// If second token is set,
	if (parsed_line.second_len)
	{
		// Add this path to the hash table
		_table.Set(root_key, key_len, parsed_line.second, parsed_line.second_len);
	}

	int depth = parsed_line.depth;

	// For each line until EOF,
	while (readLine(sfile, parsed_line))
	{
		// Skip blank lines
		if (parsed_line.first_len == 0) continue;

		// If this is not a child of the root,
		if (root_depth >= parsed_line.depth)
		{
			// Return code to allow parent to process it
			eof = 1;
			break;
		}

		// Process and if EOF was encountered,
		int retval = readTokens(sfile, parsed_line, root_key, key_len, depth);
		if (retval == 0)
		{
			break; // EOF
		}
		else if (retval == 1)
		{
			if (parsed_line.depth <= root_depth)
			{
				eof = 1;
				break;
			}
		}
	}

	// Remove appended token(s)
	root_key[root_key_len] = '\0';

	return eof; // EOF
}

void Settings::readFile(SequentialFileReader &sfile)
{
	char root_key[SETTINGS_STRMAX+1];
	root_key[0] = '\0';

	// For each line until EOF,
	ParsedLine parsed_line;
	while (readLine(sfile, parsed_line))
	{
		int retval;

		do
		{
			retval = readTokens(sfile, parsed_line, root_key, 0, 0);
			if (retval == 0)
				return;
		} while (retval == 1);
	}
}

void Settings::write()
{
	// TODO
}

int Settings::getInt(const char *name, int default_value)
{
	// TODO
	return 0;
}

const char *Settings::getStr(const char *name, const char *default_value)
{
	// TODO
	return "";
}

void Settings::setInt(const char *name, int value)
{
	// TODO
}

void Settings::setStr(const char *name, const char *value)
{
	// TODO
}
