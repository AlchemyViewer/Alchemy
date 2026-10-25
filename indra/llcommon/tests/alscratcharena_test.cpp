/**
 * @file alscratcharena_test.cpp
 * @brief A thread's scratch arena: blocks last on top, given back last
 *        first, and the heap for what does not fit.
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
#include "altestsanitizer.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <thread>

namespace
{
    std::uintptr_t address(const void* p)
    {
        return reinterpret_cast<std::uintptr_t>(p);
    }

    TEST_CASE("alscratcharena: a block given back is where the next one is taken", "[alscratcharena]")
    {
        ALScratchArena::resetCounters();
        void* first = ALScratchArena::allocate(100);
        ALScratchArena::deallocate(first);
        void* second = ALScratchArena::allocate(300);
        CHECK(second == first);
        ALScratchArena::deallocate(second);

        const ALScratchArena::Counters counters = ALScratchArena::counters();
        CHECK(counters.arena_blocks == 2);
        CHECK(counters.heap_blocks == 0);
    }

    TEST_CASE("alscratcharena: blocks are aligned and each lies above the one before", "[alscratcharena]")
    {
        void* a = ALScratchArena::allocate(1);
        void* b = ALScratchArena::allocate(17);
        void* c = ALScratchArena::allocate(0);
        void* d = ALScratchArena::allocate(48);
        for (void* p : { a, b, c, d })
        {
            CHECK(address(p) % ALScratchArena::ALIGNMENT == 0);
        }
        CHECK(address(b) >= address(a) + 1);
        CHECK(address(c) >= address(b) + 17);
        CHECK(address(d) > address(c));
        ALScratchArena::deallocate(d);
        ALScratchArena::deallocate(c);
        ALScratchArena::deallocate(b);
        ALScratchArena::deallocate(a);
        void* again = ALScratchArena::allocate(1);
        CHECK(again == a);
        ALScratchArena::deallocate(again);
    }

    // A block bigger than what is left comes from the heap, and a smaller
    // one taken after it still comes from the arena; both go back to where
    // they came from in either order of kind.
    TEST_CASE("alscratcharena: a block the arena has no room for comes from the heap", "[alscratcharena]")
    {
        ALScratchArena::resetCounters();
        void* base = ALScratchArena::allocate(16);
        ALScratchArena::deallocate(base);

        void* most = ALScratchArena::allocate(ALScratchArena::CAPACITY - 4096);
        void* over = ALScratchArena::allocate(8192);
        void* small = ALScratchArena::allocate(1024);
        // The first block follows a header of one ALIGNMENT at the arena's
        // start. The heap may place a block right after the arena's end.
        const std::uintptr_t lo = address(base) - ALScratchArena::ALIGNMENT;
        const std::uintptr_t hi = lo + ALScratchArena::CAPACITY;
        CHECK((address(most) >= lo && address(most) < hi));
        CHECK(!(address(over) >= lo && address(over) < hi));
        CHECK((address(small) > address(most) && address(small) < hi));
        // The heap's block is written over in full, as a user writes it.
        std::memset(over, 0xab, 8192);
        ALScratchArena::deallocate(small);
        ALScratchArena::deallocate(over);
        ALScratchArena::deallocate(most);

        void* whole = ALScratchArena::allocate(ALScratchArena::CAPACITY);
        CHECK(!(address(whole) >= lo && address(whole) < hi));
        ALScratchArena::deallocate(whole);

        const ALScratchArena::Counters counters = ALScratchArena::counters();
        CHECK(counters.arena_blocks == 3);
        CHECK(counters.heap_blocks == 2);
        CHECK(counters.heap_bytes == 8192 + ALScratchArena::CAPACITY);
        CHECK(counters.peak <= ALScratchArena::CAPACITY);
        CHECK(counters.peak > ALScratchArena::CAPACITY - 4096);

        void* after = ALScratchArena::allocate(16);
        CHECK(after == base);
        ALScratchArena::deallocate(after);
    }

    // meshoptimizer asks for the most a size_t holds where a count times
    // its size would overflow, so that the heap refuses it.
    TEST_CASE("alscratcharena: a size no arena could hold is the heap's to refuse", "[alscratcharena]")
    {
        if constexpr (ll_test::SANITIZER_ALLOCATOR)
        {
            SKIP("a sanitizer's operator new ends the process rather than throw");
        }
        CHECK_THROWS_AS(ALScratchArena::allocate(size_t(-1)), std::bad_alloc);
        void* p = ALScratchArena::allocate(32);
        void* q = ALScratchArena::allocate(32);
        CHECK(address(q) > address(p));
        ALScratchArena::deallocate(q);
        ALScratchArena::deallocate(p);
    }

    TEST_CASE("alscratcharena: a scope gives its blocks back as it ends", "[alscratcharena]")
    {
        void* base = ALScratchArena::allocate(16);
        ALScratchArena::deallocate(base);
        {
            ALScratchArena::Scope outer;
            U32* a = outer.allocate<U32>(1000);
            {
                ALScratchArena::Scope inner;
                F32* b = inner.allocate<F32>(4000);
                CHECK(address(b) > address(a));
            }
            F64* c = outer.allocate<F64>(10);
            CHECK(address(c) > address(a));
        }
        void* after = ALScratchArena::allocate(16);
        CHECK(after == base);
        ALScratchArena::deallocate(after);
    }

    // A block given back while one taken after it is still out leaves the
    // top where it is: the next block is taken above the one still out, not
    // over it. The thread is the case's own. Given back again once it is on
    // top, the first block brings the top back to the start, so that the
    // arena is freed as the thread ends.
    TEST_CASE("alscratcharena: a block given back out of order leaves the blocks above it to their holders", "[alscratcharena]")
    {
        void* below = nullptr;
        void* above = nullptr;
        void* next = nullptr;
        void* again = nullptr;
        std::thread([&]
                    {
                        below = ALScratchArena::allocate(64);
                        above = ALScratchArena::allocate(64);
                        ALScratchArena::deallocate(below);
                        next = ALScratchArena::allocate(64);
                        ALScratchArena::deallocate(next);
                        ALScratchArena::deallocate(above);
                        ALScratchArena::deallocate(below);
                        again = ALScratchArena::allocate(64);
                        ALScratchArena::deallocate(again);
                    })
            .join();
        CHECK(address(above) > address(below));
        CHECK(address(next) >= address(above) + 64);
        CHECK(again == below);
    }

    // A thread_local made before its thread first takes a block, holding
    // one as the thread ends, is destroyed after the arena is ended. The
    // block is still its own, to write and to give back into the arena;
    // what it takes after comes from the heap.
    struct LateHolder
    {
        void* block = nullptr;
        // What the arena served the destructor, once it has finished.
        std::optional<ALScratchArena::Counters>* served = nullptr;

        ~LateHolder()
        {
            if (served)
            {
                std::memset(block, 0xab, 64);
                ALScratchArena::deallocate(block);
                ALScratchArena::resetCounters();
                ALScratchArena::deallocate(ALScratchArena::allocate(64));
                *served = ALScratchArena::counters();
            }
        }
    };
    thread_local LateHolder tLateHolder;

    TEST_CASE("alscratcharena: a block still out as its thread ends goes back into the arena", "[alscratcharena]")
    {
        std::optional<ALScratchArena::Counters> late;
        std::thread([&]
                    {
                        tLateHolder.served = &late;
                        tLateHolder.block = ALScratchArena::allocate(64);
                    })
            .join();
        REQUIRE(late.has_value());
        CHECK(late->arena_blocks == 0);
        CHECK(late->heap_blocks == 1);
    }

    // A thread_local made before its thread first takes a block is
    // destroyed after the thread's arena is freed, and what it takes then
    // comes from the heap. A block taken from the freed arena would be
    // written into memory already given back, and the destructor would not
    // get as far as saying what it was served.
    struct LateTaker
    {
        // What the arena served the destructor, once it has finished.
        std::optional<ALScratchArena::Counters>* served = nullptr;

        ~LateTaker()
        {
            if (served)
            {
                ALScratchArena::resetCounters();
                ALScratchArena::deallocate(ALScratchArena::allocate(64));
                *served = ALScratchArena::counters();
            }
        }
    };
    thread_local LateTaker tLateTaker;

    TEST_CASE("alscratcharena: a block taken after the thread's arena is freed comes from the heap", "[alscratcharena]")
    {
        std::optional<ALScratchArena::Counters> late;
        std::thread([&]
                    {
                        tLateTaker.served = &late;
                        ALScratchArena::deallocate(ALScratchArena::allocate(64));
                    })
            .join();
        REQUIRE(late.has_value());
        CHECK(late->arena_blocks == 0);
        CHECK(late->heap_blocks == 1);
    }

    TEST_CASE("alscratcharena: each thread takes from an arena of its own", "[alscratcharena]")
    {
        void* mine = ALScratchArena::allocate(64);
        ALScratchArena::resetCounters();
        void* theirs = nullptr;
        ALScratchArena::Counters their_counters;
        std::thread([&]
                    {
                        ALScratchArena::resetCounters();
                        theirs = ALScratchArena::allocate(64);
                        ALScratchArena::deallocate(theirs);
                        their_counters = ALScratchArena::counters();
                    })
            .join();
        CHECK(!(address(theirs) >= address(mine) && address(theirs) < address(mine) + ALScratchArena::CAPACITY));
        CHECK(their_counters.arena_blocks == 1);
        CHECK(ALScratchArena::counters().arena_blocks == 0);
        ALScratchArena::deallocate(mine);
    }
}
