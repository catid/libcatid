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

#include <cat/io/RagdollFile.hpp>
#include <cat/parse/BufferTok.hpp>
#include <cat/io/Logging.hpp>
#include <cat/hash/Murmur.hpp>
#include <fstream>
#include <cstring>
#include <cstdlib>
using namespace cat;
using namespace std;
using namespace ragdoll;


// Keep in synch with MAX_TAB_RECURSION_DEPTH
static const char *TAB_STRING = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";


//// ragdoll::SanitizedKey

static int SanitizeKeyString(const char *key, char *sanitized_string)
{
	char ch, *outs = sanitized_string;
	bool seen_punct = false;

	while ((ch = *key++))
	{
		if (ch >= 'A' && ch <= 'Z')
		{
			if (seen_punct)
			{
				*outs++ = '.';
				seen_punct = false;
			}
			*outs++ = ch + 'a' - 'A';
		}
		else if (ch >= 'a' && ch <= 'z' ||
			ch >= '0' && ch <= '9')
		{
			if (seen_punct)
			{
				*outs++ = '.';
				seen_punct = false;
			}
			*outs++ = ch;
		}
		else
		{
			if (outs != sanitized_string)
				seen_punct = true;
		}
	}

	*outs = '\0';

	return (int)(outs - sanitized_string);
}

static int SanitizeKeyRangeString(const char *key, int len, char *sanitized_string)
{
	char ch, *outs = sanitized_string;
	bool seen_punct = false;

	while (len-- > 0)
	{
		ch = *key++;

		if (ch >= 'A' && ch <= 'Z')
		{
			if (seen_punct)
			{
				*outs++ = '.';
				seen_punct = false;
			}
			*outs++ = ch + 'a' - 'A';
		}
		else if (ch >= 'a' && ch <= 'z' ||
			ch >= '0' && ch <= '9')
		{
			if (seen_punct)
			{
				*outs++ = '.';
				seen_punct = false;
			}
			*outs++ = ch;
		}
		else
		{
			if (outs != sanitized_string)
				seen_punct = true;
		}
	}

	*outs = '\0';

	return (int)(outs - sanitized_string);
}

SanitizedKey::SanitizedKey(const char *key)
{
	_len = SanitizeKeyString(key, _key);
	_hash = MurmurHash(_key, _len).Get32();
}

SanitizedKey::SanitizedKey(const char *key, int len)
{
	_len = SanitizeKeyRangeString(key, len, _key);
	_hash = MurmurHash(_key, _len).Get32();
}


//// ragdoll::HashKey

HashKey::HashKey(const KeyAdapter &key)
{
	_key.SetFromRangeString(key.Key(), key.Length());
	_hash = key.Hash();
	_len = key.Length();
}


//// ragdoll::HashValue

HashValue::HashValue(const char *value, int len)
{
	_value.SetFromRangeString(value, len);
}


//// ragdoll::HashItem

HashItem::HashItem(const KeyAdapter &key)
	: HashKey(key)
{
}


//// ragdoll::HashTable

