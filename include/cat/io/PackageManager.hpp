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

#ifndef PACKAGE_MANAGER_HPP
#define PACKAGE_MANAGER_HPP

#include <cat/Platform.hpp>

namespace cat {


struct PackageAddress
{
	u32 offset, size;

	CAT_INLINE PackageAddress(u32 noffset, u32 nsize) { offset = noffset; size = nsize; }
};


// Package resource identifier macro
#define CAT_UNPACK(packagePath) "You need to run the preprocessor!"
#define CAT_UNPACK(packagePath, offset, size) PackageAddress(offset, size)

/*
	All file resources are packed into one large file and each is
	assigned a unique identifying number, starting from 0.

	The client source code is preprocessed by a tool that replaces the
	second argument to instances of the CAT_UNPACK() macro with the
	correct ID number based on the string given as the first argument.

	CAT_UNPACK("world1/lightmap3.png")
	-> CAT_UNPACK("world1/lightmap3.png", 15241, 256)

	At runtime the client application will not be aware of the string
	name of a resource in the package, only where to go to get it.

	Resources that are used together during tuning will have identifiers
	that are close together so that disk seek time is minimized.
*/


/*
	Package File Format:

	<magic(8 bytes)>
	<chunk array length(4 bytes)>
	<chunk 0 name offset(4 bytes)>
	<chunk 0 file offset(4 bytes)>
	<chunk 0 file size(4 bytes)>
	<chunk 1 name offset(4 bytes)>
	<chunk 1 file offset(4 bytes)>
	<chunk 1 file size(4 bytes)>
	...
	"string name for chunk 0\0"
	"string name for chunk 1\0"
	...
	[data for chunk 0]
	[data for chunk 1]
	...
	eof
*/


} // namespace cat

#endif // PACKAGE_MANAGER_HPP
