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

#include <cat/parse/BufferTok.hpp>
using namespace cat;


BufferTok::BufferTok(const char *bufferi, int leni)
{
    buffer = bufferi;
    len = leni;
    delimiter = ' ';
    newline = true;
}

/*
 * operator(char)
 * Set the delimiter character 
 * 
 * This character is used to find the end of the current token
 * Newlines are always a delimiter
 */
BufferTok &BufferTok::operator()(char ch)
{
    delimiter = ch;
    return *this;
}

/*
 * Same as operator(), see above
 * 
 * Also resets the newline() flag, so that data can start being
 * read from the next line.  If newline() wasn't set, it searches
 * for the next line to start reading.
 */
BufferTok &BufferTok::operator[](char ch)
{
    delimiter = ch;
    if (!newline)
    {
        char last = 0;

        while (len)
        {
            ch = *buffer;

            if (ch == '\r' || ch == '\n')
            {
                if (newline && ch != last)
                {
                    ++buffer;
                    --len;
                    break;
                }

                newline = true;
                last = ch;

                ++buffer;
                --len;
            }
            else
            {
                if (newline) break;

                ++buffer;
                --len;
            }
        }
    }
    newline = false;
    return *this;
}

// Read more bytes from the buffer
u32 BufferTok::readNext(char *token, u32 tokenBufferSize)
{
    if (!len || newline)
    {
        *token = 0;
        return 0;
    }

    bool seenNonSpace = false;
    char ch, last = 0;
    u32 copied = 0;

    while (len)
    {
        ch = *buffer;

        if (newline)
        {
            if (ch != last && (ch == '\r' || ch == '\n'))
            {
                ++buffer;
                --len;
            }
            break;
        }

        if (ch == delimiter)
        {
            if (delimiter != ' ' || seenNonSpace)
            {
                ++buffer;
                --len;
                break;
            }
        }

        switch (ch)
        {
        case ' ':
            if (seenNonSpace && last != ' ')
            {
                if (copied < tokenBufferSize-1)
                    token[copied++] = ch;
                last = ch;
            }
            break;
        case '\r':
        case '\n':
            newline = true;
            last = ch;
            break;
        default:
            if ((u8)(ch - '!') < 94)
            {
                seenNonSpace = true;
                if (copied < tokenBufferSize-1)
                    token[copied++] = ch;
                last = ch;
            }
        }

        ++buffer;
        --len;
    }

    if (last == ' ') --copied;

    token[copied] = 0;

    return copied;
}

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
BufferTok &BufferTok::operator>>(int &n)
{
    char work[256];

    u32 copied = readNext(work, sizeof(work));

    n = copied ? atoi(work) : 0;

    return *this;
}

BufferTok &BufferTok::operator>>(char *n)
{
    readNext(n, 256);

    return *this;
}
