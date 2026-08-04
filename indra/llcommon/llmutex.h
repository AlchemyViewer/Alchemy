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

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <condition_variable>

//============================================================================

//#define MUTEX_DEBUG (LL_DEBUG || LL_RELEASE_WITH_DEBUG_INFO)
#define MUTEX_DEBUG 0 //disable mutex debugging as it's interfering with profiles

#if MUTEX_DEBUG
#include <map>
#endif

class LL_COMMON_API LLMutex
{
public:
    LLMutex();
    virtual ~LLMutex();

    void lock();        // blocks
    bool trylock();     // non-blocking, returns true if lock held.
    void unlock();      // undefined behavior when called on mutex not being held
    bool isLocked();    // non-blocking, but does do a lock/unlock so not free
    bool isSelfLocked(); //return true if locked in a same thread
    LLThread::id_t lockingThread() const; //get ID of locking thread

protected:
    std::mutex          mMutex;
    mutable U32         mCount;
    mutable LLThread::id_t  mLockingThread;

#if MUTEX_DEBUG
    std::unordered_map<LLThread::id_t, bool> mIsLocked;
#endif
};

//============================================================================

class LL_COMMON_API LLSharedMutex
{
public:
    LLSharedMutex();

    bool isLocked() const;
    bool isThreadLocked() const;
    bool isShared() const { return mIsShared; }

    void lockShared();
    void lockExclusive();
    template<bool SHARED> void lock();

    bool trylockShared();
    bool trylockExclusive();
    template<bool SHARED> bool trylock();

    void unlockShared();
    void unlockExclusive();
    template<bool SHARED> void unlock();

private:
    std::shared_mutex mSharedMutex;
    mutable std::mutex mLockMutex;
    std::unordered_map<LLThread::id_t, U32> mLockingThreads;
    bool mIsShared;

    using iterator = std::unordered_map<LLThread::id_t, U32>::iterator;
    using const_iterator = std::unordered_map<LLThread::id_t, U32>::const_iterator;
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
inline bool LLSharedMutex::trylock<true>()
{
    return trylockShared();
}

template<>
inline bool LLSharedMutex::trylock<false>()
{
    return trylockExclusive();
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

    void wait();        // blocks
    void signal();
    void broadcast();

    // Predicate form: sleep until pred() returns true, atomically re-checking it
    // under this condition's mutex on every wakeup. Unlike the bare wait() above,
    // this closes the classic lost-wakeup window (state change + signal landing
    // between a caller's own predicate check and its wait() call would otherwise
    // sleep forever), so use it whenever the condition is one-shot. Call WITHOUT
    // holding this mutex; pred runs with it held, so it may read state guarded by
    // lock()/unlock() on this same object.
    template <typename PRED>
    void wait(PRED&& pred)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, std::forward<PRED>(pred));
    }

    // Timed predicate form: returns true once pred() holds, false if the timeout
    // elapsed first (pred re-checked under this condition's mutex either way).
    // Same lost-wakeup-safe contract as wait(pred) above -- callers typically loop
    // on a false return to keep waiting while emitting periodic diagnostics.
    template <typename REP, typename PERIOD, typename PRED>
    bool waitFor(const std::chrono::duration<REP, PERIOD>& timeout, PRED&& pred)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCond.wait_for(lock, timeout, std::forward<PRED>(pred));
    }

protected:
    std::condition_variable mCond;
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

    LLMutexTrylock(LLMutex* mutex, U32 aTries, U32 delay_ms) : mMutex(mutex), mLocked(false)
    {
        if (!mMutex)
            return;

        for (U32 i = 0; i < aTries; ++i)
        {
            mLocked = mMutex->trylock();
            if (mLocked)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

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

//============================================================================

/**
* @class LLScopedLock
* @brief Small class to help lock and unlock mutexes.
*
* The constructor handles the lock, and the destructor handles
* the unlock. Instances of this class are <b>not</b> thread safe.
*/
class LL_COMMON_API LLScopedLock
{
public:
    /**
    * @brief Constructor which accepts a mutex, and locks it.
    *
    * @param mutex An allocated mutex. If you pass in NULL,
    * this wrapper will not lock.
    */
    LLScopedLock(std::mutex* mutex);

    /**
    * @brief Destructor which unlocks the mutex if still locked.
    */
    ~LLScopedLock();

    /*
    * @brief Non-copyable constructor and operator
    */
    LLScopedLock(const LLScopedLock&) = delete;
    LLScopedLock& operator=(const LLScopedLock&) = delete;

    /**
    * @brief Check lock.
    */
    bool isLocked() const { return mLocked; }

    /**
    * @brief This method unlocks the mutex.
    */
    void unlock();

protected:
    bool mLocked;
    std::mutex* mMutex;
};

#endif // LL_LLMUTEX_H
