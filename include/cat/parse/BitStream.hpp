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
// 10/08/08 fix shrink() scheduling
// 10/03/08 major rewrite

// TODO: Big-endian code is untested
// TODO: Not suitable for storing large buffers > 500 MB in size

#ifndef BIT_STREAM_HPP
#define BIT_STREAM_HPP

#include <cat/Platform.hpp>

namespace cat {


// Define how to swap bytes for integer data
// swapLE = store/read data in Little-Endian
// swapBE = store/read data in Big-Endian
#define SWAP_BS(n) swapLE(n)


//// BitStream manipulators ////

namespace bit
{
    template<u32 N_BITS> struct set
    {
        u32 bits;
        set(u32 c_bits) : bits(c_bits) { }
    };

    template<u32 N_BITS> struct get
    {
        u32 &ref;
        get(u32 &c_ref) : ref(c_ref) { }
    };
}

namespace byte
{
    struct set
    {
        u32 bytes;
        const void *ref;

        set(u32 b, const void *r) : ref(r), bytes(b) { }
    };

    struct get
    {
        u32 bytes;
        void *ref;

        get(u32 b, void *r) : ref(r), bytes(b) { }
    };
}


//// BitStream ////

class BitStream
{
    bool fixed_buffer;
    u8 *buffer;
    u32 buffer_bytes;
    bool read_underrun;

    // grow to be able to write a number of bits
    void grow(u32 bits);

public:
    u32 read_offset, write_offset; // in bits

public:
    BitStream(u32 bytes = 0, void *vbuffer = 0);
    ~BitStream();

    // free unused buffer space
    void shrink();

public:
    u8 *get() { return buffer; }

    bool aligned() { return read_offset % 8 == 0; }
    u8 *peek() { return buffer + read_offset / 8; }

public:
    // returns true iff the buffer is valid
    bool valid() { return buffer != 0; }

    // returns count of remaining readable bits
    int unread() { return write_offset - read_offset; }

    // returns true if a recent read operation would have overrun the buffer
    bool underrun();

    // skip ahead a number of bits
    void skip(u32 bits);

public:
    // insertion
    void write1(u8 data); // data MUST be 1 or 0

    template<class T> void write(T data)
    {
        grow(sizeof(T) * 8);

        u32 byte_offset = write_offset / 8;
        u32 shift = write_offset % 8;

        if (shift)
        {
            buffer[byte_offset] |= (u8)(data << shift);
            data = data >> (8 - shift);
            ++byte_offset;
        }

        *(T*)(buffer + byte_offset) = data;

        write_offset += sizeof(T) * 8;
    }
    template<> void write(s8 data) { write((u8)data); }
    template<> void write(s16 data) { write((u16)data); }
    template<> void write(s32 data) { write((u32)data); }
    template<> void write(s64 data) { write((u64)data); }
    template<> void write(float data) { write(*(u32*)&data); }
    template<> void write(double data) { write(*(u64*)&data); }

    void writeBits(u32 data, int count);
    void writeBytes(const void *data, u32 byte_count);

public:
    // stream-mode insertion
    BitStream &operator<<(const char *data) { writeBytes(data, (u32)strlen(data)); return *this; }

    template<class T> BitStream &operator<<(T data) { write(data); return *this; }

    template<u32 N_BITS> BitStream &operator<<(const bit::set<N_BITS> &n) { writeBits(n.bits, N_BITS); return *this; }
    template<> BitStream &operator<<(const bit::set<1> &n) { write1((u8)n.bits); return *this; }
    template<> BitStream &operator<<(const bit::set<8> &n) { write((u8)n.bits); return *this; }
    template<> BitStream &operator<<(const bit::set<16> &n) { write((u16)n.bits); return *this; }
    template<> BitStream &operator<<(const bit::set<32> &n) { write((u32)n.bits); return *this; }

    BitStream &operator<<(const byte::set &n) { writeBytes(n.ref, n.bytes); return *this; }

public:
    // extraction
    u8 read1();

    template<class T> void read(T &data)
    {
        const u32 bits = sizeof(T) * 8;

        if (read_offset + bits > write_offset)
        {
            read_underrun = true;
            return;
        }

        u32 byte_offset = read_offset / 8;
        u32 shift = read_offset % 8;

        if (shift)
            data = (*(T*)(buffer + byte_offset + 1) << (8 - shift)) | (buffer[byte_offset] >> shift);
        else
            data = *(T*)(buffer + byte_offset);

        read_offset += bits;
    }
    template<> void read(float &data) { read((u32&)data); }
    template<> void read(double &data) { write(*(u64*)&data); }
    template<class T> T read() { T temp; read(temp); return temp; }

    u32 readBits(u32 count);
    void readBytes(void *data, u32 byte_count);

public:
    // stream-mode extraction
    template<class T> BitStream &operator>>(T &data) { read(data); return *this; }

    template<u32 N_BITS> BitStream &operator>>(const bit::get<N_BITS> &n) { n.ref = readBits(N_BITS); return *this; }
    template<> BitStream &operator>>(const bit::get<1> &n) { n.ref = read1(); return *this; }
    template<> BitStream &operator>>(const bit::get<8> &n) { n.ref = read<u8>(); return *this; }
    template<> BitStream &operator>>(const bit::get<16> &n) { n.ref = read<u16>(); return *this; }
    template<> BitStream &operator>>(const bit::get<32> &n) { read(n.ref); return *this; }

    BitStream &operator>>(const byte::get &n) { readBytes(n.ref, n.bytes); return *this; }
};


} // namespace cat

#endif // BIT_STREAM_HPP

