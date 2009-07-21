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

#include <cat/threads/RegionAllocator.hpp>
#include <cat/threads/Atomic.hpp>
#include <cat/math/BitMath.hpp>
#include <cat/io/Settings.hpp>
#include <cat/io/Logging.hpp>
using namespace cat;

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#endif


const u32 RegionAllocator::BLOCK_SIZE[REGION_COUNT] = {
    64,
    128,
    256,
    512,
    1024,
    2048
};


RegionAllocator::RegionAllocator()
{
    regions[0] = 0;

    // Set the block counts for each region (~12 MB by default)
    blocks_per_region[0] = 8192; // 64 byte blocks
    blocks_per_region[1] = 4096; // 128 byte blocks
    blocks_per_region[2] = 2048; // 256 byte blocks
    blocks_per_region[3] = 1024; // 512 byte blocks
    blocks_per_region[4] = 1024; // 1024 byte blocks
    blocks_per_region[5] = 4096; // 2048 byte blocks

    // Calculate space required
    u32 info_bytes[REGION_COUNT];

    bytes_overall = 0;
    for (u32 ii = 0; ii < REGION_COUNT; ++ii)
    {
        bitmap_dwords[ii] = (blocks_per_region[ii] + 31) / 32;
        info_bytes[ii] = sizeof(RegionInfoHead) + bitmap_dwords[ii] * 4;
        bytes_overall += info_bytes[ii];
        bytes_overall += blocks_per_region[ii] * BLOCK_SIZE[ii];
    }

    // Pre-allocate all the memory required
#if defined(CAT_OS_WINDOWS)
    u8 *base = (u8*)VirtualAlloc(0, bytes_overall, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#endif
    if (!base) return;

    // Set the region pointers
    for (u32 ii = 0; ii < REGION_COUNT; ++ii)
    {
        regions[ii] = base;
        base += blocks_per_region[ii] * BLOCK_SIZE[ii];
    }

    // Set the region info pointers
    for (u32 ii = 0; ii < REGION_COUNT; ++ii)
    {
        region_info[ii] = (RegionInfo *)base;
        base += info_bytes[ii];
    }

    // Zero region info memory (just in case) since I noticed that
    // sometimes the allocated pages are not zeroed as the MSDN
    // documentation had led me to believe.
    memset(region_info[0], 0, base - (u8*)region_info[0]);

    //errors = 0;
}

RegionAllocator::~RegionAllocator()
{
    Shutdown();
}

void RegionAllocator::Shutdown()
{
    if (regions[0])
    {
#if defined(CAT_OS_WINDOWS)
        VirtualFree(regions[0], 0, MEM_RELEASE);
#endif
        regions[0] = 0;
    }
}

bool RegionAllocator::Valid()
{
    return regions[0] != 0;
}


void *RegionAllocator::Acquire(u32 bytes)
{
    // Determine smallest region from byte count
    u32 region = (bytes - 1) >> 5;
    if (region) region = BitMath::BSR32(region);

    // Scan for a region that might be free
    for (; region < REGION_COUNT; ++region)
    {
        RegionInfo *info = region_info[region];

        // Loop through all bitmask dwords
        // Start from the last dword bitmask that was used
        u32 bitmask_start = info->next_bitmap_entry;
        u32 bitmask_index = bitmask_start;

        do {
            // Loop through all the unset bits in this bitmask dword
            // Until the bitmask has no unset bits
            volatile u32 *bmptr = &info->bitmap[bitmask_index];
            u32 bitmask = *bmptr;

            while (bitmask != 0xffffffff)
            {
                u32 freebit = BitMath::BSF32(~bitmask);

                if (!Atomic::BTS(bmptr, freebit))
                {
                    // Won the race to claim this block
                    // Set the next bitmap entry
                    info->next_bitmap_entry = bitmask_index;
                    return regions[region] + (bitmask_index * 32 + freebit) * BLOCK_SIZE[region];
                }

                // We lost the race, loop around and try again
                bitmask = *bmptr;
            }

            // Next bitmask dword
            ++bitmask_index;
            if (bitmask_index >= bitmap_dwords[region])
                bitmask_index = 0;

        } while (bitmask_index != bitmask_start);
    }

    // Fall back to malloc if the request is too large.
    return malloc(bytes);
}

void RegionAllocator::Release(void *ptr)
{
    if (!ptr) return;

    if (ptr >= region_info[0])
    {
        free(ptr);
        return;
    }

    for (int ii = REGION_COUNT-1; ii >= 0; --ii)
    {
        if (ptr >= regions[ii])
        {
            // Find block index
            size_t offset = (u8*)ptr - regions[ii];
            u32 block = (u32)(offset / BLOCK_SIZE[ii]);
            RegionInfo *info = region_info[ii];

            // Reset bit for that block (deallocate)
            Atomic::BTR(&info->bitmap[block / 32], block % 32);
            return;
        }
    }

    free(ptr);
}

void *RegionAllocator::Resize(void *ptr, u32 bytes)
{
    if (!ptr)
        return Acquire(bytes);

    if (ptr >= region_info[0])
        return realloc(ptr, bytes);

    for (int ii = REGION_COUNT-1; ii >= 0; --ii)
    {
        if (ptr >= regions[ii])
        {
            // Check if we don't need to allocate anything larger
            if (BLOCK_SIZE[ii] >= bytes)
                return ptr;

            // Allocate larger storage
            void *new_region = Acquire(bytes);

            // Copy the old data over
            if (new_region)
                memcpy(new_region, ptr, BLOCK_SIZE[ii]);

            // Find block index
            size_t offset = (u8*)ptr - regions[ii];
            u32 block = (u32)(offset / BLOCK_SIZE[ii]);
            RegionInfo *info = region_info[ii];

            // Reset bit for that block (deallocate)
            Atomic::BTR(&info->bitmap[block / 32], block % 32);
            return new_region;
        }
    }

    return realloc(ptr, bytes);
}
