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

#include "../test/lltut.h"

#include <atomic>
#include <thread>
#include <vector>

namespace tut
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

    typedef test_group<llmutex_data> llmutex_group;
    typedef llmutex_group::object object;
    llmutex_group llmutexgrp("llmutex");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("lock marks ownership, unlock clears it");

        ensure("unlocked mutex reports self-locked", !mMutex.isSelfLocked());
        ensure_equals("unlocked mutex has an owner", mMutex.lockingThread(), LLThread::id_t());

        mMutex.lock();
        ensure("locked mutex does not report self-locked", mMutex.isSelfLocked());
        ensure_equals("locking thread not recorded", mMutex.lockingThread(), LLThread::currentID());
        ensure("isLocked() false while held", mMutex.isLocked());

        mMutex.unlock();
        ensure("still self-locked after unlock", !mMutex.isSelfLocked());
        ensure_equals("owner not cleared on unlock", mMutex.lockingThread(), LLThread::id_t());
        ensure("isLocked() true after unlock", !mMutex.isLocked());
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("lock is recursive and balanced");

        mMutex.lock();
        mMutex.lock();
        mMutex.lock();

        ensure("self-lock lost during recursion", mMutex.isSelfLocked());

        mMutex.unlock();
        ensure("released on the first of three unlocks", mMutex.isLocked());
        mMutex.unlock();
        ensure("released on the second of three unlocks", mMutex.isLocked());
        mMutex.unlock();

        ensure("not released after balanced unlocks", !mMutex.isLocked());
        ensure("still self-locked after balanced unlocks", !mMutex.isSelfLocked());
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("trylock recurses on a self-held mutex");

        mMutex.lock();
        ensure("trylock failed on a mutex this thread already holds", mMutex.trylock());

        mMutex.unlock();
        ensure("recursive trylock did not need a matching unlock", mMutex.isLocked());
        mMutex.unlock();
        ensure("not released after balanced unlocks", !mMutex.isLocked());
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("ownership does not leak across threads");

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

        ensure("another thread saw our lock as its own", !saw_self_locked);
        ensure("another thread acquired a held mutex", !trylock_succeeded);

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

        ensure("another thread could not acquire a released mutex", acquired);
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("LLMutexTrylock reports and releases");

        {
            LLMutexTrylock lock(&mMutex);
            ensure("trylock scope failed on a free mutex", lock.isLocked());
            ensure("trylock scope did not take ownership", mMutex.isSelfLocked());
        }
        ensure("trylock scope did not release", !mMutex.isLocked());

        // A null mutex is legal and locks nothing -- callers pass member pointers that
        // may not be constructed yet.
        LLMutexTrylock null_lock(nullptr);
        ensure("null trylock scope claimed a lock", !null_lock.isLocked());
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("mutual exclusion holds under contention");

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

        ensure_equals("lost an acquisition under contention", counter, NUM_THREADS * NUM_ITERATIONS);
        ensure_equals("thread claimed ownership it did not have", false_self_locks.load(), 0);
    }
}
