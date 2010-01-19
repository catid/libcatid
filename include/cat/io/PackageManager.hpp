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

#ifndef CAT_PACKAGE_MANAGER_HPP
#define CAT_PACKAGE_MANAGER_HPP

#include <cat/Platform.hpp>

namespace cat {


struct PackageAddress
{
	u32 offset, size;

	CAT_INLINE PackageAddress(u32 noffset, u32 nsize) { offset = noffset; size = nsize; }
};


// Package resource identifier macro
#define CAT_UNPACK(packagePath, offset, size) PackageAddress(offset, size)

/*
	All file resources are packed into one large file and each is
	assigned a unique identifying number, starting from 0.

	The client source code is preprocessed by a tool that replaces the
	arguments of the CAT_UNPACK() macro with the correct ID number based
	on the string given as the first argument.

	CAT_UNPACK("world1/lightmap3.png", 0, 0)
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

	<chunk 0 offset(4 bytes)>
	<chunk 0 size(4 bytes)>
	"string name for chunk 0\0"

	<chunk 1 offset(4 bytes)>
	<chunk 1 size(4 bytes)>
	"string name for chunk 1\0"

	...

	[data for chunk 0]
	[data for chunk 1]

	...

	eof
*/


} // namespace cat

#endif // CAT_PACKAGE_MANAGER_HPP
