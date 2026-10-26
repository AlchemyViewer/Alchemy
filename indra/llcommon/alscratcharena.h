/**
 * @file alscratcharena.h
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

#ifndef AL_SCRATCHARENA_H
#define AL_SCRATCHARENA_H

#include "llpreprocessor.h"
#include "stdtypes.h"

#include <cstddef>

// Scratch memory for work that gives its blocks back last first, as
// meshoptimizer does. Each thread has its own arena of CAPACITY bytes,
// made the first time the thread takes a block from it: a block is taken
// from the top, and giving it back moves the top down to it. A block that
// does not fit what is left comes from the process heap through operator
// new, and goes back there. A thread touches only its own arena, so
// nothing is locked.
//
// The order is the caller's to keep: a block goes back before every block
// taken after it has. Nothing holding a block may yield to a coroutine
// that takes from the same thread's arena. A block given back out of that
// order leaves the top where it is, so that no block still in use is
// handed out again: its room is lost to the thread, and the first such
// return is logged. A block given back twice is passed over the same way,
// unless it is the block on top.
//
// As its thread ends the arena takes no more blocks: one taken after, as
// by a thread_local destroyed after it, comes from the heap. The arena is
// freed once every block taken from it is back, so that a block a
// thread_local holds stays its holder's until given back. An arena that
// has lost room to a block given back out of order is never freed.
class LL_COMMON_API ALScratchArena
{
public:
    // Bytes in each thread's arena. LLVolumeFace::cacheOptimize, the
    // largest user, peaks at about 35 bytes an index and 36 a vertex: 2 MB
    // for a mesh LOD face of 8,385 vertices and 16,128 triangles. Twice
    // that leaves faces up to about 32,000 triangles off the heap; larger
    // ones take their largest blocks from it. Only the pages a thread has
    // used are touched.
    static constexpr size_t CAPACITY = 4 * 1024 * 1024;

    // Every block starts on a multiple of this, as operator new's do.
    static constexpr size_t ALIGNMENT = 16;

    // A block of at least bytes from the calling thread's arena, or from
    // the heap where the arena has no room for it.
    static void* allocate(size_t bytes);

    // Gives back a block allocate took on this thread.
    static void deallocate(void* block);

    // What the calling thread's arena has served since the last reset.
    struct Counters
    {
        size_t arena_blocks = 0; // blocks taken from the arena
        size_t heap_blocks = 0;  // blocks taken from the heap
        size_t heap_bytes = 0;   // bytes those asked for
        size_t peak = 0;         // the most of the arena in use at once
    };

    static Counters counters();
    static void resetCounters();

    // Blocks taken for a scope, given back last first as it ends.
    class Scope
    {
    public:
        Scope() = default;
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        // Room for count values of T, not initialised.
        template <typename T>
        T* allocate(size_t count)
        {
            return static_cast<T*>(take(count * sizeof(T)));
        }

    private:
        void* take(size_t bytes);

        static constexpr size_t MAX_BLOCKS = 16;
        void* mBlocks[MAX_BLOCKS];
        size_t mCount = 0;
    };
};

#endif // AL_SCRATCHARENA_H
