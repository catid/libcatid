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

/*
    Unit test for the Elliptic Curve Cryptography code

    Performs 10,000 simulated key exchanges, terminating on any error

    Demonstrates how to use the ECC code
*/

#include <cat/AllMath.hpp>
#include <iostream>
#include <map>
#include "SecureServerDemo.hpp"
#include "SecureClientDemo.hpp"
#include <cat/crypt/rand/Fortuna.hpp>
using namespace std;
using namespace cat;

// Generate candidate values for c for the ECC.cpp code
void GenerateCandidatePrimes();

// Generate table used for w-MOF in the ECC.cpp code
void GenerateMOFTable(int window_bits);

// Compare Barrett, Montgomery, and Special modulus functions for speed
void TimeModulusCode();

// ECC protocol test
void ECCTest();

// Full handshake test
void HandshakeTest();

// IV reconstruction test
void TestIVReconstruction();

void TestSkein256();
void TestSkein512();

void TestCurveParameterD();

void TestChaCha();
/*
void test1()
{
    u32 x = Degree32(0x7ffffff);
}

void test2()
{
    u32 x = SecondDegree32(0x7ffff);
}
*/

void GenerateWMOFTable()
{
    const int w = 8;

    const int W = 1 << w;

    int print = 0;
    for (int kk = 0; kk < 2; ++kk)
    {
        for (int ii = 0; ii < W; ++ii)
        {
            int top = ii;
            int bot = (ii >> 1) | (kk << (w-1));
            int val = 0;

            for (int jj = 0; jj < w; ++jj)
            {
                val += (top & (1 << jj)) - (bot & (1 << jj));
            }

            int x = ii;

            if (kk)
            {
                x = (~ii & (W-1));
            }

            if (val)
            {
                int input = val - 1;

                int d = 0;
                while ((val & 1) == 0)
                {
                    val >>= 1;
                    ++d;
                }

                int neg = val < 0;
                if (val < 0) val = -val;

                int index = (val - 1) / 2;

                if (neg)
                {
                    //cout << "last=" << kk << ", x=" << x << " -> -" << index << "d" << d << endl;
                }
                else
                {
                    if ((ii & 1) == 0)
                    {
                        cout << "{" << index << "," << d << "},";
                        if (++print % 8 == 0) cout << endl;
                    }
                    //cout << "last=" << kk << ", in=" << input << " -> +" << index << "d" << d << endl;
                }
            }
            else
            {
                //cout << "last=" << kk << ", x=" << x << " -> zero" << endl;
            }
        }
    }
}

void AddTest(const Leg *in_a, const Leg *in_b, Leg *out)
{
    const int library_legs = 4;
    const Leg modulus_c = 189;
}

int TestDivide()
{
    BigRTL x(10, 256);
    Leg *a = x.Get(0);
    Leg *b = x.Get(1);
    Leg *q = x.Get(2);
    Leg *r = x.Get(3);
    Leg *p = x.Get(4);

    MersenneTwister mtprng;
    mtprng.Initialize();

    for (;;)
    {
        mtprng.Generate(a, x.RegBytes());
        mtprng.Generate(b, x.RegBytes());

        cout << hex << "a = ";
        for (int ii = x.Legs()-1; ii >= 0; --ii)
            cout << hex << a[ii] << " ";
        cout << endl;

        cout << hex << "b = ";
        for (int ii = x.Legs()-1; ii >= 0; --ii)
            cout << hex << b[ii] << " ";
        cout << endl;

        x.Divide(a, b, q, r);

        cout << hex << "a' = ";
        for (int ii = x.Legs()-1; ii >= 0; --ii)
            cout << hex << a[ii] << " ";
        cout << endl;

        cout << hex << "b' = ";
        for (int ii = x.Legs()-1; ii >= 0; --ii)
            cout << hex << b[ii] << " ";
        cout << endl;

        cout << hex << "q = ";
        for (int ii = x.Legs()-1; ii >= 0; --ii)
            cout << hex << q[ii] << " ";
        cout << endl;

        cout << hex << "r = ";
        for (int ii = x.Legs()-1; ii >= 0; --ii)
            cout << hex << r[ii] << " ";
        cout << endl;

        x.Multiply(q, b, p);

        cout << hex << "p' = ";
        for (int ii = 2*x.Legs()-1; ii >= 0; --ii)
            cout << hex << p[ii] << " ";
        cout << endl;

        x.Add(p, r, p);

        cout << hex << "p = ";
        for (int ii = 2*x.Legs()-1; ii >= 0; --ii)
            cout << hex << p[ii] << " ";
        cout << endl;

        if (!x.Equal(p, a))
        {
            cout << "FAILURE: Divide" << endl;
            return 1;
        }
    }
}

