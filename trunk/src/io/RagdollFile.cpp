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
			*outs++ = ch + 'a' - 'Z';
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
			*outs++ = ch + 'a' - 'Z';
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


//// ragdoll::KeyInput

KeyInput::KeyInput(SanitizedKey &key)
{
	_key = key.Key();
	_len = key.Length();
	_hash = key.Hash();
}


//// ragdoll::HashKey

HashKey::HashKey(const KeyInput &key)
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

HashItem::HashItem(const KeyInput &key)
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

HashItem *HashTable::Lookup(const KeyInput &key)
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

HashItem *HashTable::Create(const KeyInput &key)
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
		KeyInput key_input(san_key);
		HashItem *item = _output_file->_table.Lookup(key_input);
		if (!item)
		{
			// Create a new item for this key
			item = _output_file->_table.Create(key_input);
			item->_enlisted = false;
		}

		// Update item value
		if (item)
		{
			if (_is_override)
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
	_file_data = 0;
	_file_size = 0;

	_modded = 0;
	_newest = 0;
}

File::~File()
{
	if (_file_data)
		delete []_file_data;
}

bool File::Read(const char *file_path)
{
	Parser parser;
	return parser.Read(file_path, this);
}

bool File::Override(const char *file_path)
{
	Parser parser;
	if (!parser.Read(file_path, this, true))
		return false;

	return true;
}

void File::Set(const char *key, const char *value)
{
	CAT_DEBUG_ENFORCE(key && value);

	// Add this path to the hash table
	SanitizedKey san_key(key);
	KeyInput key_input(san_key);
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
	KeyInput key_input(san_key);
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
	KeyInput key_input(san_key);
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
	KeyInput key_input(san_key);
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
	KeyInput key_input(san_key);
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
	KeyInput key_input(san_key);
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
	KeyInput key_input(san_key);
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
	KeyInput key_input(san_key);
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
	MergeSort for the singly-linked list of modified items

	Sorts by location in the file
*/
void File::SortModifiedItems()
{
	HashItem *head = _modded;
	int frame_size = 1;

	CAT_FOREVER
	{
		HashItem *a = head;
		HashItem *tail = 0;

		while (a)
		{
			HashItem *b = a->_skip_next;

			if (!b)
			{
				_modded = head;
				return;
			}

			u32 aoff = a->_key_end_offset;
			u32 boff = b->_key_end_offset;

			int ii;
			for (ii = 0; ii < frame_size; ++ii)
			{
				// In cases where both are equal, preserve order
				if (aoff <= boff)
				{
					if (tail) tail->_mod_next = a;
					else head = a;
					tail = a;

					a = a->_mod_next;
					if (!a)
					{
						tail->_mod_next = b;
						break;
					}
					aoff = a->_key_end_offset;
				}
				else
				{
					if (tail) tail->_mod_next = b;
					else head = b;
					tail = b;

					b = b->_mod_next;
					if (!b)
					{
						tail->_mod_next = a;
						break;
					}
					aoff = b->_key_end_offset;
				}
			}

			// Find end of frame
			for (; ii < frame_size; ++ii)
			{
				if (!tail) break;
				tail = tail->_mod_next;
			}

			head->_skip_next = a = tail;
		}

		frame_size *= 2;
	}
}

/*
	Merge two sorted linked lists:
	Newest items are merged into the modified items list.

	If both items have the same key-end-offset, then it will
	use the modified list to match the proper write order.
*/
HashItem *File::MergeNewestItems()
{
	// If nothing is new,
	HashItem *n = _newest;
	if (!n) return; 

	// If nothing is old,
	HashItem *m = _modded;
	if (!m)
	{
		_modded = _newest;
		return;
	}

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
		else
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

bool File::WriteNewKey(char *key, int key_len, HashItem *item)
{
	// Strip off dotted parts until we find it in the hash table
	for (int jj = key_len - 1; jj > 1; --jj)
	{
		if (key[jj] == '.')
		{
			key[jj] = '\0';

			// Create a key from the string
			u32 hash = MurmurHash(key, jj).Get32();
			KeyInput key_input(key, jj, hash);

			// Lookup the parent item
			HashItem *item = _table.Lookup(key_input);
			if (item)
			{
				// TODO

				return true;
			}
			else
			{
				if (WriteNewKey(key, jj, item))
				{
					// TODO

					return true;
				}
				else
				{
					// TODO

					return false;
				}
			}

			key[jj] = '.';
		}
	}

	return false;
}

bool File::Write(const char *file_path, bool force)
{
	CAT_DEBUG_ENFORCE(file_path);

	if (!force && (!_newest && !_modded)) return true;

	// Cache view
	u8 *front = _view.GetFront();
	u32 eof = _view.GetLength();

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

	// Sort the modified items in increasing order
	SortModifiedItems();

	// For each new item in the list,
	for (HashItem *ii = _newest; ii; ii = ii->_mod_next)
	{
		const char *overall_key = ii->Key();
		int overall_len = ii->Length();

		// Copy as lower-case
		char lowercase_key[MAX_CHARS+1];
		CopyToLowercaseString(overall_key, lowercase_key);

		// If not able to insert under an existing key,
		if (!WriteNewKey(lowercase_key, overall_len, ii))
		{
			// TODO: Write it to the end
		}
	}

	// For each modified item,
	u32 copy_start = 0;
	for (HashItem *write_node = MergeNewestItems(); write_node; write_node = write_node->_mod_next)
	{
		u32 key_end_offset = write_node->_key_end_offset;
		u32 eol_offset = write_node->_eol_offset;
		u32 copy_bytes = key_end_offset - copy_start;

		if (copy_bytes > 0)
			file.write(_file_data + copy_start, copy_bytes);

		copy_start = eol_offset;

		// TODO: Insert new data here
	}

	// Copy remainder of file
	if (copy_start < _file_size)
	{
		u32 copy_bytes = _file_size - copy_start;

		if (copy_bytes > 0)
			file.write(_file_data + copy_start, copy_bytes);
	}

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
