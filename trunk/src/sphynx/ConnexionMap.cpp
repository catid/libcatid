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

#include <cat/sphynx/ConnexionMap.hpp>
#include <cat/hash/Murmur.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;
using namespace sphynx;

static CAT_INLINE u32 flood_hash_addr(const NetAddr &addr, u32 salt)
{
	u32 key;

	// If address is IPv6,
	if (addr.Is6())
	{
		// Hash first 64 bits of 128-bit address to 32 bits
		// Right now the last 64 is easy to change if you actually have an IPv6 address
		key = MurmurHash(addr.GetIP6(), (addr.CanDemoteTo4() ? NetAddr::IP6_BYTES : NetAddr::IP6_BYTES/2), salt).Get32();
	}
	else // assuming IPv4 and address is not invalid
	{
		key = addr.GetIP4();

		// Thomas Wang's integer hash function
		// http://www.cris.com/~Ttwang/tech/inthash.htm
		key = (key ^ 61) ^ (key >> 16);
		key = key + (key << 3);
		key = key ^ (key >> 4) ^ salt;
		key = key * 0x27d4eb2d;
		key = key ^ (key >> 15);
	}

	return key;
}

static CAT_INLINE u32 map_hash_addr(const NetAddr &addr, u32 ip_salt, u32 port_salt)
{
	u32 key;

	// If address is IPv6,
	if (addr.Is6())
	{
		// Hash full address because this function is not for flood detection
		key = MurmurHash(addr.GetIP6(), NetAddr::IP6_BYTES, ip_salt).Get32();
	}
	else // assuming IPv4 and address is not invalid
	{
		key = addr.GetIP4();

		// Thomas Wang's integer hash function
		// http://www.cris.com/~Ttwang/tech/inthash.htm
		key = (key ^ 61) ^ (key >> 16);
		key = key + (key << 3);
		key = key ^ (key >> 4) ^ ip_salt;
		key = key * 0x27d4eb2d;
		key = key ^ (key >> 15);
	}

	// Map 16-bit port 1:1 to a random-looking number
	key += (u32)addr.GetPort() * (port_salt*4 + 1);

	return key;
}

ConnexionMap::ConnexionMap()
{
	CAT_OBJCLR(_map_table);
	CAT_OBJCLR(_flood_table);
	_is_shutdown = false;
	_count = 0;
}

ConnexionMap::~ConnexionMap()
{
}

void ConnexionMap::Initialize(FortunaOutput *csprng)
{
	_ip_salt = csprng->Generate();
	_port_salt = csprng->Generate();
	_flood_salt = csprng->Generate();
}

bool ConnexionMap::LookupCheckFlood(Connexion * &connexion, const NetAddr &addr)
{
	// Hash IP:port:salt to get the hash table key
	u32 key = map_hash_addr(addr, _ip_salt, _port_salt) & HASH_TABLE_MASK;
	u32 flood_key = flood_hash_addr(addr, _flood_salt) & HASH_TABLE_MASK;

	AutoReadLock lock(_table_lock);

	if (IsShutdown())
	{
		connexion = 0;
		return false;
	}

	CAT_FOREVER
	{
		// Grab the slot
		Slot *slot = &_map_table[key];

		Connexion *conn = slot->conn;

		// If the slot is used and the user address matches,
		if (conn && conn->_client_addr == addr)
		{
			conn->AddRef(CAT_REFOBJECT_FILE_LINE);

			connexion = conn;
			return false;
		}
		else
		{
			// If the slot indicates a collision,
			if (slot->collision)
			{
				// Calculate next collision key
				key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & HASH_TABLE_MASK;

				// Loop around and process the next slot in the collision list
			}
			else
			{
				// Reached end of collision list, so the address was not found in the table
				break;
			}
		}
	}

	connexion = 0;
	return (_flood_table[flood_key] >= CONNECTION_FLOOD_THRESHOLD);
}

Connexion *ConnexionMap::Lookup(u32 key)
{
	if (key >= HASH_TABLE_SIZE) return 0;

	AutoReadLock lock(_table_lock);

	if (IsShutdown())
		return 0;

	Connexion *conn = _map_table[key].conn;

	if (conn)
	{
		conn->AddRef(CAT_REFOBJECT_FILE_LINE);

		return conn;
	}

	return 0;
}