int TestModularInverse()
{
    BigPseudoMersenne x(10, 256, 189);
    Leg *a = x.Get(0);
    Leg *modulus = x.Get(1);
    Leg *inverse = x.Get(2);
    Leg *p = x.Get(3);

    MersenneTwister mtprng;
    mtprng.Initialize();

    for (;;)
    {
        mtprng.Generate(a, x.RegBytes());

        x.MrInvert(a, inverse);
        x.MrMultiply(a, inverse, p);
        x.MrReduce(p);

        if (!x.EqualX(p, 1))
        {
            cout << "FAILURE: Inverse" << endl;
            return 1;
        }
    }
}

int TestSquareRoot()
{
    BigPseudoMersenne x(10, 256, 189);
    Leg *a = x.Get(0);
    Leg *modulus = x.Get(1);
    Leg *inverse = x.Get(2);
    Leg *p = x.Get(3);
    Leg *s = x.Get(4);
    Leg *t = x.Get(5);
    Leg *u = x.Get(6);

    MersenneTwister mtprng;
    mtprng.Initialize();

    for (;;)
    {
        mtprng.Generate(a, x.RegBytes());

        x.MrSquare(a, s);

        x.MrSquareRoot(s, t);

        if (!x.Equal(a, t))
        {
            x.MrNegate(t, t);

            if (!x.Equal(a, t))
            {
                cout << "FAILURE: Square" << endl;
                return 1;
            }
        }
    }

    return 0;
}

