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
#include <cat/io/MappedFile.hpp>
#include <cat/hash/Murmur.hpp>
#include <fstream>
#include <cstring>
#include <cstdlib>
using namespace cat;
using namespace std;
using namespace ragdoll;


// Calculate key hash
static u32 GetKeyHash(const char *key, int len)
{
	char lcase[MAX_CHARS+1];

	if (len > MAX_CHARS)
		len = MAX_CHARS;

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


//// ragdoll::KeyInput

KeyInput::KeyInput(const char *key)
{
	int len = (int)strlen(key);

	_hash = GetKeyHash(key, len);
	_key = key;
	_len = len;
}

KeyInput::KeyInput(const char *key, int len)
{
	_hash = GetKeyHash(key, len);
	_key = key;
	_len = len;
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
		KeyInput key_input(_root_key, key_len);
		HashItem *item = _output_file->_table.Lookup(key_input);
		if (!item)
		{
			// Create a new item for this key
			item = _output_file->_table.Create(key_input);
		}

		// Update item value
		if (item)
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

bool Parser::Read(const char *file_path, File *output_file, char **file_data, u32 *file_size)
{
	CAT_DEBUG_ENFORCE(file_path && output_file);

	_output_file = output_file;

	// Open the file
	MappedFile file;
	if (!file.Open(file_path))
	{
		CAT_INFO("Parser") << "Unable to open " << file_path;
		return false;
	}

	// Ensure file is not too large
	u64 file_length = file.GetLength();
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
	MappedView view;
	if (!view.Open(&file))
	{
		CAT_WARN("Parser") << "Unable to open view of " << file_path;
		return false;
	}

	// Map a view of the entire file
	if (!view.MapView(0, nominal_length))
	{
		CAT_WARN("Parser") << "Unable to map view of " << file_path;
		return false;
	}

	// Initialize parser
	_file_front = _file_data = (char*)view.GetFront();
	_eof = _file_data + nominal_length;
	_root_key[0] = '\0';
	_store_offsets = false;

	// If file data will be copied out,
	if (file_data && file_size)
	{
		char *copy = new char[nominal_length];
		if (!copy)
		{
			CAT_WARN("Parser") << "Out of memory allocating file buffer of bytes = " << nominal_length;
			return false;
		}

		memcpy(copy, _file_data, nominal_length);

		*file_data = copy;
		*file_size = nominal_length;
		_store_offsets = true;
	}

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
	return parser.Read(file_path, this, &_file_data, &_file_size);
}

bool File::Override(const char *file_path)
{
	Parser parser;
	if (!parser.Read(file_path, this))
		return false;

	return true;
}

void File::Set(const char *key, const char *value)
{
	CAT_DEBUG_ENFORCE(key && value);

	// Add this path to the hash table
	KeyInput key_input(key);
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
			item->MarkNew();
		}
	}
	else
	{
		if (!item->IsNew())
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
		}
	}

	// Update item value
	if (item) item->SetValueStr(value);
}

const char *File::Get(const char *key, const char *defaultValue)
{
	CAT_DEBUG_ENFORCE(key && defaultValue);

	// Add this path to the hash table
	KeyInput key_input(key);
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
			item->MarkNew();

			item->SetValueStr(defaultValue);
		}
	}

	return defaultValue;
}

void File::SetInt(const char *key, int value)
{
	CAT_DEBUG_ENFORCE(key);

	// Add this path to the hash table
	KeyInput key_input(key);
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
			item->MarkNew();
		}
	}
	else
	{
		if (!item->IsNew())
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
		}
	}

	// Update item value
	if (item) item->SetValueInt(value);
}

int File::GetInt(const char *key, int defaultValue)
{
	CAT_DEBUG_ENFORCE(key);

	// Add this path to the hash table
	KeyInput key_input(key);
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
			item->MarkNew();

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
	KeyInput key_input(key);
	HashItem *item = _table.Lookup(key_input);
	if (!item)
	{
		if (value[0] == '\0')
		{
			lock->WriteUnlock();
			return;
		}

		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->MarkNew();
		}
	}
	else
	{
		if (!item->IsNew())
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
		}
	}

	// Update item value
	if (item) item->SetValueStr(value);

	lock->WriteUnlock();
}

