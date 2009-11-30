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
	Comba 8x8 square:

	<--- most significant               least significant --->
	                             a   b   c   d   e   f   g   h
	X                            a   b   c   d   e   f   g   h
	----------------------------------------------------------
	+                          2ah 2bh 2ch 2dh 2eh 2fh 2gh  hh
	                       2ag 2bg 2cg 2dg 2eg 2fg  gg
	                   2af 2bf 2cf 2df 2ef  ff
	               2ae 2be 2ce 2de  ee
	           2ad 2bd 2cd  dd
	       2ac 2bc  cc
	   2ab  bb
	aa
*/
void CombaSquare8(const Leg *input, Leg *output)
{
	Leg C0, C1, C2;

	C2 = 0;
	CAT_LEG_MUL(input[0], input[0], C1, C0);
	output[0] = C0;

	CAT_LEG_COMBA_DBL2(input[0], input[1], C1, C2, C0);
	output[1] = C1;

	CAT_LEG_COMBA_DBL2(input[0], input[2], C2, C0, C1);
	CAT_LEG_COMBA3(input[1], input[1], C2, C0, C1);
	output[2] = C2;

	CAT_LEG_COMBA_DBL2(input[0], input[3], C0, C1, C2);
	CAT_LEG_COMBA_DBL3(input[1], input[2], C0, C1, C2);
	output[3] = C0;

	CAT_LEG_COMBA_DBL2(input[0], input[4], C1, C2, C0);
	CAT_LEG_COMBA_DBL3(input[1], input[3], C1, C2, C0);
	CAT_LEG_COMBA3(input[2], input[2], C1, C2, C0);
	output[4] = C1;

	CAT_LEG_COMBA_DBL2(input[0], input[5], C2, C0, C1);
	CAT_LEG_COMBA_DBL3(input[1], input[4], C2, C0, C1);
	CAT_LEG_COMBA_DBL3(input[2], input[3], C2, C0, C1);
	output[5] = C2;

	CAT_LEG_COMBA_DBL2(input[0], input[6], C0, C1, C2);
	CAT_LEG_COMBA_DBL3(input[1], input[5], C0, C1, C2);
	CAT_LEG_COMBA_DBL3(input[2], input[4], C0, C1, C2);
	CAT_LEG_COMBA3(input[3], input[3], C0, C1, C2);
	output[6] = C0;

	CAT_LEG_COMBA_DBL2(input[0], input[7], C1, C2, C0);
	CAT_LEG_COMBA_DBL3(input[1], input[6], C1, C2, C0);
	CAT_LEG_COMBA_DBL3(input[2], input[5], C1, C2, C0);
	CAT_LEG_COMBA_DBL3(input[3], input[4], C1, C2, C0);
	output[7] = C1;

	CAT_LEG_COMBA_DBL2(input[1], input[7], C2, C0, C1);
	CAT_LEG_COMBA_DBL3(input[2], input[6], C2, C0, C1);
	CAT_LEG_COMBA_DBL3(input[3], input[5], C2, C0, C1);
	CAT_LEG_COMBA3(input[4], input[4], C2, C0, C1);
	output[8] = C2;

	CAT_LEG_COMBA_DBL2(input[2], input[7], C0, C1, C2);
	CAT_LEG_COMBA_DBL3(input[3], input[6], C0, C1, C2);
	CAT_LEG_COMBA_DBL3(input[4], input[5], C0, C1, C2);
	output[9] = C0;

	CAT_LEG_COMBA_DBL2(input[3], input[7], C1, C2, C0);
	CAT_LEG_COMBA_DBL3(input[4], input[6], C1, C2, C0);
	CAT_LEG_COMBA3(input[5], input[5], C1, C2, C0);
	output[10] = C1;

	CAT_LEG_COMBA_DBL2(input[4], input[7], C2, C0, C1);
	CAT_LEG_COMBA_DBL3(input[5], input[6], C2, C0, C1);
	output[11] = C2;

	CAT_LEG_COMBA_DBL2(input[5], input[7], C0, C1, C2);
	CAT_LEG_COMBA3(input[6], input[6], C0, C1, C2);
	output[12] = C0;

	CAT_LEG_COMBA_DBL2(input[6], input[7], C1, C2, C0);
	output[13] = C1;

	CAT_LEG_COMBA3(input[7], input[7], C2, C0, C1);
	output[14] = C2;
	output[15] = C0;
}

void BigRTL::Square(const Leg *input, Leg *output)
{
	if (library_legs == 8)
	{
		CombaSquare8(input, output);
		return;
	}

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