int TestTwistedEdward()
{
    BigTwistedEdward x(100, 256, 189, 321);
    Leg *a = x.Get(0);
    Leg *modulus = x.Get(1);
    Leg *inverse = x.Get(2);
    Leg *p = x.Get(3);
    Leg *s = x.Get(4);
    Leg *t = x.Get(5);
    Leg *u = x.Get(6);
    Leg *pt = x.Get(7);

    MersenneTwister mtprng;
    mtprng.Initialize();

    for (;;)
    {
        x.PtGenerate(&mtprng, pt);

        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEDouble(pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtEDouble(pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtDouble(pt, pt);
        x.PtEDouble(pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtAdd(pt, pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtDouble(pt, pt);
        x.PtEDouble(pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtEAdd(pt, pt, pt);
        x.PtAdd(pt, pt, pt);

        x.SaveAffineXY(pt, t, u);

        if (!x.LoadVerifyAffineXY(t, u, pt))
        {
            cout << "FAILURE: TE" << endl;
        }
        else
        {
            //cout << "SUCCESS: TE" << endl;
        }
    }

    return 0;
}

int main()
{
    //GenerateWMOFTable();
    //return 0;
    //return TestDivide();
    //return TestModularInverse();
    //return TestSquareRoot();
    //return TestTwistedEdward();

    if (!FortunaFactory::ref()->Initialize())
    {
        cout << "FAILURE: Unable to initialize the Fortuna factory" << endl;
        return 1;
    }
/*
    cout << endl << "Atomic testing:" << endl;
    if (!Atomic::UnitTest())
    {
        cout << "FAILURE: Atomic test failed" << endl;
        return 1;
    }
    cout << "SUCCESS: Atomic functions work!" << endl;

/*
    cout << endl << "ECC testing:" << endl;
    ECCTest();
*/
    //return 0;
/*
    cout << endl << "Testing curve parameter d:" << endl;
    TestCurveParameterD();

    cout << "Candidate primes for ECC:" << endl;
    GenerateCandidatePrimes();
*/
    cout << endl << "w-MOF table generation:" << endl;
    GenerateMOFTable(4);
/*
    cout << endl << "Modulus code timing:" << endl;
    TimeModulusCode();
*/
    cout << endl << "Full handshake testing:" << endl;
    HandshakeTest();

    cout << endl << "IV reconstruction testing:" << endl;
    TestIVReconstruction();

    cout << endl << "Hash testing and timing:" << endl;
    TestSkein256();
    TestSkein512();

    cout << endl << "ChaCha testing and timing:" << endl;
    TestChaCha();

    return 0;
}
/*
void GenerateCandidatePrimes()
{
    u32 k[CAT_EDWARD_LIMBS];

    MersenneTwister prng;
    prng.Initialize();

    for (int ii = 1; ii < 1000; ii += 4)
    {
        Set32(k, CAT_EDWARD_LIMBS, 0);
        Subtract32(k, CAT_EDWARD_LIMBS, ii);

        if (RabinMillerPrimeTest(&prng, k, CAT_EDWARD_LIMBS, 40))
        {
            cout << "Candidate prime c = " << ii << endl;
        }
    }
}
*/
void GenerateMOFTable(int window_bits)
{
    cout << "When we see each combinations of w+1 bits, what operations should be performed?" << endl;
    cout << "It will be a number of doubles, then an addition by an odd number, then some more doublings." << endl;

    int top = 1 << (window_bits + 1);
    int cnt = 0;

    for (int ii = 0; ii < top; ++ii)
    {
        int r = 0;

        for (int jj = window_bits; jj >= 1; --jj)
        {
            int top = ii & (1 << (jj-1));
            int bottom = ii & (1 << jj);

            if (top)
            {
                if (bottom)
                {
                    // 0
                }
                else
                {
                    // 1
                    r += 1 << (jj - 1);
                }
            }
            else
            {
                if (bottom)
                {
                    // -1
                    r -= 1 << (jj - 1);
                }
                else
                {
                    // 0
                }
            }
        }

        int squares_before = window_bits;
        int squares_after = 0;
        if (r)
        {
            while (!(r&1))
            {
                --squares_before;
                ++squares_after;
                r >>= 1;
            }
        }

        cout << ii << "(";

        for (int jj = 1 << window_bits; jj; jj >>= 1)
        {
            if (ii & jj)    cout << '1';
            else            cout << '0';
        }

        cout << ") -> " << squares_before << "D + (" << r << ") + " << squares_after << "D" << endl;

        // test code for the bit twiddling version of w-MOF

        u32 bits = ii;
        u32 w = window_bits;

        // invert low bits if negative, and mask out high bit
        u32 z = (bits ^ -(s32)(bits >> w)) & ((1 << w) - 1);
        // perform shift and subtract to get positive index
        u32 x = z - (z >> 1);
        // compute number of trailing zeroes in x
        u32 y = x ^ (x - 1);
        u32 shift = ((15 - y) & 16) >> 2;
        y >>= shift;
        u32 s = shift;
        shift = ((3 - y) & 4) >> 1; y >>= shift; s |= shift;
        s |= (y >> 1);
        x >>= s;

        cout << "+ " << hex << (u32)x << endl;
        cout << "D " << dec << (u32)s << endl;

        if (x)
        {
            // compute table index
            x = ((x - 1) >> 1) + ((bits & (1 << w)) >> 2);
            cout << "Table # " << dec << (u32)x << endl;
        }
        else
            cout << "Table !Zero" << endl;

/*
        if (ii & 1)
        {
            if (r >= 0)
            {
                cout << "{" << ((r + 1) >> 1) - 1 << "," << squares_after << "},";
                if (++cnt == 8)
                {
                    cnt = 0;
                    cout << endl;
                }
            }
        }*/
    }
}

/*
void TestCurveParameterD()
{
    // Verify that d is not a square in Fp.
    // Attempt to find its square root and then square the root to see if it matches

    u32 d[CAT_EDWARD_LIMBS];
    u32 root[CAT_EDWARD_LIMBS];
    u32 square[CAT_EDWARD_LIMBS*2];
    u32 reduced[CAT_EDWARD_LIMBS];

    Set32(d, CAT_EDWARD_LIMBS, CAT_EDWARD_D);

    SpecialSquareRoot(CAT_EDWARD_LIMBS, d, CAT_EDWARD_C, root);

    Square(CAT_EDWARD_LIMBS, square, root);

    SpecialModulus(square, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, reduced);

    if (Equal(CAT_EDWARD_LIMBS, reduced, d))
    {
        cout << "FAILURE: Twisted Edwards curve parameter d is a square in Fp" << endl;
        return;
    }

    // Negate d and try it again
    Negate(CAT_EDWARD_LIMBS, d, d);
    Subtract32(d, CAT_EDWARD_LIMBS, CAT_EDWARD_C);

    SpecialSquareRoot(CAT_EDWARD_LIMBS, d, CAT_EDWARD_C, root);

    Square(CAT_EDWARD_LIMBS, square, root);

    SpecialModulus(square, CAT_EDWARD_LIMBS*2, CAT_EDWARD_C, CAT_EDWARD_LIMBS, reduced);

    if (Equal(CAT_EDWARD_LIMBS, reduced, d))
    {
        cout << "FAILURE: Twisted Edwards curve parameter d is a square in Fp" << endl;
        return;
    }

    cout << "SUCCESS: Twisted Edwards curve parameter d is NOT a square in Fp" << endl;
}
*//*
void TimeModulusCode()
{
#define LIMBS 8
    u32 a[LIMBS*2], b[LIMBS], modulus[LIMBS];
    u32 p_x[LIMBS], p_y[LIMBS];
    u32 three[LIMBS];

    Set32(three, LIMBS, 3);
    Set32(b, LIMBS, 11);
    Set32(p_x, LIMBS, 8);
    Set32(p_y, LIMBS, 15);

    //INFO("Test") << "Generating a pseudo-prime modulus...";
    //GenerateStrongPseudoPrime(modulus, LIMBS);
    Set32(modulus, LIMBS, 0);
    const u32 C = 189;
    Subtract32(modulus, LIMBS, C);
    cout << "Modulus: " << ToStr(modulus, LIMBS, 16) << endl;

    u32 v[LIMBS*2], v_inverse[LIMBS];
    u32 p[LIMBS*2+1], presidue[LIMBS*2+1], q[LIMBS*2], r[LIMBS];

    Set(v, LIMBS*2, modulus, LIMBS);
    BarrettModulusPrecomp(LIMBS, modulus, v_inverse);

    cout << "v_inverse = " << ToStr(v_inverse, LIMBS, 16) << endl;

    MersenneTwister prng;
    prng.Initialize();

    double ac1 = 0, ac2 = 0, ac3 = 0;
    for (int ii = 0; ii < 100000; ++ii)
    {
        prng.Generate(a, LIMBS*4);
        prng.Generate(b, LIMBS*4);
        Multiply(LIMBS, p, a, b);

        MonInputResidue(p, LIMBS*2, v, LIMBS, presidue);
        u32 mod_inv = MonReducePrecomp(v[0]);

        double clk1 = Clock::usec();
        BarrettModulus(LIMBS, p, v, v_inverse, q);
        double clk2 = Clock::usec();
        MonFinish(LIMBS, presidue, v, mod_inv);
        double clk3 = Clock::usec();
        SpecialModulus(p, LIMBS*2, C, LIMBS, r);
        double clk4 = Clock::usec();

        double t3 = clk4 - clk3;
        double t2 = clk3 - clk2;
        double t1 = clk2 - clk1;

        ac1 += t1;
        ac2 += t2;
        ac3 += t3;

        for (int ii = 0; ii < LIMBS; ++ii)
        {
            if (presidue[ii] != q[ii])
            {
                cout << "FAIL: Montgomery doesn't match Barrett" << endl;
                break;
            }
            if (presidue[ii] != r[ii])
            {
                cout << "FAIL: Montgomery doesn't match Special" << endl;
                cout << "Expected = " << ToStr(q, LIMBS, 16) << endl;
                cout << "Got      = " << ToStr(r, LIMBS, 16) << endl;
                Subtract(q, LIMBS, r, LIMBS);
                cout << "Difference = " << ToStr(q, LIMBS, 16) << endl;
                u32 crem = Divide32(LIMBS, q, C);
                cout << "Delta Divides C = " << ToStr(q, LIMBS, 16) << " times" << endl;
                cout << "Delta Modulus C = " << crem << endl;
                break;
            }
        }
    }

    cout << "MonFinish()/SpecialModulus(): " << ac2 / ac3 << endl;
    cout << "BarrettModulus()/MonFinish(): " << ac1 / ac2 << endl;
}
*/
/*
void ECCTest()
{
    u8 server_private_key[CAT_SERVER_PRIVATE_KEY_BYTES(256)];
    u8 server_public_key[CAT_SERVER_PUBLIC_KEY_BYTES(256)];

    cout << "OFFLINE PRECOMPUTATION: Generating server private and public keys" << endl;
    double t1 = Clock::usec();
    TwistedEdwardServer::GenerateOfflineStuff(256, server_private_key, server_public_key);
    double t2 = Clock::usec();
    cout << "-- Operation completed in " << (t2 - t1) << " usec" << endl;

    u32 server_private_key_init[] = {0xf51bf87c, 0x1a54bd3f, 0x604f3bee, 0xfa9456a5, 0xfc92b38d, 0xafd5e227, 0xe1c39b5c, 0x72a23ae1 };
    u32 server_public_key_init[] = {0x94731fb8, 0xd1a4ccc1, 0x4e209634, 0x71b6e742, 0x7fb0597e, 0x8339f95a, 0xb8604807, 0x8ac5191d, 0xe151bedc, 0x529bf61a, 0x1abd54ce, 0x8717df10, 0x956fd368, 0x242e1573, 0x3e32bcc7, 0x766027c7, 0x3daac87a, 0x44071c19, 0xd354c8d6, 0x14f8d20b, 0xa21dd55, 0x8c007ae3, 0xd73c7ed1, 0xcdb089a6, 0xa4513555, 0xd6e2c24d, 0x81c6394a, 0x807359a2, 0x325ff9db, 0x2521f08c, 0x9d9ae16, 0xe7752bad };

    memcpy(server_private_key, server_private_key_init, sizeof(server_private_key));
    memcpy(server_public_key, server_public_key_init, sizeof(server_public_key));

    cout << "u32 server_private_key_init[] = {";
    for (int ii = 0; ii < sizeof(server_private_key); ii += sizeof(Leg))
        cout << hex << "0x" << *(Leg*)&server_private_key[ii] << ", ";
    cout << "};" << endl;

    cout << "u32 server_public_key_init[] = {";
    for (int ii = 0; ii < sizeof(server_public_key); ii += sizeof(Leg))
        cout << hex << "0x" << *(Leg*)&server_public_key[ii] << ", ";
    cout << dec << "};" << endl;

    //for (int jj = 0; jj < 10000; ++jj)
    {
        double runtime = 0;
        int bins[100] = { 0 };
        int ii;
        for (ii = 0; ii < 1000; ++ii)
        {
            TwistedEdwardServer server;
            u8 server_shared_secret[TwistedEdwardCommon::MAX_BYTES];

            TwistedEdwardClient client;
            u8 client_shared_secret[TwistedEdwardCommon::MAX_BYTES];
            u8 client_public_key[TwistedEdwardCommon::MAX_BYTES*2];

            //cout << "ON SERVER START-UP: Loading server private key" << endl;
            t1 = Clock::usec();
            if (!server.Initialize(256))
            {
                cout << "FAILURE: Server init" << endl;
                return;
            }
            server.SetPrivateKey(server_private_key);
            t2 = Clock::usec();
            //cout << "-- Operation completed in " << (t2 - t1) << " usec" << endl;

            //cout << "ON CLIENT START-UP: Initializing" << endl;
            t1 = Clock::usec();
            if (!client.Initialize(256))
            {
                cout << "FAILURE: Client init" << endl;
                return;
            }
            t2 = Clock::usec();
            //cout << "-- Operation completed in " << (t2 - t1) << " usec" << endl;

            //cout << "CLIENT: ONLINE NOT TIME-CRITICAL: Computing shared secret" << endl;
            t1 = Clock::usec();
            if (!client.ComputeSharedSecret(server_public_key, client_public_key, client_shared_secret))
            {
                cout << "FAILURE: Client rejected server public key" << endl;
                //cout << "seed was " << ii + jj * 10000 << endl;
                return;
            }
            t2 = Clock::usec();
            //cout << "-- Operation completed in " << (t2 - t1) << " usec" << endl;

            //cout << "SERVER: ONLINE AND TIME-CRITICAL: Computing shared secret" << endl;
            t1 = Clock::usec();
            if (!server.ComputeSharedSecret(client_public_key, server_shared_secret))
            {
                cout << "FAILURE: Server rejected public key" << endl;
                //cout << "seed was " << ii + jj * 10000 << endl;
                return;
            }
            else
            {
                t2 = Clock::usec();
                //cout << "SUCCESS: Server accepted public key" << endl;
            }
            double x = t2 - t1;
            runtime += x;
            int y = (int)x - 100;
            if (y >= 0 && y < 20)
                bins[y]++;
            //cout << "-- Operation completed in " << (t2 - t1) << " usec" << endl;

            if (memcmp(server_shared_secret, client_shared_secret, client.GetSharedSecretBytes()))
            {
                cout << "FAILURE: Server shared secret did not match client shared secret" << endl;
                //cout << "seed was " << ii + jj * 10000 << endl;
                return;
            }
            else
            {
                //cout << "SUCCESS: Server shared secret matched client shared secret" << endl;
            }
        }
        cout << "Average server computation time = " << runtime/(double)ii << " usec" << endl;

        cout << "Histogram of timing with 1 usec bins:" << endl;
        for (int ii = 0; ii < 20; ++ii)
        {
            cout << bins[ii] << " <- " << 100 + ii << " usec" << endl;
        }
    }
}
*/
void HandshakeTest()
{
	BigTwistedEdward *tls_math = KeyAgreementCommon::InstantiateMath(CAT_DEMO_BITS);
	FortunaOutput *tls_csprng = FortunaFactory::ii->Create();

	for (;;)
    {
        // Offline:

        u8 server_private_key[CAT_DEMO_PRIVATE_KEY_BYTES];
        u8 server_public_key[CAT_DEMO_PUBLIC_KEY_BYTES];
        KeyMaker bob_the_key_maker;

        //cout << "Generating server public and private keys..." << endl;
        if (!bob_the_key_maker.GenerateKeyPair(tls_math, tls_csprng, server_public_key, CAT_DEMO_PUBLIC_KEY_BYTES, server_private_key, CAT_DEMO_PRIVATE_KEY_BYTES))
        {
            cout << "FAILURE: Unable to generate key pair" << endl;
            return;
        }

        // Startup:

        //cout << "Starting up..." << endl;
        SecureServerDemo server;
        SecureClientDemo client;

        server.Reset(&client, server_public_key, server_private_key);

        client.Reset(&server, server_public_key);

        // Online:

        //cout << "Client wants to connect." << endl;
        client.SendHello();

        if (client.success)
        {
            //cout << "SUCCESS: Handshake succeeded and we were able to exchange messages securely!" << endl;
        }
        else
        {
            cout << "FAILURE: Handshake failed somehow.  See messages above." << endl;
            break;
        }
    }
}

bool got_iv(u64 correct)
{
    u32 new_iv_low = (u32)(correct & AuthenticatedEncryption::IV_MASK);

    static u64 last_iv = 0;

    u64 recon = AuthenticatedEncryption::ReconstructIV(last_iv, new_iv_low);
    last_iv = recon;
    return recon == correct;
}

void TestIVReconstruction()
{
    for (u64 iv = 0; iv < 0x5000000; iv += 10000)
    {
        if (!got_iv(iv - 17))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv - 19))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv + 3))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv + 3))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv + 2))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv - 3))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv - 1))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
        if (!got_iv(iv))
        {
            cout << "FAILURE: IV reconstruction failed at IV = " << iv << endl;
            return;
        }
    }
    cout << "SUCCESS: IV reconstruction is working properly" << endl;
}
/*
void SHA256OneRun()
{
    static const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

    SHA256 sha;
    sha.Crunch(msg, (u32)strlen(msg));
    const u8 *out = sha.Finish();
}

void TestSHA256()
{
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

    double t1 = Clock::usec();
    SHA256 sha;
    sha.Crunch(msg, (u32)strlen(msg));
    const u8 *out = sha.Finish();
    double t2 = Clock::usec();

    static u32 test_array[8] = {
        0x248d6a61, 0xd20638b8, 0xe5c02693, 0x0c3e6039,
        0xa33ce459, 0x64ff2167, 0xf6ecedd4, 0x19db06c1
    };

    const u32 *out32 = (const u32*)out;

    for (int ii = 0; ii < 8; ++ii)
    {
        if (out32[ii] != getBE(test_array[ii]))
        {
            cout << "FAILURE: SHA-256 output does not match example output" << endl;
            return;
        }
    }

    cout << "SUCCESS: SHA-256 output matches example output. Time: " << (t2 - t1) << " usec" << endl;

    cout << "SHA-256 ran in " << Clock::MeasureClocks(1000, SHA256OneRun) << " clock cycles (median of test data)" << endl;
}

void SHA512OneRun()
{
    static const char *msg = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    SHA512 sha;
    sha.Crunch(msg, (u32)strlen(msg));
    const u8 *out = sha.Finish();
}

void TestSHA512()
{
    const char *msg = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    double t1 = Clock::usec();
    SHA512 sha;
    sha.Crunch(msg, (u32)strlen(msg));
    const u8 *out = sha.Finish();
    double t2 = Clock::usec();

    static u64 test_array[8] = {
        0x8e959b75dae313daLL, 0x8cf4f72814fc143fLL, 0x8f7779c6eb9f7fa1LL, 0x7299aeadb6889018LL,
        0x501d289e4900f7e4LL, 0x331b99dec4b5433aLL, 0xc7d329eeb6dd2654LL, 0x5e96e55b874be909LL
    };

    const u64 *out64 = (const u64*)out;

    for (int ii = 0; ii < 8; ++ii)
    {
        if (out64[ii] != getBE(test_array[ii]))
        {
            cout << "FAILURE: SHA-512 output does not match example output" << endl;
            return;
        }
    }

    cout << "SUCCESS: SHA-512 output matches example output. Time: " << (t2 - t1) << " usec" << endl;

    cout << "SHA-512 ran in " << Clock::MeasureClocks(1000, SHA512OneRun) << " clock cycles (median of test data)" << endl;
}

void TestSHA384()
{
    const char *msg = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    double t1 = Clock::usec();
    SHA512 sha(384);
    sha.Crunch(msg, (u32)strlen(msg));
    const u8 *out = sha.Finish();
    double t2 = Clock::usec();

    static u64 test_array[6] = {
        0x09330c33f71147e8LL, 0x3d192fc782cd1b47LL, 0x53111b173b3b05d2LL, 0x2fa08086e3b0f712LL,
        0xfcc7c71a557e2db9LL, 0x66c3e9fa91746039LL
    };

    const u64 *out64 = (const u64*)out;

    for (int ii = 0; ii < 6; ++ii)
    {
        if (out64[ii] != getBE(test_array[ii]))
        {
            cout << "FAILURE: SHA-384 output does not match example output" << endl;
            return;
        }
    }

    cout << "SUCCESS: SHA-384 output matches example output. Time: " << (t2 - t1) << " usec" << endl;
}
*/
void Skein256OneRun()
{
    static const u8 key[] = { 0x06 };
    static const u8 msg[] = { 0xcc };
    const int len = 1;

    u8 out[32];

    Skein hash;
    hash.BeginKey(256);
    hash.Crunch(key, 1);
    hash.End();
    hash.BeginMAC();
    hash.Crunch(msg, len);
    hash.End();
    hash.Generate(out, sizeof(out));
}