void File::Get(const char *key, const char *defaultValue, std::string &out_value, RWLock *lock)
{
	CAT_DEBUG_ENFORCE(key && lock && defaultValue);

	lock->ReadLock();

	// Add this path to the hash table
	KeyInput key_input(key);
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
			item->MarkNew();

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
	KeyInput key_input(key);
	HashItem *item = _table.Lookup(key_input);
	if (!item)
	{
		if (value == 0)
		{
			lock->WriteUnlock();
			return;
		}

		// Create a new item for this key
		item = _table.Create(key_input);
		if (item)
		{
			// Push onto the new list
			CAT_FSLL_PUSH_FRONT(_newest, item, _mod_next);
			item->MarkNew();
		}
	}
	else
	{
		if (!item->IsNew())
		{
			CAT_FSLL_PUSH_FRONT(_modded, item, _mod_next);
		}
	}

	// Update item value
	if (item) item->SetValueInt(value);

	lock->WriteUnlock();
}

int File::GetInt(const char *key, int defaultValue, RWLock *lock)
{
	CAT_DEBUG_ENFORCE(key && lock);

	lock->ReadLock();

	// Add this path to the hash table
	KeyInput key_input(key);
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
			item->MarkNew();

			item->SetValueInt(defaultValue);
		}

		lock->WriteUnlock();
	}

	return defaultValue;
}

/*
	MergeSort for a linked list of modified items

	Sorts by location in the file
*/
static HashItem *SortHashItems(HashItem *head)
{
    int insize = 1;

    CAT_FOREVER
	{
        HashItem *p = head, *tail = 0;
        head = 0;

        int nmerges = 0;  /* count number of merges we do in this pass */

        while (p)
		{
            nmerges++;  /* there exists a merge to be done */

			/* step `insize' places along from p */
            HashItem *q = p;
            int psize = 0;

            for (int i = 0; i < insize; i++)
			{
                psize++;
			    q = q->next;
                if (!q) break;
            }

            /* if q hasn't fallen off end, we have two lists to merge */
            qsize = insize;

            /* now we have two lists; merge them */
            while (psize > 0 || (qsize > 0 && q))
			{
                /* decide whether next element of merge comes from p or q */
                if (psize == 0)
				{
					/* p is empty; e must come from q. */
				    e = q; q = q->next; qsize--;
				}
				else if (qsize == 0 || !q)
				{
					/* q is empty; e must come from p. */
					e = p; p = p->next; psize--;
				}
				else if (cmp(p,q) <= 0)
				{
					/* First element of p is lower (or same);
					 * e must come from p. */
					e = p; p = p->next; psize--;
				}
				else
				{
					/* First element of q is lower; e must come from q. */
					e = q; q = q->next; qsize--;
				}

                /* add the next element to the merged list */
				if (tail)
					tail->next = e;
				else
					list = e;

				/* Maintain reverse pointers in a doubly linked list. */
				e->prev = tail;

				tail = e;
            }

            /* now p has stepped `insize' places along, and q has too */
            p = q;
        }

	    tail->next = NULL;

        /* If we have done only one merge, we're finished. */
        if (nmerges <= 1)   /* allow for nmerges==0, the empty list case */
            return list;

        /* Otherwise repeat, merging lists twice the size */
        insize *= 2;
    }
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

	// For each new item in the list,
	for (iter ii = _new_list; ii; ++ii)
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
	for (iter ii = _modified; ii; ++ii)
	{
		u32 key_end_offset, eol_offset;
		ii->GetFileOffsets(key_end_offset, eol_offset);

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

	// Delete file
	std::remove(file_path);

	// Move it to the final path
	std::rename(temp_path.c_str(), file_path);

	_dirty = false;
	return true;
}