bool HashTable::Grow()
{
	// Calculate growth rate
	u32 old_size = _allocated;
	u32 new_size = old_size * GROW_RATE;
	if (new_size < PREALLOC) new_size = PREALLOC;

	CAT_INANE("HashTable") << "Growing to " << new_size << " buckets";

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

HashTable::HashTable()
{
	_buckets = 0;
	_allocated = 0;
	_used = 0;
}

HashTable::~HashTable()
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

HashItem *HashTable::Lookup(const KeyAdapter &key)
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

HashItem *HashTable::Create(const KeyAdapter &key)
{
	// If first allocation fails,
	if (!_buckets && !Grow()) return 0;

	// If cannot create an item,
	HashItem *item = new HashItem(key);
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


//// ragdoll::HashTable::Iterator

void HashTable::Iterator::IterateNext()
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

HashTable::Iterator::Iterator(HashTable &head)
{
	_remaining = head._allocated;
	_bucket = head._buckets;
	_ii = *_bucket;

	if (!_ii)
	{
		IterateNext();
	}
}


//// ragdoll::Parser

char *Parser::FindEOL(char *data, char *eof)
{
	// While data pointer is within file,
	while (data < eof)
	{
		// Grab current character and point to next character
		char ch = *data;

		if (ch == '\n')
		{
			if (++data >= eof) break;
			if (*data == '\r') ++data;
			break;
		}
		else if (ch == '\r')
		{
			if (++data >= eof) break;
			if (*data == '\n') ++data;
			break;
		}

		++data;
	}

	return data;
}

char *Parser::FindSecondTokenEnd(char *data, char *eof)
{
	char *second = data;
	char *eol = second;
	int len = 0;
	while (++eol < eof)
	{
		char ch = *eol;

		if (ch == '\r')
		{
			--len;
			if (++eol >= eof) break;
			if (*eol == '\n')
			{
				++eol;
				--len;
			}
			break;
		}
		else if (ch == '\n')
		{
			--len;
			if (++eol >= eof) break;
			if (*eol == '\r')
			{
				++eol;
				--len;
			}
			break;
		}
	}

	_second_len = len + (int)(eol - second);
	_eol = _second + _second_len;
	return second;
}

bool Parser::FindSecondToken(char *&data, char *eof)
{
	// Find the start of whitespace after first token
	char *first = data;
	char *second = first;
	int len = 0;
	while (++second < eof)
	{
		char ch = *second;

		if (ch == '\r')
		{
			_eol = second;
			--len;
			if (++second >= eof) break;
			if (*second == '\n')
			{
				++second;
				--len;
			}
			break;
		}
		else if (ch == '\n')
		{
			_eol = second;
			--len;
			if (++second >= eof) break;
			if (*second == '\r')
			{
				++second;
				--len;
			}
			break;
		}
		else if (ch == ' ' || ch == '\t')
		{
			_first_len = (int)(second - first);

			// Now hunt for the beginning of the second token
			while (++second < eof)
			{
				char ch = *second;

				if (ch == '\r')
				{
					_eol = second;
					if (++second >= eof) break;
					if (*second == '\n') ++second;
					break;
				}
				else if (ch == '\n')
				{
					_eol = second;
					if (++second >= eof) break;
					if (*second == '\r') ++second;
					break;
				}
				else if (ch != ' ' && ch != '\t')
				{
					data = second;
					_second = second;
					return true;
				}
			}

			data = second;
			return false;
		}
	}

	_first_len = len + (int)(second - first);
	data = second;
	return false;
}

bool Parser::FindFirstToken(char *&data, char *eof)
{
	int tab_count = 0, space_count = 0;
	char *first = data;

	// While EOF not encountered,
	while (first < eof)
	{
		char ch = *first;

		if (ch == '\n')
		{
			if (++first >= eof) break;
			if (*first == '\r') ++first;
			data = first;
			return true;
		}
		else if (ch == '\r')
		{
			if (++first >= eof) break;
			if (*first == '\n') ++first;
			data = first;
			return true;
		}
		else if (ch == ' ')
			++space_count;
		else if (ch == '\t')
			++tab_count;
		else if (!IsAlpha(ch))
		{
			data = FindEOL(first + 1, eof);
			return true;
		}
		else
		{
			_first = first;

			/*
				Calculate depth from tab and space count

				Round up front 2 spaces to an extra tab in case just
				the last tab is replaced by spaces and the tab stops
				are set at 2 characters (attempting to be clever about it)
			*/
			int depth = tab_count + (space_count + 2) / 4;
			if (depth > MAX_TAB_RECURSION_DEPTH) depth = MAX_TAB_RECURSION_DEPTH;
			_depth = depth;

			// Find second token starting from first token
			if (FindSecondToken(first, eof))
				first = FindSecondTokenEnd(first, eof);

			data = first;
			return true;
		}

		++first;
	}

	data = first;
	return false;
}

bool Parser::NextLine()
{
	// Initialize parser results
	_first_len = 0;
	_second_len = 0;
	_eol = 0;

	return FindFirstToken(_file_data, _eof);
}

int Parser::ReadTokens(int root_key_len, int root_depth)
{
	int eof = 0;

	do
	{
		// If there is not enough space to append the first token to the end of the root key,
		if (root_key_len + 1 + _first_len > MAX_CHARS)
		{
			// Signal EOF here to avoid mis-attributing keys
			CAT_WARN("Settings") << "Long line caused settings processing to abort early";
			return 0;
		}

		// Append first token to root key
		int key_len = root_key_len + _first_len;
		char *write_key = _root_key + root_key_len;

		if (root_key_len > 0)
		{
			_root_key[root_key_len] = '.';
			++write_key;
			++key_len;
		}

		memcpy(write_key, _first, _first_len);
		write_key[_first_len] = '\0';

		// Add this path to the hash table
		SanitizedKey san_key(_root_key, key_len);
		KeyAdapter key_input(san_key);
		HashItem *item = _output_file->_table.Lookup(key_input);
		if (!item)
		{
			// Create a new item for this key
			item = _output_file->_table.Create(key_input);

			if (!_is_override)
			{
				item->_enlisted = false;
			}
			else
			{
				// Push onto the new list
				CAT_FSLL_PUSH_FRONT(_output_file->_modded, item, _mod_next);
				item->_enlisted = true;
			}
		}
		else
		{
			if (!item->_enlisted)
			{
				// Push onto the new list
				CAT_FSLL_PUSH_FRONT(_output_file->_newest, item, _mod_next);
			}
		}

		// Update item value
		if (item)
		{
			if (!_is_override)
			{
				// Calculate key end offset and end of line offset
				u32 key_end_offset = (u32)(_first + _first_len - _file_front);
				u32 eol_offset;

				if (!_eol)
					eol_offset = (u32)(_eof - _file_front);
				else
					eol_offset = (u32)(_eol - _file_front);

				item->_key_end_offset = key_end_offset;
				item->_eol_offset = eol_offset;
			}
			else
			{
				// Push onto the new list
				CAT_FSLL_PUSH_FRONT(_output_file->_newest, item, _mod_next);
				item->_enlisted = true;
			}

			item->_depth = _depth;

			// If second token is set,
			if (_second_len > 0)
				item->SetValueRangeStr(_second, _second_len);
			else
				item->ClearValue();
		}

		// For each line until EOF,
		int depth = _depth;
		while (NextLine())
		{
			// Skip blank lines
			if (_first_len == 0) continue;

			// If new line depth is at or beneath the root,
			if (root_depth >= _depth)
				eof = 1; // Pass it back to the root to handle
			// If new line is a child of current depth,
			else if (depth < _depth)
			{
				// Otherwise the new line depth is deeper, so recurse and add onto the key
				eof = ReadTokens(key_len, depth);

				// If not EOF,
				if (eof != 0)
				{
					// If new line depth is at the same level as current token,
					if (root_depth < _depth)
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
		_root_key[root_key_len] = '\0';
	} while (eof == 2);

	return eof;
}

bool Parser::Read(const char *file_path, File *output_file, bool is_override)
{
	CAT_DEBUG_ENFORCE(file_path && output_file);

	_output_file = output_file;
	_is_override = is_override;

	MappedFile local_file, *file;
	MappedView local_view, *view;

	// If using local mapped file,
	if (is_override)
	{
		file = &local_file;
		view = &local_view;
	}
	else
	{
		file = &_output_file->_file;
		view = &_output_file->_view;
	}

	// Open the file
	if (!file->Open(file_path))
	{
		CAT_INFO("Parser") << "Unable to open " << file_path;
		return false;
	}

	// Ensure file is not too large
	u64 file_length = file->GetLength();
	if (file_length > MAX_FILE_SIZE)
	{
		CAT_WARN("Parser") << "Size too large for " << file_path;
		return false;
	}

	// Ensure file is not empty
	u32 nominal_length = (u32)file_length;
	if (nominal_length <= 0)
	{
		CAT_INFO("Parser") << "Ignoring empty file " << file_path;
		return false;
	}

	// Open a view of the file
	if (!view->Open(file))
	{
		CAT_WARN("Parser") << "Unable to open view of " << file_path;
		return false;
	}

	// Map a view of the entire file
	if (!view->MapView(0, nominal_length))
	{
		CAT_WARN("Parser") << "Unable to map view of " << file_path;
		return false;
	}

	// Initialize parser
	_file_front = _file_data = (char*)view->GetFront();
	_eof = _file_data + nominal_length;
	_root_key[0] = '\0';

	// Kick off the parsing
	if (!NextLine())
		return false;

	// Bump tokens back to the next level while not EOF
	while (1 == ReadTokens(0, 0));

	return true;
}


//// ragdoll::RagdollFile

File::File()
{
	_modded = 0;
	_newest = 0;
}

File::~File()
{
}

bool File::Read(const char *file_path)
{
	CAT_DEBUG_ENFORCE(file_path);

	return Parser().Read(file_path, this);
}

bool File::Override(const char *file_path)
{
	CAT_DEBUG_ENFORCE(file_path);

	return Parser().Read(file_path, this, true);
}

void File::Set(const char *key, const char *value)
{
	CAT_DEBUG_ENFORCE(key && value);

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (!item)
	{
		if (value[0] == '\0') return;

		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->_enlisted = true;

			item->SetValueStr(value);
		}
	}
	else 
	{
		// If item is not listed yet,
		if (!item->_enlisted)
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
			item->_enlisted = true;
		}

		item->SetValueStr(value);
	}
}

const char *File::Get(const char *key, const char *defaultValue)
{
	CAT_DEBUG_ENFORCE(key && defaultValue);

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (item) return item->GetValueStr();

	// If default value is not undefined,
	if (defaultValue[0] != '\0')
	{
		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->_enlisted = true;

			item->SetValueStr(defaultValue);
		}
	}

	return defaultValue;
}

void File::SetInt(const char *key, int value)
{
	CAT_DEBUG_ENFORCE(key);

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (!item)
	{
		if (value == 0) return;

		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->_enlisted = true;

			item->SetValueInt(value);
		}
	}
	else
	{
		// If item is not listed yet,
		if (!item->_enlisted)
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
			item->_enlisted = true;
		}

		item->SetValueInt(value);
	}
}

