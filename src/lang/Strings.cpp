/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

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

#include <cat/lang/Strings.hpp>
using namespace cat;

#if defined(CAT_UNKNOWN_BUILTIN_ISTRCMP)

bool cat::iStrEqual(const char *A, const char *B)
{
	// Forever,
	for (;;)
	{
		// Grab next character from each string
		char a = *A++;
		char b = *B++;

		// Convert to lower case if needed
		if (a >= 'A' && a <= 'Z') a += 'a' - 'Z';
		if (b >= 'A' && b <= 'Z') b += 'a' - 'Z';

		// If characters do not match, return false
		if (a != b) return false;

		// If both characters are '\0', we have reached
		// the end and no characters were different
		if (a == '\0') return true;
	}
}

#endif // CAT_UNKNOWN_BUILTIN_ISTRCMP


// Get length of string that has a maximum length (potentially no trailing nul)
u32 cat::GetFixedStrLen(const char *str, u32 max_len)
{
	for (u32 ii = 0; ii < max_len; ++ii)
		if (str[ii] == '\0')
			return ii;

	return max_len;
}


// Set a fixed string buffer (zero-padded) from a variable-length string,
// both either zero or length-terminated.  Returns length of copied string
u32 cat::SetFixedStr(char *dest, u32 dest_len, const char *src, u32 src_max_len)
{
	u32 ii;

	// Copy characters until source or destination buffer ends or encounter null
	for (ii = 0; ii < dest_len && ii < src_max_len; ++ii)
	{
		char ch = src[ii];

		if (ch == '\0')
			break;

		dest[ii] = ch;
	}

	u32 copied = ii;

	// Pad destination with null bytes
	// NOTE: Does not guarantee the destination is null-terminated
	for (; ii < dest_len; ++ii)
	{
		dest[ii] = '\0';
	}

	return copied;
}
