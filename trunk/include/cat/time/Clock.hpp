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

#ifndef CAT_CLOCK_HPP
#define CAT_CLOCK_HPP

#include <cat/Platform.hpp>
#include <string>

namespace cat {


class Clock
{
#ifdef CAT_OS_WINDOWS
	u32 period;
#endif

public:
	Clock();
	~Clock();

	static u32 cycles();    // timestamp in cycles

	static u32 sec(); 		// timestamp in seconds
	static u32 hsec(); 		// timestamp in hundredths of a second
	static u32 msec(); 		// timestamp in milliseconds
	static double usec(); 	// timestamp in microseconds

	static std::string format(const char *format_string);

	static void sleep(u32 milliseconds);

	static bool SetHighPriority();
	static bool SetNormalPriority();

	static u32 MeasureClocks(int iterations, void (*FunctionPtr)());
};


} // namespace cat

#endif // CAT_CLOCK_HPP
