/**
 * @file llmutex.h
 * @brief Base classes for mutex and condition handling.
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLMUTEX_H
#define LL_LLMUTEX_H

#include "stdtypes.h"
#include "llthread.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <condition_variable>

#if defined(__GLIBC__)
#include <pthread.h>
#endif

//============================================================================

class LL_COMMON_API LLMutex
{
public:
    LLMutex();
    ~LLMutex();

    void lock();        // blocks
    bool trylock();     // non-blocking, returns true if lock held.
    bool try_lock() { return trylock(); } // for std::unique_lock and std::scoped_lock
    void unlock();      // undefined behavior when called on mutex not being held
    bool isLocked() const; // non-blocking; exact for the holder, a snapshot for any other thread
    bool isSelfLocked(); //return true if locked in a same thread
    LLThread::id_t lockingThread() const; //get ID of locking thread

protected:
    std::mutex          mMutex;
    // Owner-only: read and written solely by whichever thread currently holds mMutex.
    mutable U32         mCount;
    // Read by every caller of lock()/trylock() before mMutex is held, and written by the
    // owner, so it must be atomic even though relaxed ordering suffices -- see
    // LLMutex::isSelfLocked() for why relaxed is not merely a heuristic here.
    mutable std::atomic<LLThread::id_t> mLockingThread;

    static_assert(std::atomic<LLThread::id_t>::is_always_lock_free,
                  "LLMutex::mLockingThread is read on the lock() fast path; a locking atomic here "
                  "would put a mutex inside the mutex");
};

//============================================================================

// A reader-writer lock either side may re-enter. A thread holding it exclusively may
// take it again either way; a thread holding it shared may take it shared again, even
// while a writer waits, but not exclusively, which would wait on its own hold. Readers
// share nothing but the lock itself: a shared re-entry is found in a table of the shared
// locks its thread holds, never in the mutex.
class LL_COMMON_API LLSharedMutex
{
public:
    LLSharedMutex();

    LLSharedMutex(const LLSharedMutex&) = delete;
    LLSharedMutex& operator=(const LLSharedMutex&) = delete;

    void lockShared();
    void lockExclusive();
    template<bool SHARED> void lock();

    void unlockShared();
    void unlockExclusive();
    template<bool SHARED> void unlock();

private:
#if defined(__GLIBC__)
    // glibc's default rwlock lets a new reader in past a waiting writer, so readers that
    // keep overlapping keep a writer out for as long as they do. This one prefers writers,
    // as the SRW lock and libc++'s shared_mutex do. glibc calls that kind nonrecursive: a
    // reader taking it again behind a waiting writer would wait on itself, which a shared
    // re-entry here never does.
    class RWLock
    {
    public:
        RWLock()
        {
            pthread_rwlockattr_t attr;
            pthread_rwlockattr_init(&attr);
            pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
            pthread_rwlock_init(&mLock, &attr);
            pthread_rwlockattr_destroy(&attr);
        }
        ~RWLock() { pthread_rwlock_destroy(&mLock); }

        RWLock(const RWLock&) = delete;
        RWLock& operator=(const RWLock&) = delete;

        void lock() { pthread_rwlock_wrlock(&mLock); }
        void unlock() { pthread_rwlock_unlock(&mLock); }
        void lock_shared() { pthread_rwlock_rdlock(&mLock); }
        void unlock_shared() { pthread_rwlock_unlock(&mLock); }

    private:
        pthread_rwlock_t mLock;
    };
#else
    using RWLock = std::shared_mutex;
#endif
    RWLock mSharedMutex;
    // The exclusive holder. Relaxed for the reason LLMutex::mLockingThread is.
    std::atomic<LLThread::id_t> mExclusiveOwner;
    // Owner-only: the holds the exclusive holder has taken beyond its first, either way.
    U32 mExclusiveDepth;
};

template<>
inline void LLSharedMutex::lock<true>()
{
    lockShared();
}

template<>
inline void LLSharedMutex::lock<false>()
{
    lockExclusive();
}

template<>
inline void LLSharedMutex::unlock<true>()
{
    unlockShared();
}

template<>
inline void LLSharedMutex::unlock<false>()
{
    unlockExclusive();
}

// Actually a condition/mutex pair (since each condition needs to be associated with a mutex).
class LL_COMMON_API LLCondition : public LLMutex
{
public:
    LLCondition();
    ~LLCondition();

    void signal();
    void broadcast();

    // Sleep until pred() returns true. pred is checked under this condition's lock,
    // which its thread owns as lock() would: pred may read state that lock() guards,
    // and may lock() this condition again. The lock is given up only while asleep, so
    // a change made under it and then signalled cannot fall between the check and the
    // sleep. Call without holding this condition.
    template <typename PRED>
    void wait(PRED&& pred)
    {
        Waiter waiter(*this);
        while (!pred())
        {
            waiter.sleep();
        }
    }

    // As wait(pred), but for at most timeout: true once pred() holds, false if the
    // timeout passed first. Callers typically loop on false to keep waiting while
    // emitting periodic diagnostics.
    template <typename REP, typename PERIOD, typename PRED>
    bool waitFor(const std::chrono::duration<REP, PERIOD>& timeout, PRED&& pred)
    {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::ceil<std::chrono::steady_clock::duration>(timeout);
        Waiter waiter(*this);
        while (!pred())
        {
            if (!waiter.sleepUntil(deadline))
            {
                return pred();
            }
        }
        return true;
    }

protected:
    std::condition_variable mCond;

private:
    // This condition's lock, held for one wait and owned as lock() owns it.
    class Waiter
    {
    public:
        explicit Waiter(LLCondition& condition);
        ~Waiter();

        void sleep();
        // False once the deadline has passed.
        bool sleepUntil(const std::chrono::steady_clock::time_point& deadline);

    private:
        LLCondition& mCondition;
        std::unique_lock<std::mutex> mLock;
    };
};

//============================================================================

class LLMutexLock
{
public:
    LLMutexLock(LLMutex* mutex)
    {
        mMutex = mutex;

        if (mMutex)
            mMutex->lock();
    }

    LLMutexLock(const LLMutexLock&) = delete;
    LLMutexLock& operator=(const LLMutexLock&) = delete;

    ~LLMutexLock()
    {
        if (mMutex)
            mMutex->unlock();
    }

private:
    LLMutex* mMutex;
};

//============================================================================

template<bool SHARED>
class LLSharedMutexLockTemplate
{
public:
    LLSharedMutexLockTemplate(LLSharedMutex* mutex)
    : mSharedMutex(mutex)
    {
        if (mSharedMutex)
            mSharedMutex->lock<SHARED>();
    }

    LLSharedMutexLockTemplate(const LLSharedMutexLockTemplate&) = delete;
    LLSharedMutexLockTemplate& operator=(const LLSharedMutexLockTemplate&) = delete;

    ~LLSharedMutexLockTemplate()
    {
        if (mSharedMutex)
            mSharedMutex->unlock<SHARED>();
    }

    void lock()
    {
        if (mSharedMutex)
            mSharedMutex->lock<SHARED>();
    }

    void unlock()
    {
        if (mSharedMutex)
            mSharedMutex->unlock<SHARED>();
    }

private:
    LLSharedMutex* mSharedMutex;
};

using LLSharedMutexLock = LLSharedMutexLockTemplate<true>;
using LLExclusiveMutexLock = LLSharedMutexLockTemplate<false>;

//============================================================================

// Scoped locking class similar in function to LLMutexLock but uses
// the trylock() method to conditionally acquire lock without
// blocking.  Caller resolves the resulting condition by calling
// the isLocked() method and either punts or continues as indicated.
//
// Mostly of interest to callers needing to avoid stalls who can
// guarantee another attempt at a later time.

class LLMutexTrylock
{
public:
    LLMutexTrylock(LLMutex* mutex) : mMutex(mutex), mLocked(false)
    {
        if (mMutex)
            mLocked = mMutex->trylock();
    }

    LLMutexTrylock(const LLMutexTrylock&) = delete;
    LLMutexTrylock& operator=(const LLMutexTrylock&) = delete;

    ~LLMutexTrylock()
    {
        if (mMutex && mLocked)
            mMutex->unlock();
    }

    bool isLocked() const
    {
        return mLocked;
    }

private:
    LLMutex*    mMutex;
    bool        mLocked;
};

#endif // LL_LLMUTEX_H
