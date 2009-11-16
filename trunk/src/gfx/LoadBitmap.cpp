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

#include <cat/gfx/LoadBitmap.hpp>
using namespace std;

#ifdef CAT_PRAGMA_PACK
#pragma pack(push)
#pragma pack(1)
#endif

struct BMPHeader
{
	char signature[2];
	char reserved[8];
	u32 dataOffset;
	u32 bmpHeaderSize;
	u32 width;
	u32 height;
	u16 planes;
	u16 bpp;
	u32 compression;
	u32 imageSize, xPelsPerMeter, yPelsPerMeter, colorCount, importantColors;
} CAT_PACKED;

#ifdef CAT_PRAGMA_PACK
#pragma pack(pop)
#endif


static u32 nextHighestPOT(u32 n)
{
	u32 b = 2;
	while (n >>= 1) b <<= 1;
	return b;
}

void BMPTokenizer::rasterizeImage(u8 *image)
{
	u32 newWidth, newHeight;

	if (requirePOTS)
	{
		newWidth = IS_POWER_OF_2(header.width) ? header.width : nextHighestPOT(header.width);
		newHeight = IS_POWER_OF_2(header.height) ? header.height : nextHighestPOT(header.height);
	}
	else
	{
		newWidth = header.width;
		newHeight = header.height;
	}

	auto_ptr<u8> final(new u8[4 * newWidth * newHeight]);
	ENFORCE(final.get()) << "Out of memory: Unable to allocate texture data";

	u8 *rgba_ptr = final.get();
	
	for (int y = header.height - 1; y >= 0; --y)
	{
		u8 *irow = image + y * header.width * 3;
		u32 hctr = header.width;

		while (hctr--)
		{
			u8 b = *irow++;
			u8 g = *irow++;
			u8 r = *irow++;

			*rgba_ptr++ = r;
			*rgba_ptr++ = g;
			*rgba_ptr++ = b;

			if ((r == trans_red) && (g == trans_green) && (b == trans_blue))
				*rgba_ptr++ = 0;
			else
				*rgba_ptr++ = 0xff;
		}

		rgba_ptr += (newWidth - header.width) * 4;
	}

	onImage((u32*)final.get(), newWidth, newHeight);
}

void BMPTokenizer::onImage(u32 *image, u32 newWidth, u32 newHeight)
{
	texture->load(image, newWidth, newHeight, header.width, header.height);
}

BMPTokenizer::BMPTokenizer(const string &path, bool prequirePOTS, Texture *ptexture)
	: mmf(path.c_str()), requirePOTS(prequirePOTS), texture(ptexture)
{
	ENFORCE(mmf.isValid()) << "Unable to read file: " << path;

	ENFORCE(!mmf.underrun(sizeof(BMPHeader))) << "Corrupt bitmap: " << path;
	BMPHeader *fh = (BMPHeader *)mmf.read(sizeof(BMPHeader));

	ENFORCE(fh->signature[0] == 'B' && fh->signature[1] == 'M') << "Not a bitmap: " << path;

	header.dataOffset = getLE(fh->dataOffset);
	header.bmpHeaderSize = getLE(fh->bmpHeaderSize);
	header.width = getLE(fh->width);
	header.height = getLE(fh->height);
	header.planes = getLE(fh->planes);
	header.bpp = getLE(fh->bpp);

	ENFORCE(header.planes == 1) << "Bitmap planes must not exceed 1: " << path;
	ENFORCE(header.bpp == 24) << "Bitmap BPP must be 24: " << path;

	trans_red = 0;
	trans_green = 0;
	trans_blue = 0;

	mmf.seek(header.dataOffset);
	ENFORCE(!mmf.underrun(3 * header.width * header.height)) << "Truncated bitmap data: " << path;
	rasterizeImage((u8*)mmf.look());
}

BMPTokenizer::~BMPTokenizer()
{
}
