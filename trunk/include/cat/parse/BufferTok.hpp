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

#ifndef BUFFER_TOK_HPP
#define BUFFER_TOK_HPP

#include <cat/Platform.hpp>

namespace cat {


/*
	MyBuf = "   123     5 :  6  "

	BufferTok bt(MyBuf, 256);

	int a, b, c; 
	bt() >> a; // empty delimiter means white space and newlines
	bt(':') >> b >> c;
	if (!bt) // out of buffer space
*/

// Tokenize a buffer
class BufferTok
{
	const char *buffer;
	int len;

	char delimiter;
	bool newline;

	u32 readNext(char *token, u32 tokenBufferSize);

public:
	BufferTok(const char *buffer, int len);

	// Returns true when the buffer is empty
	inline bool operator!() { return len <= 0; }

	// Returns true when the buffer extraction is stuck on a new line
	// Use operator[] to reset this flag
	inline bool onNewline() { return newline; }

	/*
	 * operator(char)
	 * Set the delimiter character 
	 * 
	 * This character is used to find the end of the current token
	 * Newlines are always a delimiter
	 */
	BufferTok &operator()(char ch = 0);

	/*
	 * Same as operator(), see above
	 * 
	 * Also resets the newline() flag, so that data can start being
	 * read from the next line.  If newline() wasn't set, it searches
	 * for the next line to start reading.
	 */
	BufferTok &operator[](char ch);

	/*
	 * operator>>
	 * Extract a token
	 * 
	 * Strips whitespace and the end-of-token delimiter
	 * Newlines are always a delimiter
	 * 
	 * After the tokenizer encounters a newline, it will continue
	 * returning no results with newline() flag set, until the
	 * delimiter is reset with [], so incomplete lines don't wrap.
	 */
	BufferTok &operator>>(int &n);
	BufferTok &operator>>(char *n); // buffer must have at least 256 characters
};


} // namespace cat

#endif // BUFFER_TOK_HPP