int File::GetInt(const char *key, int defaultValue)
{
	CAT_DEBUG_ENFORCE(key);

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (item) return item->GetValueInt();

	// If default value is not undefined,
	if (defaultValue != 0)
	{
		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->_enlisted = true;

			item->SetValueInt(defaultValue);
		}
	}

	return defaultValue;
}

void File::Set(const char *key, const char *value, RWLock *lock)
{
	CAT_DEBUG_ENFORCE(key && lock && value);

	lock->WriteLock();

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (!item)
	{
		if (value[0] != '\0')
		{
			// Create a new item for this key
			item = _table.Create(key_input);
			if (item)
			{
				// Push onto the new list
				CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
				item->_enlisted = true;

				item->SetValueStr(value);
			}
		}
	}
	else
	{
		// If item is not listed yet,
		if (!item->_enlisted)
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
			item->_enlisted = true;
		}

		item->SetValueStr(value);
	}

	lock->WriteUnlock();
}

void File::Get(const char *key, const char *defaultValue, std::string &out_value, RWLock *lock)
{
	CAT_DEBUG_ENFORCE(key && lock && defaultValue);

	lock->ReadLock();

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (item)
	{
		out_value = item->GetValueStr();
		lock->ReadUnlock();
		return;
	}

	lock->ReadUnlock();

	// If default value is not undefined,
	if (defaultValue[0] != '\0')
	{
		lock->WriteLock();

		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->_enlisted = true;

			item->SetValueStr(defaultValue);
		}

		lock->WriteUnlock();
	}

	out_value = defaultValue;
}

