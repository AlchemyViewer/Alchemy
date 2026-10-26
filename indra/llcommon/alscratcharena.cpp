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

    // The thread's arena. Nothing is done to destroy it, so that it lasts
    // as long as its thread, past every thread_local's destructor;
    // ArenaRelease ends it.
    struct Arena
    {
        U8* mBase = nullptr;
        size_t mTop = 0;
        // The arena could not be made, or its thread has ended: every block
        // comes from the heap.
        bool mUnavailable = false;
        ALScratchArena::Counters mCounters;
    };

    thread_local Arena tArena;

    // Frees an arena its thread has ended once no block is out of it.
    void freeIfEnded(Arena& arena)
    {
        if (arena.mUnavailable && arena.mTop == 0)
        {
            ll_aligned_free<64>(arena.mBase);
            arena.mBase = nullptr;
        }
    }

    // Ends its thread's arena as the thread ends. It is made with the arena,
    // so a thread_local made before is destroyed after it: what that takes
    // then comes from the heap, and a block it holds goes back into the
    // arena, which is freed once the last is back.
    struct ArenaRelease
    {
        Arena* mArena = nullptr;

        ~ArenaRelease()
        {
            if (mArena)
            {
                mArena->mUnavailable = true;
                freeIfEnded(*mArena);
            }
        }
    };

    thread_local ArenaRelease tArenaRelease;
} // namespace

//static
void* ALScratchArena::allocate(size_t bytes)
{
    Arena& arena = tArena;
    if (!arena.mBase && !arena.mUnavailable)
    {
        arena.mBase = static_cast<U8*>(ll_aligned_malloc<64>(CAPACITY));
        arena.mUnavailable = !arena.mBase;
        tArenaRelease.mArena = &arena;
    }

    // Compared before it is rounded, so that a size near the top of size_t,
    // as meshoptimizer asks for when a count overflows, goes to the heap.
    const size_t room = CAPACITY - arena.mTop;
    if (!arena.mUnavailable && room > HEADER && bytes <= room - HEADER)
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
        // Only the block on top moves the top down. Moving it down to any
        // other would hand the blocks above, still in use, to the next
        // taker, so the top stays, and the block's room is lost to the
        // thread.
        const size_t start = static_cast<size_t>(p - arena.mBase);
        if (start >= HEADER && start <= arena.mTop && *reinterpret_cast<const size_t*>(p - HEADER) == arena.mTop - start)
        {
            arena.mTop = start - HEADER;
            freeIfEnded(arena);
        }
        else
        {
            LL_WARNS_ONCE("ALScratchArena") << "A block was given back out of order or twice" << LL_ENDL;
        }
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
