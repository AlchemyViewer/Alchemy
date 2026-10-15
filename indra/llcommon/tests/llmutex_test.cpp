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
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    // A copied guard would unlock its mutex once per copy.
    static_assert(!std::is_copy_constructible_v<LLMutexLock> && !std::is_copy_assignable_v<LLMutexLock>);
    static_assert(!std::is_copy_constructible_v<LLMutexTrylock> && !std::is_copy_assignable_v<LLMutexTrylock>);

    // Runs a body on a thread of its own and says whether it has returned. A body
    // still blocked when the worker goes out of scope is left to finish without it:
    // a deadlock the test has already reported, or a probe kept out of a lock the
    // test still holds. The thread owns the flag it sets, and a probe owns what it
    // locks, so both outlive the worker.
    class Worker
    {
    public:
        template <typename FUNC>
        explicit Worker(FUNC&& body)
        : mDone(std::make_shared<std::atomic<bool>>(false))
        , mThread([done = mDone, body = std::forward<FUNC>(body)]() mutable
            {
                body();
                *done = true;
            })
        {
        }

        ~Worker()
        {
            if (*mDone)
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
            while (!*mDone)
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
        std::shared_ptr<std::atomic<bool>> mDone;
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

    TEST_CASE_METHOD(llmutex_data, "llmutex: isLocked sees a hold from any thread", "[llmutex]")
    {
        mMutex.lock();
        CHECK(mMutex.isLocked());

        std::atomic<bool> seen_by_other{false};
        onOtherThread([&]() { seen_by_other = mMutex.isLocked(); });
        UNSCOPED_INFO("another thread did not see the hold");
        CHECK(seen_by_other);

        mMutex.unlock();
        CHECK_FALSE(mMutex.isLocked());

        std::atomic<bool> stale{true};
        onOtherThread([&]() { stale = mMutex.isLocked(); });
        CHECK_FALSE(stale);
    }

    TEST_CASE("llmutex: the standard lock types take an LLMutex", "[llmutex]")
    {
        LLMutex a;
        LLMutex b;
        {
            std::scoped_lock both(a, b);
            CHECK(a.isSelfLocked());
            CHECK(b.isSelfLocked());
        }
        CHECK_FALSE(a.isLocked());
        CHECK_FALSE(b.isLocked());

        a.lock();
        std::atomic<bool> other_got_it{true};
        std::thread([&]()
            {
                std::unique_lock<LLMutex> lock(a, std::try_to_lock);
                other_got_it = lock.owns_lock();
            }).join();
        UNSCOPED_INFO("try_to_lock took a mutex another thread holds");
        CHECK_FALSE(other_got_it);
        a.unlock();
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

    //------------------------------------------------------------------------
    // LLSharedMutex
    //------------------------------------------------------------------------

    // Whether another thread is kept from taking a lock exclusively right now. The
    // writer holds its own reference, since a blocked one outlives the call.
    bool writerExcluded(std::shared_ptr<LLSharedMutex> mutex)
    {
        Worker writer([mutex]()
            {
                mutex->lockExclusive();
                mutex->unlockExclusive();
            });
        return !writer.finishesWithin(BLOCKED);
    }

    struct llsharedmutex_data
    {
        std::shared_ptr<LLSharedMutex> mShared = std::make_shared<LLSharedMutex>();
        LLSharedMutex& mMutex = *mShared;

        // Whether another thread is kept from taking the lock exclusively right now.
        bool othersExcluded()
        {
            return writerExcluded(mShared);
        }
    };

    TEST_CASE_METHOD(llsharedmutex_data, "llsharedmutex: readers share and a writer waits for them", "[llsharedmutex]")
    {
        mMutex.lockShared();

        Worker reader([this]()
            {
                mMutex.lockShared();
                mMutex.unlockShared();
            });
        UNSCOPED_INFO("a second reader waited on the first");
        CHECK(reader.finishesWithin(RELEASED));

        std::atomic<bool> wrote{false};
        Worker writer([&]()
            {
                mMutex.lockExclusive();
                wrote = true;
                mMutex.unlockExclusive();
            });
        CHECK_FALSE(writer.finishesWithin(BLOCKED));
        CHECK_FALSE(wrote);

        mMutex.unlockShared();
        CHECK(writer.finishesWithin(RELEASED));
        CHECK(wrote);
    }

    TEST_CASE_METHOD(llsharedmutex_data, "llsharedmutex: a writer excludes readers", "[llsharedmutex]")
    {
        mMutex.lockExclusive();

        Worker reader([this]()
            {
                mMutex.lockShared();
                mMutex.unlockShared();
            });
        CHECK_FALSE(reader.finishesWithin(BLOCKED));

        mMutex.unlockExclusive();
        CHECK(reader.finishesWithin(RELEASED));
    }

    TEST_CASE_METHOD(llsharedmutex_data, "llsharedmutex: a reader re-enters while a writer waits", "[llsharedmutex]")
    {
        // The deadlock the recursion tracking exists for. The lock queues new readers
        // behind a waiting writer, so a reader that went back to the lock itself would
        // wait on a writer that is waiting on it.
        std::atomic<bool> writer_started{false};
        std::atomic<bool> first_held{false};
        Worker reader([&]()
            {
                mMutex.lockShared();
                first_held = true;
                while (!writer_started)
                {
                    std::this_thread::yield();
                }
                // Let the writer reach the lock and queue on it.
                std::this_thread::sleep_for(BLOCKED);
                mMutex.lockShared();
                mMutex.unlockShared();
                mMutex.unlockShared();
            });
        while (!first_held)
        {
            std::this_thread::yield();
        }
        Worker writer([&]()
            {
                writer_started = true;
                mMutex.lockExclusive();
                mMutex.unlockExclusive();
            });

        UNSCOPED_INFO("the re-entering reader deadlocked behind the writer");
        CHECK(reader.finishesWithin(RELEASED));
        CHECK(writer.finishesWithin(RELEASED));
    }

    TEST_CASE_METHOD(llsharedmutex_data, "llsharedmutex: a writer gets in while readers keep overlapping", "[llsharedmutex]")
    {
        // A lock that lets new readers in past a waiting writer, as glibc's default rwlock
        // does, keeps the writer out for as long as the readers overlap.
        std::atomic<bool> stop{false};
        auto read = [&]()
        {
            while (!stop)
            {
                mMutex.lockShared();
                std::this_thread::sleep_for(1ms);
                mMutex.unlockShared();
            }
        };
        Worker reader1(read);
        Worker reader2(read);
        Worker reader3(read);
        Worker reader4(read);
        std::this_thread::sleep_for(BLOCKED);

        Worker writer([this]()
            {
                mMutex.lockExclusive();
                mMutex.unlockExclusive();
            });
        UNSCOPED_INFO("the readers kept the writer out");
        CHECK(writer.finishesWithin(RELEASED));

        stop = true;
        CHECK(reader1.finishesWithin(RELEASED));
        CHECK(reader2.finishesWithin(RELEASED));
        CHECK(reader3.finishesWithin(RELEASED));
        CHECK(reader4.finishesWithin(RELEASED));
        CHECK(writer.finishesWithin(RELEASED));
    }

    TEST_CASE_METHOD(llsharedmutex_data, "llsharedmutex: exclusive re-enters and releases on the last unlock", "[llsharedmutex]")
    {
        mMutex.lockExclusive();
        mMutex.lockExclusive();
        mMutex.unlockExclusive();
        UNSCOPED_INFO("released on the first of two unlocks");
        CHECK(othersExcluded());
        mMutex.unlockExclusive();
        CHECK_FALSE(othersExcluded());
    }

    TEST_CASE_METHOD(llsharedmutex_data, "llsharedmutex: shared nests inside exclusive without releasing it", "[llsharedmutex]")
    {
        // LLImageRaw's copies take the source shared, and the source may be the image
        // they already hold exclusively.
        mMutex.lockExclusive();
        mMutex.lockShared();
        mMutex.unlockShared();
        UNSCOPED_INFO("the nested shared unlock released the exclusive hold");
        CHECK(othersExcluded());
        mMutex.unlockExclusive();
        CHECK_FALSE(othersExcluded());
    }

    TEST_CASE_METHOD(llsharedmutex_data, "llsharedmutex: shared re-entry releases on the last unlock", "[llsharedmutex]")
    {
        mMutex.lockShared();
        mMutex.lockShared();
        mMutex.unlockShared();
        UNSCOPED_INFO("released on the first of two shared unlocks");
        CHECK(othersExcluded());
        mMutex.unlockShared();
        CHECK_FALSE(othersExcluded());
    }

    TEST_CASE("llsharedmutex: shared holds on several mutexes release in any order", "[llsharedmutex]")
    {
        auto a = std::make_shared<LLSharedMutex>();
        auto b = std::make_shared<LLSharedMutex>();
        auto c = std::make_shared<LLSharedMutex>();

        a->lockShared();
        b->lockShared();
        c->lockShared();
        b->lockShared();

        b->unlockShared();
        b->unlockShared();
        CHECK_FALSE(writerExcluded(b));
        CHECK(writerExcluded(a));
        CHECK(writerExcluded(c));

        a->unlockShared();
        CHECK_FALSE(writerExcluded(a));
        CHECK(writerExcluded(c));

        c->unlockShared();
        CHECK_FALSE(writerExcluded(c));
    }
}
