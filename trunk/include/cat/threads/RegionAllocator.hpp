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

#ifndef REGION_ALLOCATOR_HPP
#define REGION_ALLOCATOR_HPP

#include <cat/Singleton.hpp>
#include <memory>
#include <xstring>
#include <sstream>

namespace cat {


// A region-based allocator that is lock-free, supporting
// a range of allocation block sizes that are pre-allocated
// in a pre-determined way, tuned to the application.
class RegionAllocator : public Singleton<RegionAllocator>
{
    CAT_SINGLETON(RegionAllocator);
    ~RegionAllocator();

protected:
    struct RegionInfoHead
    {
        u32 next_bitmap_entry;
    };

    struct RegionInfo : public RegionInfoHead
    {
        volatile u32 bitmap[1];
    };

    // 64, 128, 256, 512, 1024, 2048 only
    static const u32 REGION_COUNT = 6;
    static const u32 BLOCK_SIZE[REGION_COUNT];

    u32 bytes_overall;
    u32 blocks_per_region[REGION_COUNT];
    u32 bitmap_dwords[REGION_COUNT];
    u8 *regions[REGION_COUNT];
    RegionInfo *region_info[REGION_COUNT];

    //u32 errors;

public:
    bool Valid();

    void Shutdown();

public:
    void *Acquire(u32 bytes);
    void *Resize(void *ptr, u32 bytes);
    void Release(void *ptr);

    template<class T>
    CAT_INLINE void Delete(T *ptr)
    {
        ptr->~T();
        Release(ptr);
    }
};

// Use STLRegionAllocator in place of the standard STL allocator
// to make use of the RegionAllocator in STL types.  Some common
// usage typedefs follow this class definition below.
template<typename T>
class STLRegionAllocator
{
public:
    typedef size_t size_type;
    typedef size_t difference_type;
    typedef T *pointer;
    typedef const T *const_pointer;
    typedef T &reference;
    typedef const T &const_reference;
    typedef T value_type;

    template<typename S>
    struct rebind
    {
        typedef STLRegionAllocator<S> other;
    };

    pointer address(reference X) const
    {
        return &X;
    }

    const_pointer address(const_reference X) const
    {
        return &X;
    }

    STLRegionAllocator()
    {
    }

private:
    template<typename S>
    STLRegionAllocator(const STLRegionAllocator<S> &cp)
    {
        // Not allowed
    }

    template<typename S>
    STLRegionAllocator<T> &operator=(const STLRegionAllocator<S> &cp)
    {
        // Not allowed
        return *this;
    }

public:
    pointer allocate(size_type Count, const void *Hint = 0)
    {
        return (pointer)RegionAllocator::ii->Acquire((u32)Count * sizeof(T));
    }

    void deallocate(pointer Ptr, size_type Count)
    {
        RegionAllocator::ii->Release(Ptr);
    }

    void construct(pointer Ptr, const T &Val)
    {
        std::_Construct(Ptr, Val);
    }

    void destroy(pointer Ptr)
    {
        std::_Destroy(Ptr);
    }

    size_type max_size() const
    {
        return 0x00FFFFFF;
    }
};


// Common usage typedefs for using RegionAllocator as the STL allocator
typedef std::basic_ostringstream<char, std::char_traits<char>, STLRegionAllocator<char> > region_ostringstream;
typedef std::basic_string<char, std::char_traits<char>, STLRegionAllocator<char> > region_string;


} // namespace cat

// Provide placement new constructor and delete pair to allow for
// an easy syntax to create objects from the RegionAllocator:
//   T *a = new (RegionAllocator::ii) T();
// The object can be freed with:
//   RegionAllocator::ii->Delete(a);
// Which insures that the destructor is called before freeing memory
inline void *operator new(size_t bytes, cat::RegionAllocator *allocator)
{
    return allocator->Acquire((cat::u32)bytes);
}

inline void operator delete(void *ptr, cat::RegionAllocator *allocator)
{
    allocator->Release(ptr);
}

#endif // REGION_ALLOCATOR_HPP