bool ConnexionMap::Insert(Connexion *conn)
{
	// Hash IP:port:salt to get the hash table key
	u32 key = map_hash_addr(conn->_client_addr, _ip_salt, _port_salt) & HASH_TABLE_MASK;
	u32 flood_key = flood_hash_addr(conn->_client_addr, _flood_salt) & HASH_TABLE_MASK;

	// Grab the slot
	Slot *slot = &_map_table[key];

	// Add a reference to the Connexion
	conn->AddRef(CAT_REFOBJECT_FILE_LINE);

	AutoWriteLock lock(_table_lock);

	if (IsShutdown())
	{
		lock.Release();
		conn->ReleaseRef(CAT_REFOBJECT_FILE_LINE);
		return false;
	}

	// While collision keys are marked used,
	while (slot->conn)
	{
		// If client is already connected,
		if (slot->conn->_client_addr == conn->_client_addr)
		{
			lock.Release();
			conn->ReleaseRef(CAT_REFOBJECT_FILE_LINE);
			return false;
		}

		// Set flag for collision
		slot->collision = true;

		// Iterate to next collision key
		key = (key * COLLISION_MULTIPLIER + COLLISION_INCREMENTER) & HASH_TABLE_MASK;
		slot = &_map_table[key];

		// NOTE: This will loop forever if every table key is marked used
	}

	_count++;

	_flood_table[flood_key]++;

	// Mark used
	slot->conn = conn;
	conn->_key = key;
	conn->_flood_key = flood_key;

	lock.Release();

	CAT_INFO("ConnexionMap") << "Inserted connexion from " << conn->GetAddress().IPToString() << " : " << conn->GetAddress().GetPort() << " id=" << conn->GetKey();

	// Keeps reference held
	return true;
}

void ConnexionMap::Remove(Connexion *conn)
{
	if (!conn) return;

	u32 key = conn->_key;

	// If key is invalid,
	if (key >= HASH_TABLE_SIZE) return;

	CAT_INFO("ConnexionMap") << "Removing connexion from " << conn->GetAddress().IPToString() << " : " << conn->GetAddress().GetPort() << " id=" << conn->GetKey();

	conn->ReleaseRef(CAT_REFOBJECT_FILE_LINE);

	AutoWriteLock lock(_table_lock);

	// Remove connexion
	_map_table[key].conn = 0;

	// If at a leaf in the collision list,
	if (!_map_table[key].collision)
	{
		// Unset collision flags until first filled entry is found
		do 
		{
			// Roll backwards
			key = ((key + COLLISION_INCRINVERSE) * COLLISION_MULTINVERSE) & HASH_TABLE_MASK;

			// If collision list is done,
			if (!_map_table[key].collision)
				break;

			// Remove collision flag
			_map_table[key].collision = false;

		} while (!_map_table[key].conn);
	}

	_count--;

	_flood_table[conn->_flood_key]--;
}

void ConnexionMap::ShutdownAll()
{
	CAT_INFO("ConnexionMap") << "Requesting shutdown of all connexions";

	std::vector<Connexion*> connexions;

	AutoWriteLock lock(_table_lock);

	_is_shutdown = true;

	// For each hash table bin,
	for (int key = 0; key < HASH_TABLE_SIZE; ++key)
	{
		Connexion *conn = _map_table[key].conn;

		// If table entry is populated,
		if (conn)
		{
			conn->AddRef(CAT_REFOBJECT_FILE_LINE);
			connexions.push_back(conn);

			_map_table[key].conn = 0;
		}

		_map_table[key].collision = false;
	}

	_count = 0;

	lock.Release();

	// For each Connexion object to release,
	for (u32 ii = 0, size = (u32)connexions.size(); ii < size; ++ii)
	{
		Connexion *conn = connexions[ii];

		conn->Destroy(CAT_REFOBJECT_FILE_LINE);
		conn->ReleaseRef(CAT_REFOBJECT_FILE_LINE);
	}
}
