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


// Calculate settings key hash
static u32 GetSettingsKeyHash(const char *key, int len)
{
	char lcase[SETTINGS_STRMAX];

	if (len > SETTINGS_STRMAX)
		len = SETTINGS_STRMAX;

	// Copy key to temporary buffer all lower-case
	for (int ii = 0; ii < len; ++ii)
	{
		char ch = key[ii];

		if (ch >= 'A' && ch <= 'Z')
			ch += 'a' - 'Z';

		lcase[ii] = ch;
	}

	// Calculate hash from lower-case version
	return MurmurHash(lcase, len).Get32();
}


//// SettingsKeyInput

SettingsKeyInput::SettingsKeyInput(const char *key)
{
	int len = (int)strlen(key);

	_hash = GetSettingsKeyHash(key, len);
	_key = key;
	_len = len;
}

SettingsKeyInput::SettingsKeyInput(const char *key, int len)
{
	_hash = GetSettingsKeyHash(key, len);
	_key = key;
	_len = len;
}


//// SettingsHashKey

SettingsHashKey::SettingsHashKey(const SettingsKeyInput &key)
{
	_key.SetFromRangeString(key.Key(), key.Length());
	_hash = key.Hash();
	_len = key.Length();
}


//// SettingsHashValue

SettingsHashValue::SettingsHashValue(const char *value, int len)
{
	_value.SetFromRangeString(value, len);
}


//// SettingsHashItem

SettingsHashItem::SettingsHashItem(const SettingsKeyInput &key)
	: SettingsHashKey(key)
{
}

SettingsHashItem::SettingsHashItem(const SettingsKeyInput &key, const char *value, int value_len)
	: SettingsHashKey(key), SettingsHashValue(value, value_len)
{
}


//// SettingsHashTable

bool SettingsHashTable::Grow()
{
	// Calculate growth rate
	u32 old_size = _allocated;
	u32 new_size = old_size * GROW_RATE;
	if (new_size < PREALLOC) new_size = PREALLOC;

	CAT_INANE("SettingsHashTable") << "Growing to " << new_size << " buckets";

	// Allocate larger bucket array
	SList *new_buckets = new SList[new_size];
	if (!new_buckets) return false;

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

	return true;
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

SettingsHashItem *SettingsHashTable::Lookup(const SettingsKeyInput &key)
{
	// If nothing allocated,
	if (!_allocated) return 0;

	// Search used table indices after hash
	u32 ii = key.Hash() & (_allocated - 1);

	// For each item in the selected bucket,
	for (iter jj = _buckets[ii]; jj; ++jj)
	{
		// If the key matches,
		if (*jj == key)
		{
			// Found it!
			return jj;
		}
	}

	return 0;
}

SettingsHashItem *SettingsHashTable::Create(const SettingsKeyInput &key)
{
	// Check if it exists already
	SettingsHashItem *item = Lookup(key);
	if (item) return item;

	// If first allocation fails,
	if (!_buckets && !Grow()) return 0;

	// If cannot create an item,
	item = new SettingsHashItem(key);
	if (!item) return 0;

	// If time to grow,
	if (_used * GROW_THRESH >= _allocated)
	{
		// If grow fails,
		if (!Grow())
		{
			delete item;
			return 0;
		}
	}

	// Insert in bucket corresponding to hash low bits
	u32 bucket_index = key.Hash() & (_allocated - 1);
	_buckets[bucket_index].PushFront(item);

	// Increment used count to keep track of when to grow
	++_used;

	return item;
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

	do
	{
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
			++key_len;
		}

		memcpy(write_key, parsed_line.first, parsed_line.first_len);
		write_key[parsed_line.first_len] = '\0';

		// If second token is set,
		if (parsed_line.second_len)
		{
			// Add this path to the hash table
			SettingsHashItem *item = _table.Create(SettingsKeyInput(root_key, key_len));
			if (item) item->SetValueRangeStr(parsed_line.second, parsed_line.second_len);
		}

		int depth = parsed_line.depth;

		// For each line until EOF,
		while (readLine(sfile, parsed_line))
		{
			// Skip blank lines
			if (parsed_line.first_len == 0) continue;

			// If new line depth is at or beneath the root,
			if (root_depth >= parsed_line.depth)
				eof = 1; // Pass it back to the root to handle
			// If new line is a child of current depth,
			else if (depth < parsed_line.depth)
			{
				// Otherwise the new line depth is deeper, so recurse and add onto the key
				eof = readTokens(sfile, parsed_line, root_key, key_len, depth);

				// If not EOF,
				if (eof != 0)
				{
					// If new line depth is at the same level as current token,
					if (root_depth < parsed_line.depth)
						eof = 2; // Repeat whole routine again at this depth
					else
						eof = 1; // Pass it back to the root to handle
				}
			}
			else // New line depth is at about the same level as current
				eof = 2; // Repeat whole routine again at this depth

			break;
		}

		// Remove appended token
		root_key[root_key_len] = '\0';
	} while (eof == 2);

	return eof; // EOF
}

void Settings::readFile(SequentialFileReader &sfile)
{
	char root_key[SETTINGS_STRMAX+1];
	root_key[0] = '\0';

	// For each line until EOF,
	ParsedLine parsed_line;
	if (!readLine(sfile, parsed_line))
		return;

	// Bump tokens back to the next level while not EOF
	while (1 == readTokens(sfile, parsed_line, root_key, 0, 0));
}

int Settings::getInt(const char *name, int default_value)
{
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

void Settings::write()
{
	// TODO
}
