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
// 05/14/09 added LUT to speed decompression
// 05/10/09 first revision

#ifndef CAT_RANGE_CODER_HPP
#define CAT_RANGE_CODER_HPP

#include <cat/platform.hpp>
#include <ostream>
#include <map>
#include <string>

namespace cat {


/*
	TextStatsCollector

	Collects order-1 statistics of the text given one character at a time.
	Order-1 statistics include the likelihood of a character given the previous one.

	This is intended to be used on a large sample of text (of unlimited length)
	to come up with statistics that most text should follow.  When the resulting
	table is used with a range coder, compression should be reliably achieved,
	though a bit should be allocated for the case where the result of the coder
	would be longer than encoding with uniform ranges, like if someone enters "ZZZZ".

	The RangeEncoder class has a text compressor based on the output of this class.

	I opted for a static table because this is to be run on a network server
	where many clients are connected.  The memory needed for this type of
	compression does not increase with the number of clients.
*/
class TextStatsCollector
{
	u32 last, total, frequencies[256][256];
	u8 seen[256];

public:
	TextStatsCollector();

public:
	// Record a character occurance
	// 0 = end of line, so next character counts towards initial character frequency
	void Tally(u8 x);

#if defined(CAT_COMPILER_MSVC)
#pragma pack(push)
#pragma pack(1)
#endif

	struct TableFormat
	{
		// MurmurHash2 of remainder, with seed = 0
		u32 hash;

		// Total symbols in the table <= 256
		u16 total;

		// Fraction of a byte represented by total, scaled from [0, 2^15]
		u16 log2total;

		// ASCII character code -> Table index map
		u8 char2index[256];

		// Table index -> ASCII character code map
		u8 index2char[256];

		/*
			Start of frequency table

			The first 32 entries are used for reverse lookup (freq->symbol):
			GET_SYMBOL_LUT() will get this address:
			frequencies[0..15] = array of 16 bytes creating a lookup table (LUT) given
								the high 4 bits of the frequency, for the low range
			frequencies[16..31] = array of 16 bytes creating a lookup table (LUT) given
								the high 4 bits of the frequency, for the high range

			GET_SYMBOL_BASE() will get this address:
			frequencies[32] = cumulative frequency for (last=0, this=1) out of 2^16 trials
			frequencies[33] = cumulative frequency for (last=0, this=2) out of 2^16 trials
			frequencies[34] = ...

			Note: (0, 0) is implicitly zero, and (0, TOTAL) is implicitly 2^16
			So these tables don't include those implicit entries.
		*/
		u16 frequencies[1];
	} CAT_PACKED;

#if defined(CAT_COMPILER_MSVC)
#pragma pack(pop)
#endif

	// Returns code that creates a table in the above format
	bool GenerateMinimalStaticTable(const char *TableName, std::ostream &osout);

	// Check for errors in the in-memory version of the table that was generated
	static bool VerifyTableIntegrity(const TableFormat *table);
};


/*
	Range Encoder

	Encodes a single message one field at a time using the minimum
	number of bits, rounded up to the next highest byte.

	To insure that the message does not grow in size, provide limited
	space for the output buffer and check .Fail() at the end.  If it
	failed, this means it ran out of space during encoding.

	If encoding succeeded, check .Used() to determine the number of
	bytes used by the output buffer.
*/
class RangeEncoder
{
	u8 *output;
	int limit, remaining;

	u64 low, range;

	// Write out bytes as needed
	CAT_INLINE void Normalize();

	CAT_INLINE void GetTableSymbol(const TextStatsCollector::TableFormat *stats, u32 &last, u8 ch, u16 &symbol_low, u16 &symbol_range);

public:
	// Ctors
	RangeEncoder(void *output_i, int limit_i);
	RangeEncoder(RangeEncoder &cp);

	// Overwrite one context with another.  Using context references instead
	// of working on the contexts directly is probably more efficient.
	RangeEncoder &operator=(RangeEncoder &cp);

	// State accessors
	bool Fail() { return output == 0; }
	int Used() { return limit - remaining; }

public:
	// Encode the given text with the given statistics
	// NOTE: May be up to one byte longer than the original message
	void Text(const char *msg, const TextStatsCollector::TableFormat *stats);

	// Encode a biased bit given the frequency it is 0
	// frequency = average times out of 2^32 trials the bit will be 0
	void BiasedBit(u32 b, u32 frequency);

	// Encode a field that takes on [0, total) values with equal likelihood
	void Range(u32 symbol, u32 total);

	// Emit the final byte(s) needed to encode the symbols
	void Finish();
};


/*
	Range Decoder

	Interprets buffers produced by RangeEncoder
*/
class RangeDecoder
{
	const u8 *input;
	int remaining;
	u64 code, low, range;

	// Read in bytes as needed
	CAT_INLINE void Normalize();

	// Grab symbol low frequency and range from the table
	CAT_INLINE u8 GetTableSymbol(const TextStatsCollector::TableFormat *stats, u32 &last, u16 freq, u16 &symbol_low, u16 &symbol_range);

public:
	// Intializing constructor
	RangeDecoder(const void *message, int bytes);

	int Remaining() { return remaining; }

public:
	// Decode the given text with the given statistics
	int Text(char *msg, int buffer_size, const TextStatsCollector::TableFormat *stats);

	// Decode a biased bit given the frequency it is 0
	// frequency = average times out of 2^32 trials the bit will be 0
	u32 BiasedBit(u32 frequency);

	// Decode a field that takes on [0, total) values with equal likelihood
	u32 Range(u32 total);
};


} // namespace cat

#endif // CAT_RANGE_CODER_HPP
