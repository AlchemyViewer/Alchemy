/**
 * @file llmutex_test.cpp
 * @brief Unit tests for LLMutex ownership tracking and recursion
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include "llmutex.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

namespace
{
    struct llmutex_data
    {
        LLMutex mMutex;

        // Run body() on a fresh thread and wait for it. Every test here needs the
        // "other thread" perspective, and none of them may outlive the fixture.
        template <typename FUNC>
        void onOtherThread(FUNC&& body)
        {
            std::thread other(std::forward<FUNC>(body));
            other.join();
        }
    };

    TEST_CASE_METHOD(llmutex_data, "llmutex: lock marks ownership and unlock clears it", "[llmutex]")
    {
        CHECK_FALSE(mMutex.isSelfLocked());
        CHECK(mMutex.lockingThread() == LLThread::id_t());

        mMutex.lock();
        CHECK(mMutex.isSelfLocked());
        CHECK(mMutex.lockingThread() == LLThread::currentID());
        CHECK(mMutex.isLocked());

        mMutex.unlock();
        CHECK_FALSE(mMutex.isSelfLocked());
        CHECK(mMutex.lockingThread() == LLThread::id_t());
        CHECK_FALSE(mMutex.isLocked());
    }

    TEST_CASE_METHOD(llmutex_data, "llmutex: lock is recursive and balanced", "[llmutex]")
    {
        mMutex.lock();
        mMutex.lock();
        mMutex.lock();

        CHECK(mMutex.isSelfLocked());

        mMutex.unlock();
        UNSCOPED_INFO("released on the first of three unlocks");
        CHECK(mMutex.isLocked());
        mMutex.unlock();
        UNSCOPED_INFO("released on the second of three unlocks");
        CHECK(mMutex.isLocked());
        mMutex.unlock();

        CHECK_FALSE(mMutex.isLocked());
        CHECK_FALSE(mMutex.isSelfLocked());
    }

    TEST_CASE_METHOD(llmutex_data, "llmutex: trylock recurses on a self-held mutex", "[llmutex]")
    {
        mMutex.lock();
        CHECK(mMutex.trylock());

        mMutex.unlock();
        UNSCOPED_INFO("recursive trylock did not need a matching unlock");
        CHECK(mMutex.isLocked());
        mMutex.unlock();
        CHECK_FALSE(mMutex.isLocked());
    }

    TEST_CASE_METHOD(llmutex_data, "llmutex: ownership does not leak across threads", "[llmutex]")
    {
        mMutex.lock();

        std::atomic<bool> saw_self_locked{true};
        std::atomic<bool> trylock_succeeded{true};
        onOtherThread([&]()
        {
            saw_self_locked = mMutex.isSelfLocked();
            trylock_succeeded = mMutex.trylock();
            if (trylock_succeeded)
            {
                mMutex.unlock();
            }
        });

        UNSCOPED_INFO("another thread saw our lock as its own");
        CHECK_FALSE(saw_self_locked);
        UNSCOPED_INFO("another thread acquired a held mutex");
        CHECK_FALSE(trylock_succeeded);

        mMutex.unlock();

        std::atomic<bool> acquired{false};
        onOtherThread([&]()
        {
            if (mMutex.trylock())
            {
                acquired = true;
                mMutex.unlock();
            }
        });

        UNSCOPED_INFO("another thread could not acquire a released mutex");
        CHECK(acquired);
    }

    TEST_CASE_METHOD(llmutex_data, "llmutex: LLMutexTrylock reports and releases", "[llmutex]")
    {
        {
            LLMutexTrylock lock(&mMutex);
            CHECK(lock.isLocked());
            CHECK(mMutex.isSelfLocked());
        }
        CHECK_FALSE(mMutex.isLocked());

        // A null mutex is legal and locks nothing -- callers pass member pointers that
        // may not be constructed yet.
        LLMutexTrylock null_lock(nullptr);
        CHECK_FALSE(null_lock.isLocked());
    }

    TEST_CASE_METHOD(llmutex_data, "llmutex: mutual exclusion holds under contention", "[llmutex]")
    {
        // Deliberately a non-atomic counter: the mutex is what makes it safe, so a lost
        // acquisition shows up as a short count.
        constexpr int NUM_THREADS = 8;
        constexpr int NUM_ITERATIONS = 5000;
        int counter = 0;
        std::atomic<int> false_self_locks{0};

        std::vector<std::thread> threads;
        threads.reserve(NUM_THREADS);
        for (int i = 0; i < NUM_THREADS; ++i)
        {
            threads.emplace_back([&]()
            {
                for (int j = 0; j < NUM_ITERATIONS; ++j)
                {
                    // Before acquiring, this thread owns nothing. Reading a torn or stale
                    // owner id here would send lock() down the recursive path and drop the
                    // real acquisition entirely.
                    if (mMutex.isSelfLocked())
                    {
                        ++false_self_locks;
                    }
                    mMutex.lock();
                    ++counter;
                    mMutex.unlock();
                }
            });
        }
        for (std::thread& thread : threads)
        {
            thread.join();
        }

        UNSCOPED_INFO("lost an acquisition under contention");
        CHECK(counter == NUM_THREADS * NUM_ITERATIONS);
        UNSCOPED_INFO("thread claimed ownership it did not have");
        CHECK(false_self_locks.load() == 0);
    }
}
