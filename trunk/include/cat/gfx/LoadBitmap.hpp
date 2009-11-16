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

#ifndef LOAD_BITMAP_HPP
#define LOAD_BITMAP_HPP

#include <cat/Platform.hpp>

namespace cat {


/*
	LoadBitmap() loads a memory-mapped bitmap or a file from disk into RGBA format,
	meaning the first byte of every pixel is Red, then Green, Blue, and Alpha.
	The output data is useful for creating an OpenGL texture using GL_RGBA format.
	The output width and height will be powers of two.

	Parameters:
		file, bytes : Memory-mapped file pointer and number of bytes in the file
		path : Alternatively, the path to the file to load
		width, height : The dimensions of the loaded file, in pixels

	Returns: Zero on error, or a pointer to the rasterized RGBA pixels.

	Free the allocated memory using Aligned::Delete(a);
*/
void *LoadBitmap(void *file, u32 bytes, u32 &width, u32 &height);
void *LoadBitmap(const char *path, u32 &width, u32 &height);


class BMPTokenizer
{
	u8 trans_red, trans_green, trans_blue;

	bool requirePOTS; // Require Power-of-Two Size

	void rasterizeImage(u8 *image);
	void onImage(u32 *image, u32 newWidth, u32 newHeight);

public:
	BMPTokenizer();
	~BMPTokenizer();

public:
	bool LoadFile(const char *path);
};


} // namespace cat

#endif // LOAD_BITMAP_HPP