void TestSkein256()
{
    static const char *key = "My voice is my passport.  Authenticate me.";
    static const char *msg = "Too many secrets.";

    double t1 = Clock::usec();
    Skein hash;
    hash.BeginKey(256);
    hash.CrunchString(key);
    hash.End();
    hash.BeginMAC();
    hash.CrunchString(msg);
    hash.End();

    u64 out[8];
    hash.Generate(out, sizeof(out));
    double t2 = Clock::usec();

    static u64 test_array[sizeof(out)/sizeof(u64)] = {
        0x8ea14aee067ca142LL, 0x338ac1b352251261LL, 0x7dea57cfc6dfc250LL, 0x7cdaf009047c1ba0LL,
        0x970e5db911b0159cLL, 0xdcc97035fee1be22LL, 0xd76fd0e9198e8c61LL, 0x7e9062f06e46564fLL
    };

    for (int ii = 0; ii < sizeof(test_array)/sizeof(u64); ++ii)
    {
        if (out[ii] != getLE(test_array[ii]))
        {
            cout << "FAILURE: Skein-256 output does not match example output" << endl;
            return;
        }
    }

    cout << "SUCCESS: Skein-256 output matches example output. Time: " << (t2 - t1) << " usec" << endl;

    cout << "Skein-256 ran in " << Clock::MeasureClocks(1000, Skein256OneRun) << " clock cycles (median of test data)" << endl;
}