void File::SetInt(const char *key, int value, RWLock *lock)
{
	CAT_DEBUG_ENFORCE(key && lock);

	lock->WriteLock();

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (!item)
	{
		if (value != 0)
		{
			// Create a new item for this key
			item = _table.Create(key_input);
			if (item)
			{
				// Push onto the new list
				CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
				item->_enlisted = true;

				item->SetValueInt(value);
			}
		}
	}
	else
	{
		// If item is not listed yet,
		if (!item->_enlisted)
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
			item->_enlisted = true;
		}

		item->SetValueInt(value);
	}

	lock->WriteUnlock();
}

int File::GetInt(const char *key, int defaultValue, RWLock *lock)
{
	CAT_DEBUG_ENFORCE(key && lock);

	lock->ReadLock();

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyAdapter key_input(san_key);
	HashItem *item = _table.Lookup(key_input);
	if (item)
	{
		int value = item->GetValueInt();
		lock->ReadUnlock();
		return value;
	}

	lock->ReadUnlock();

	// If default value is not undefined,
	if (defaultValue != 0)
	{
		lock->WriteLock();

		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->_enlisted = true;

			item->SetValueInt(defaultValue);
		}

		lock->WriteUnlock();
	}

	return defaultValue;
}

/*
	MergeSort for a singly-linked list

	Preserves existing order for items that have the same position
*/
HashItem *File::SortItems(HashItem *head)
{
	if (!head) return 0;

	// TODO: Need to assign _skip_next in retrospect

	// Unroll first loop where consecutive pairs are put in order
	HashItem *a = head, *tail = 0;
	do
	{
		// Grab second item in pair
		HashItem *b = a->_mod_next;

		// If no second item in pair,
		if (!b)
		{
			// Initialize the skip pointer to null
			a->_skip_next = 0;

			// Done with this step size!
			break;
		}

		// Remember next pair in case swap occurs
		HashItem *next_pair = b->_mod_next;

		// If current pair are already in order,
		if (a->_key_end_offset <= b->_key_end_offset)
		{
			// Remember b as previous node
			tail = b;

			// Maintain skip list for next pass
			a->_skip_next = next_pair;
		}
		else // pair is out of order
		{
			// Fix a, b next pointers
			a->_mod_next = next_pair;
			b->_mod_next = a;

			// Link b to previous node
			if (tail) tail->_mod_next = b;
			else head = b;

			// Remember a as previous node
			tail = a;

			// Maintain skip list for next pass
			b->_skip_next = next_pair;
		}

		// Continue at next pair
		a = next_pair;
	} while (a);

	// Continue from step size of 2
	int step_size = 2;
	CAT_FOREVER
	{
		// Unroll first list merge for exit condition
		HashItem *a = head, *tail = 0;

		// Grab start of second list
		HashItem *b = a->_skip_next;

		// If no second list, sorting is done
		if (!b) break;

		// Remember pointer to next list
		HashItem *next_list = b->_skip_next;

		// First item in the new list will be either a or b
		// b already has next list pointer set, so just update a
		a->_skip_next = next_list;

		// Cache a, b offsets
		u32 aoff = a->_key_end_offset, boff = b->_key_end_offset;

		// Merge two lists together until step size is exceeded
		int b_remaining = step_size;
		HashItem *b_head = b;
		CAT_FOREVER
		{
			// In cases where both are equal, preserve order
			if (aoff <= boff)
			{
				// Set a as tail
				if (tail) tail->_mod_next = a;
				else head = a;
				tail = a;

				// Grab next a
				a = a->_mod_next;

				// If ran out of a-items,
				if (a == b_head)
				{
					// Link remainder of b-items to the end
					tail->_mod_next = b;

					// Done with this step size
					break;
				}

				// Update cache of a-offset
				aoff = a->_key_end_offset;
			}
			else
			{
				// Set b as tail
				if (tail) tail->_mod_next = b;
				else head = b;
				tail = b;

				// Grab next b
				b = b->_mod_next;

				// If ran out of b-items,
				if (--b_remaining == 0 || !b)
				{
					// Link remainder of a-items to end
					tail->_mod_next = a;

					// Done with this step size
					break;
				}

				// Update cache of b-offset
				boff = b->_key_end_offset;
			}
		}

		// Second and following merges
		while ((a = next_list))
		{
			// Grab start of second list
			b = a->_skip_next;

			// If no second list, done with this step size
			if (!b) break;

			// Remember pointer to next list
			next_list = b->_skip_next;

			// First item in the new list will be either a or b
			// b already has next list pointer set, so just update a
			a->_skip_next = next_list;

			// Cache a, b offsets
			aoff = a->_key_end_offset;
			boff = b->_key_end_offset;

			// Merge two lists together until step size is exceeded
			b_remaining = step_size;
			b_head = b;
			CAT_FOREVER
			{
				// In cases where both are equal, preserve order
				if (aoff <= boff)
				{
					// Set a as tail
					tail->_mod_next = a;
					tail = a;

					// Grab next a
					a = a->_mod_next;

					// If ran out of a-items,
					if (a == b_head)
					{
						// Link remainder of b-items to the end
						tail->_mod_next = b;

						// Done with this step size
						break;
					}

					// Update cache of a-offset
					aoff = a->_key_end_offset;
				}
				else
				{
					// Set b as tail
					tail->_mod_next = b;
					tail = b;

					// Grab next b
					b = b->_mod_next;

					// If ran out of b-items,
					if (--b_remaining == 0 || !b)
					{
						// Link remainder of a-items to end
						tail->_mod_next = a;

						// Done with this step size
						break;
					}

					// Update cache of b-offset
					boff = b->_key_end_offset;
				}
			}
		}

		// Double step size
		step_size *= 2;
	}

	return head;
}

