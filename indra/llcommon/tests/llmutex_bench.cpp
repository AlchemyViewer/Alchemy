/**
 * @file llmutex_bench.cpp
 * @brief The cost of LLMutex and LLSharedMutex as the viewer takes them.
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

// An LLMutex taken and released on one thread, then on eight threads that each
// lock a mutex of their own: that costs no more than one thread's share unless
// the lock itself shares something between threads. LLSharedMutex as
// LLImageBase takes it: made with every image, read-locked, held exclusively
// and re-entered as LLImageRaw::resize() re-enters it, and read by eight
// threads at once.
//
// The output is not a verdict; a number is read against the same row from
// another build, on the same quiet machine.

#include "linden_common.h"

#include "llmutex.h"

#include "llcoros.h"

#include "altestbenchmark.h"

#include <catch2/benchmark/catch_optimizer.hpp>

#include <memory>
#include <thread>
#include <vector>

namespace
{
    constexpr int THREADS = 8;
    // Enough that starting the threads is a small part of each run.
    constexpr int OWN_LOCKS_PER_THREAD = 100000;
    constexpr int SHARED_LOCKS_PER_THREAD = 20000;

    template <typename FUNC>
    void onThreads(FUNC&& body)
    {
        std::vector<std::thread> threads;
        threads.reserve(THREADS);
        for (int i = 0; i < THREADS; ++i)
        {
            threads.emplace_back(body, i);
        }
        for (std::thread& thread : threads)
        {
            thread.join();
        }
    }

    // A cache line or two apart, so the threads share nothing but the code.
    struct alignas(128) PaddedMutex
    {
        LLMutex mMutex;
    };

    TEST_CASE("llmutex_bench: LLMutex", "[llmutex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();

        // The viewer always has LLCoros, and in an assert build every lock() asks it
        // whether this is the main coroutine.
        LLCoros::instance();

        LLMutex mutex;
        BENCHMARK("lock and unlock")
        {
            mutex.lock();
            mutex.unlock();
        };

        std::vector<PaddedMutex> own(THREADS);
        BENCHMARK("eight threads, each locking its own mutex 100,000 times")
        {
            onThreads([&own](int i)
                {
                    LLMutex& mine = own[i].mMutex;
                    for (int n = 0; n < OWN_LOCKS_PER_THREAD; ++n)
                    {
                        mine.lock();
                        mine.unlock();
                    }
                });
        };
    }

    TEST_CASE("llmutex_bench: LLSharedMutex", "[llmutex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();

        BENCHMARK("construct and destroy, on the heap as an image is")
        {
            auto mutex = std::make_unique<LLSharedMutex>();
            // Keeps the allocation. Returning its address would too, but clang reads that
            // as returning the address of the local unique_ptr.
            Catch::Benchmark::keep_memory(mutex.get());
        };

        LLSharedMutex mutex;
        BENCHMARK("shared lock and unlock")
        {
            mutex.lockShared();
            mutex.unlockShared();
        };

        BENCHMARK("exclusive lock re-entered once")
        {
            mutex.lockExclusive();
            mutex.lockExclusive();
            mutex.unlockExclusive();
            mutex.unlockExclusive();
        };

        BENCHMARK("eight threads, each reading one mutex 20,000 times")
        {
            onThreads([&mutex](int)
                {
                    for (int n = 0; n < SHARED_LOCKS_PER_THREAD; ++n)
                    {
                        mutex.lockShared();
                        mutex.unlockShared();
                    }
                });
        };
    }
}