void Skein512OneRun()
{
    static const u8 key[] = { 0x06 };
    static const u8 msg[] = { 0xcc };
    const int len = 1;

    u8 out[32];

    Skein hash;
    hash.BeginKey(512);
    hash.Crunch(key, 1);
    hash.End();
    hash.BeginMAC();
    hash.Crunch(msg, len);
    hash.End();
    hash.Generate(out, sizeof(out));
}

void TestSkein512()
{
    static const char *key = "My voice is my passport.  Authenticate me.";
    static const char *msg = "Too many secrets.";

    double t1 = Clock::usec();
    Skein hash;
    hash.BeginKey(512);
    hash.CrunchString(key);
    hash.End();
    hash.BeginMAC();
    hash.CrunchString(msg);
    hash.End();

    u64 out[16];
    hash.Generate(out, sizeof(out));
    double t2 = Clock::usec();

    static u64 test_array[sizeof(out)/sizeof(u64)] = {
        0xc4698ec13779acefLL, 0x3af40635857457d6LL, 0xb636346dc4cca13bLL, 0x75f22f61f78c2297LL,
        0x1187202cc2c5050aLL, 0x15c9007602ad0e5bLL, 0x56477ef18a3a5d83LL, 0x120a78bc06db754aLL,
        0xdd18db6b142e5253LL, 0xf9cab38ccb33b32cLL, 0x736af3f7549790a5LL, 0x75f8e5a3c86aa564LL,
        0x1ec048271ebb6148LL, 0x2e5d0fb3b251f87fLL, 0x66c2bf4fa7908eeeLL, 0x6ff3e167f54bb92dLL
    };

    for (int ii = 0; ii < sizeof(test_array)/sizeof(u64); ++ii)
    {
        if (out[ii] != getLE(test_array[ii]))
        {
            cout << "FAILURE: Skein-512 output does not match example output" << endl;
            return;
        }
    }

    cout << "SUCCESS: Skein-512 output matches example output. Time: " << (t2 - t1) << " usec" << endl;

    cout << "Skein-512 ran in " << Clock::MeasureClocks(1000, Skein512OneRun) << " clock cycles (median of test data)" << endl;
}

static int cc_bytes;
static ChaCha cc_test;

void ChaChaOnce()
{
    char in[1500], out[1500];

    cc_test.Begin(0x0123456701234567LL);
    cc_test.Crypt(in, out, cc_bytes);
}

void TestChaCha()
{
    cout << "ChaCha timing results:" << endl;

    const char *key = "what is the key?";

    cc_test.Key(key, (int)strlen(key));

    static const int TIMING_BYTES[] = {
        16, 64, 128, 256, 512, 1024, 1500
    };

    for (int ii = 0; ii < 7; ++ii)
    {
        cc_bytes = TIMING_BYTES[ii];
        cout << cc_bytes << " bytes: " << Clock::MeasureClocks(1000, ChaChaOnce)/(float)cc_bytes << " cycles/byte" << endl;
    }
}