/*
	Merge two sorted linked lists:
	Higher priority list wins when both are at the same offset.
*/
HashItem *File::MergeItems(HashItem *hi_prio, HashItem *lo_prio)
{
	// If nothing in hi prio list,
	HashItem *m = hi_prio;
	if (!m) return lo_prio; 

	// If nothing in lo prio list,
	HashItem *n = lo_prio;
	if (!n) return m;

	// Initialize new head and tail
	u32 noff = n->_key_end_offset;
	u32 moff = m->_key_end_offset;
	HashItem *head;
	if (noff < moff)
	{
		// Set n as head
		head = n;

		// Get next n
		n = n->_mod_next;
		if (!n)
		{
			// Append remainder of m list
			head->_mod_next = m;
			return head;
		}

		// Update n offset
		noff = n->_key_end_offset;
	}
	else
	{
		// Set m as head
		head = m;

		// Get next m
		m = m->_mod_next;
		if (!m)
		{
			// Append remainder of n list
			head->_mod_next = n;
			return head;
		}

		// Update m offset
		moff = m->_key_end_offset;
	}

	HashItem *tail = head;
	CAT_FOREVER
	{
		// If n should be next,
		if (noff < moff)
		{
			// Append n
			tail->_mod_next = n;
			tail = n;

			// Get next n
			n = n->_mod_next;
			if (!n)
			{
				// Append remainder of m list
				tail->_mod_next = m;
				break;
			}

			// Update n offset
			noff = n->_key_end_offset;
		}
		else // if m == n or m > n,
		{
			// Append m
			tail->_mod_next = m;
			tail = m;

			// Get next m
			m = m->_mod_next;
			if (!m)
			{
				// Append remainder of n list
				tail->_mod_next = n;
				break;
			}

			// Update m offset
			moff = m->_key_end_offset;
		}
	}

	return head;
}

