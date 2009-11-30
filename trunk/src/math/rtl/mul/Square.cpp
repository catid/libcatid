/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#include <cat/math/BigRTL.hpp>
using namespace cat;

/*
	I've been able to get about a 3% overall improvement by implementing a huge,
	specialized Comba square function.  It's really not worth the complexity.

	The main reason Comba fails to be effective for squaring is that there
	is no good way to represent the opcodes required in C++.

	Despite these limitations squaring remains faster than multiplying.
*/

void BigRTL::Square(const Leg *input, Leg *output)
{
    Leg *cross = Get(library_regs - 2);

    // Calculate square products
    for (int ii = 0; ii < library_legs; ++ii)
        CAT_LEG_MUL(input[ii], input[ii], output[ii*2+1], output[ii*2]);

    // Calculate cross products
    cross[library_legs] = MultiplyX(library_legs-1, input+1, input[0], cross+1);
    for (int ii = 1; ii < library_legs-1; ++ii)
        cross[library_legs + ii] = MultiplyXAdd(library_legs-1-ii, input+1+ii, input[ii], cross+1+ii*2, cross+1+ii*2);

    // Multiply the cross product by 2 and add it to the square products
    output[library_legs*2-1] += DoubleAdd(library_legs*2-2, cross+1, output+1, output+1);
}
