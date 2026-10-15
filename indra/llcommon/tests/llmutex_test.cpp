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

#include "llcoros.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    // Runs a body on a thread of its own and says whether it has returned. A body
    // still blocked when the worker goes out of scope is a deadlock the test has
    // already reported; its thread is detached rather than joined so the run ends.
    class Worker
    {
    public:
        template <typename FUNC>
        explicit Worker(FUNC&& body)
        : mThread([this, body = std::forward<FUNC>(body)]() mutable
            {
                body();
                mDone = true;
            })
        {
        }

        ~Worker()
        {
            if (mDone)
            {
                mThread.join();
            }
            else
            {
                mThread.detach();
            }
        }

        bool finishesWithin(std::chrono::milliseconds limit)
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!mDone)
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    return false;
                }
                std::this_thread::sleep_for(1ms);
            }
            return true;
        }

    private:
        std::atomic<bool> mDone{false};
        std::thread mThread;
    };

    // Long enough that a thread which could run has run; a body that has not
    // returned by then is blocked.
    constexpr auto BLOCKED = 50ms;
    // Long enough that a thread which was released has finished.
    constexpr auto RELEASED = 5s;

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

    // LLMutex::lock() asserts this on every call in assert builds.
    TEST_CASE("llmutex: only a launched coroutine is off the main coroutine", "[llmutex]")
    {
        CHECK(LLCoros::on_main_coro());

        std::atomic<bool> thread_main{false};
        std::thread([&]() { thread_main = LLCoros::on_main_coro(); }).join();
        UNSCOPED_INFO("a plain thread is its own main coroutine");
        CHECK(thread_main);

        bool coro_main = true;
        bool ran = false;
        LLCoros::instance().launch("llmutex_test", [&]()
            {
                coro_main = LLCoros::on_main_coro();
                ran = true;
            });
        // The launched coroutine runs to completion before launch() returns here.
        REQUIRE(ran);
        CHECK_FALSE(coro_main);
        CHECK(LLCoros::on_main_coro());
    }

    //------------------------------------------------------------------------
    // LLCondition
    //------------------------------------------------------------------------

    struct llcondition_data
    {
        LLCondition mCond;
    };

    TEST_CASE_METHOD(llcondition_data, "llcondition: a predicate runs with the condition owned by its thread", "[llcondition]")
    {
        // The predicate reads state lock() guards, so it must be able to take the lock
        // again as any other code on that thread could.
        bool owned = false;
        bool relocked = false;
        const bool satisfied = mCond.waitFor(1ms, [&]()
            {
                owned = mCond.isSelfLocked();
                if (owned)
                {
                    mCond.lock();
                    relocked = true;
                    mCond.unlock();
                }
                return true;
            });

        CHECK(satisfied);
        UNSCOPED_INFO("the predicate's thread did not own the condition it held");
        CHECK(owned);
        CHECK(relocked);
        CHECK_FALSE(mCond.isSelfLocked());
        CHECK_FALSE(mCond.isLocked());
    }

    TEST_CASE_METHOD(llcondition_data, "llcondition: a wait that times out gives the lock back", "[llcondition]")
    {
        CHECK_FALSE(mCond.waitFor(1ms, []() { return false; }));
        CHECK_FALSE(mCond.isSelfLocked());
        CHECK_FALSE(mCond.isLocked());
    }

    TEST_CASE_METHOD(llcondition_data, "llcondition: a predicate that throws gives the lock back", "[llcondition]")
    {
        CHECK_THROWS_AS(mCond.wait([]() -> bool { throw std::runtime_error("predicate"); }), std::runtime_error);
        CHECK_FALSE(mCond.isSelfLocked());
        CHECK_FALSE(mCond.isLocked());
    }

    TEST_CASE_METHOD(llcondition_data, "llcondition: a waiter wakes owning the condition", "[llcondition]")
    {
        bool ready = false;
        std::atomic<bool> owned_on_wake{false};
        std::atomic<bool> owned_after{true};
        Worker waiter([&]()
            {
                mCond.wait([&]()
                    {
                        owned_on_wake = mCond.isSelfLocked();
                        return ready;
                    });
                owned_after = mCond.isSelfLocked();
            });

        CHECK_FALSE(waiter.finishesWithin(BLOCKED));
        {
            LLMutexLock lock(&mCond);
            ready = true;
        }
        mCond.signal();

        REQUIRE(waiter.finishesWithin(RELEASED));
        CHECK(owned_on_wake);
        CHECK_FALSE(owned_after);
        CHECK_FALSE(mCond.isLocked());
    }
}