u32 File::WriteNewKey(char *key, int key_len, HashItem *front, HashItem *end)
{
	// Strip off dotted parts until we find it in the hash table
	for (int jj = key_len - 1; jj > 1; --jj)
	{
		// Search for next dot
		if (key[jj] != '.') continue;

		// Create a key from the parent part of the string
		u32 hash = MurmurHash(key, jj).Get32();
		KeyAdapter key_input(key, jj, hash);

		// Look up the parent item
		HashItem *parent = _table.Lookup(key_input);
		if (parent)
		{
			// If parent is already enlisted,
			if (parent->_enlisted)
			{
				// Insert after parent
				end->_mod_next = parent->_mod_next;
				parent->_mod_next = front;
			}
			else
			{
				// NOTE: New items at end are all enlisted so will not get here with a new-item parent

				// Insert at front of the modified list
				end->_mod_next = _modded;
				_modded = front;
			}

			// Remember key depth
			_key_depth = parent->_depth;

			// Return end-of-line offset of parent
			return parent->_eol_offset ? parent->_eol_offset : parent->_key_end_offset;
		}
		else
		{
			// Create a hash table entry for this key
			HashItem *item = _table.Create(key_input);
			if (!item)
			{
				CAT_FATAL("Ragdoll") << "Out of memory";
				return 0;
			}

			// NOTE: Will not find this item as a parent during recursion
			// so leaving the item uninitialized for now is okay.

			// Recurse to find parent
			u32 offset = WriteNewKey(key, jj, item, end);

			// Go ahead and fill in the item
			item->_enlisted = true;
			item->_key_end_offset = offset;
			item->_eol_offset = 0; // Indicate it is a new item that needs new item processing
			item->_mod_next = front;
			item->_depth = ++_key_depth;
			item->ClearValue();
			return offset;
		}
	}

	// Did not find the parent at all, so this is a completely new item

	// Insert at the front of the eof list
	end->_mod_next = _eof_head;
	_eof_head = front;

	// Remember key depth
	_key_depth = -1;

	return 0;
}

