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

#include <cat/threads/Atomic.hpp>
using namespace cat;

bool Atomic::UnitTest()
{
	u32 x = ~0;
	if (Atomic::Add(&x, 1) != ~0 || x != 0) return false;
	if (Atomic::Add(&x, 1) != 0 || x != 1) return false;
	if (Atomic::Add(&x, 1) != 1 || x != 2) return false;

	if (Atomic::Add(&x, -1) != 2 || x != 1) return false;
	if (Atomic::Add(&x, -1) != 1 || x != 0) return false;
	if (Atomic::Add(&x, -1) != 0 || x != ~0) return false;

	x = 23;
	u32 y = 52;
	if (Atomic::Set(&x, y) != 23) return false;
	y = 23;
	if (Atomic::Set(&x, y) != 52) return false;
	if (Atomic::Set(&x, y) != 23) return false;

	u32 w = 4;
	if (Atomic::BTS(&w, 0) || w != 5) return false;
	if (!Atomic::BTS(&w, 0) || w != 5) return false;
	if (!Atomic::BTR(&w, 0) || w != 4) return false;
	if (Atomic::BTS(&w, 0) || w != 5) return false;
	if (!Atomic::BTR(&w, 0) || w != 4) return false;
	if (Atomic::BTS(&w, 1) || w != 6) return false;

	return true;
}
