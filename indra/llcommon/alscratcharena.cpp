/**
 * @file alscratcharena.cpp
 * @brief Each thread's scratch memory, given back in the reverse of the
 *        order it was taken
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alscratcharena.h"

#include "llmemory.h"

#include <new>

namespace
{
    // Each arena block sits after a header of this size holding the block's
    // size, by which its return is checked to be the last one taken.
    constexpr size_t HEADER = ALScratchArena::ALIGNMENT;

    struct Arena
    {
        U8* mBase = nullptr;
        size_t mTop = 0;
        // The arena could not be made: every block comes from the heap.
        bool mUnavailable = false;
        ALScratchArena::Counters mCounters;

        ~Arena()
        {
            ll_aligned_free<64>(mBase);
        }
    };

    thread_local Arena tArena;
} // namespace

//static
void* ALScratchArena::allocate(size_t bytes)
{
    Arena& arena = tArena;
    if (!arena.mBase && !arena.mUnavailable)
    {
        arena.mBase = static_cast<U8*>(ll_aligned_malloc<64>(CAPACITY));
        arena.mUnavailable = !arena.mBase;
    }

    // Compared before it is rounded, so that a size near the top of size_t,
    // as meshoptimizer asks for when a count overflows, goes to the heap.
    const size_t room = CAPACITY - arena.mTop;
    if (arena.mBase && room > HEADER && bytes <= room - HEADER)
    {
        const size_t size = (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
        if (size <= room - HEADER)
        {
            U8* block = arena.mBase + arena.mTop + HEADER;
            *reinterpret_cast<size_t*>(block - HEADER) = size;
            arena.mTop += HEADER + size;
            ++arena.mCounters.arena_blocks;
            arena.mCounters.peak = llmax(arena.mCounters.peak, arena.mTop);
            return block;
        }
    }

    ++arena.mCounters.heap_blocks;
    arena.mCounters.heap_bytes += bytes;
    return ::operator new(bytes);
}

//static
void ALScratchArena::deallocate(void* block)
{
    Arena& arena = tArena;
    U8* p = static_cast<U8*>(block);
    if (arena.mBase && p >= arena.mBase && p < arena.mBase + CAPACITY)
    {
        llassert(p + *reinterpret_cast<const size_t*>(p - HEADER) == arena.mBase + arena.mTop);
        arena.mTop = static_cast<size_t>(p - HEADER - arena.mBase);
        return;
    }
    ::operator delete(block);
}

//static
ALScratchArena::Counters ALScratchArena::counters()
{
    return tArena.mCounters;
}

//static
void ALScratchArena::resetCounters()
{
    Arena& arena = tArena;
    arena.mCounters = Counters();
    arena.mCounters.peak = arena.mTop;
}

ALScratchArena::Scope::~Scope()
{
    while (mCount > 0)
    {
        ALScratchArena::deallocate(mBlocks[--mCount]);
    }
}

void* ALScratchArena::Scope::take(size_t bytes)
{
    llassert_always(mCount < MAX_BLOCKS);
    void* block = ALScratchArena::allocate(bytes);
    mBlocks[mCount++] = block;
    return block;
}