static void WriteFinalKeyPart(HashItem *item, ofstream &file)
{
	// Cache key
	const char *key = item->Key();
	int len = item->Length();

	// Search for final part of key
	int ii = len - 1;
	while (ii >= 0 && IsAlphanumeric(key[ii]))
		--ii;

	// Write it
	int write_count = len - ii - 1;
	if (write_count > 0) file.write(key + ii + 1, write_count);
}

static void WriteItemValue(HashItem *item, ofstream &file)
{
	const char *value = item->GetValueStr();

	// If value is not set, abort
	if (!value[0]) return;

	// If there was no original value,
	if (item->GetEOLOffset() <= item->GetKeyEndOffset())
	{
		// Write a tab after the key
		file.write(TAB_STRING, 1);
	}

	// Write the new value string
	file.write(value, (int)strlen(value));
}

static void WriteItem(HashItem *item, ofstream &file)
{
	// Write a new line
	file.write("\n", 1);

	// Write tabs up to the depth
	int depth = item->GetDepth();
	if (depth > 0) file.write(TAB_STRING, depth);

	WriteFinalKeyPart(item, file);

	WriteItemValue(item, file);
}

bool File::Write(const char *file_path, bool force)
{
	CAT_DEBUG_ENFORCE(file_path);

	if (!force && (!_newest && !_modded)) return true;

	// Cache view
	const char *front = (const char*)_view.GetFront();
	u32 file_length = _view.GetLength();

	CAT_DEBUG_ENFORCE(front || !_modded) << "Modded items but no open file";

	// Construct temporary file path
	string temp_path = file_path;
	temp_path += ".tmp";

	// Attempt to open the temporary file for write
	ofstream file(temp_path);
	if (!file)
	{
		CAT_WARN("Ragdoll") << "Unable to open output file " << file_path;
		return false;
	}

	// Initialize eof list head
	_eof_head = 0;

	// For each new item in the list,
	for (HashItem *next, *ii = _newest; ii; ii = next)
	{
		// Cache next in list
		next = ii->_mod_next;

		// Sanitize the key string
		char sanitized_key[MAX_CHARS+1];
		int sanitized_len = SanitizeKeyString(ii->Key(), sanitized_key);

		// Write new key list into the mod or eof list
		_key_depth = 0;
		u32 offset = WriteNewKey(sanitized_key, sanitized_len, ii, ii);

		// Go ahead and fill in the item
		ii->_enlisted = true;
		ii->_key_end_offset = offset;
		ii->_eol_offset = 0; // Indicate it is a new item that needs new item processing
		// ii->_mod_next already set
		ii->_depth = ++_key_depth;
	}

	// Sort the modified items in increasing order and merge the merge-items
	u32 copy_start = 0;
	for (HashItem *ii = SortItems(_modded); ii; ii = ii->_mod_next)
	{
		u32 key_end_offset = ii->_key_end_offset;
		u32 copy_bytes = key_end_offset - copy_start;

		// Write original file data up to the start of the key
		if (copy_bytes > 0)
			file.write(front + copy_start, copy_bytes);

		// If modifying a value of an existing key,
		u32 eol_offset = ii->_eol_offset;
		if (eol_offset)
		{
			// NOTE: EOL offset points at the next character after the original value

			WriteItemValue(ii, file);

			// NOTE: No need to write a new line here

			copy_start = eol_offset;
		}
		else
		{
			WriteItem(ii, file);

			copy_start = key_end_offset;
		}
	}

	// Copy remainder of file
	if (copy_start < file_length)
	{
		u32 copy_bytes = file_length - copy_start;

		if (copy_bytes > 0)
			file.write(front + copy_start, copy_bytes);
	}

	// For each EOF item,
	for (HashItem *ii = _eof_head; ii; ii = ii->_mod_next)
		WriteItem(ii, file);

	// Flush and close the file
	file.flush();
	file.close();

	// Close view of actual file
	_view.Close();
	_file.Close();

	// Delete file
	std::remove(file_path);

	// Move it to the final path
	std::rename(temp_path.c_str(), file_path);

	return true;
}
