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

#include <cat/parse/MessageRouter.hpp>
using namespace cat;


void MessageRouter::Set(u8 opcode, const MessageHandler &handler)
{
	handlers[opcode] = handler;
}

void MessageRouter::Clear(u8 opcode)
{
	handlers[opcode].clear();
}

void MessageRouter::Invoke(u8 opcode, BitStream &msg)
{
	if (handlers[opcode])
	{
		handlers[opcode](msg);
	}
}
