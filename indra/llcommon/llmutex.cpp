/**
 * @file llmutex.cpp
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "linden_common.h"

#include "llmutex.h"
#include "llthread.h"
#include "lltimer.h"
#include "llcoros.h"


//---------------------------------------------------------------------
//
// LLMutex
//
LLMutex::LLMutex() :
 mCount(0),
 mLockingThread(LLThread::id_t())
{
}

LLMutex::~LLMutex()
{
}

void LLMutex::lock()
{
    // LLMutex is not coroutine aware and should not be used from a coroutine
    // If your code is running in a coroutine, you should use LLCoros::Mutex instead
    // NOTE:  If the stack trace you're staring at contains non-thread-safe code,
    // you should use LLAppViewer::instance().postToMainThread() to shuttle execution
    // back to the main loop.
    // NOTE: If you got here from seeing this assert in your log and you're not seeing
    // a stack trace that points here, put a breakpoint in on_main_coro and try again.
    llassert(LLCoros::on_main_coro());

    if(isSelfLocked())
    { //redundant lock
        mCount++;
        return;
    }

    if (!mMutex.try_lock())
    {
        // Only a lock that has to wait shows in a profile.
        LL_PROFILE_ZONE_NAMED_CATEGORY_THREAD("LLMutex contended");
        mMutex.lock();
    }

    mLockingThread.store(LLThread::currentID(), std::memory_order_relaxed);
}

void LLMutex::unlock()
{
    if (mCount > 0)
    { //not the root unlock
        mCount--;
        return;
    }

    mLockingThread.store(LLThread::id_t(), std::memory_order_relaxed);
    mMutex.unlock();
}

bool LLMutex::isLocked() const
{
    // The owner fields, not a try_lock(): that would hold the lock for a moment against
    // every other thread, and is undefined on a mutex the caller already holds.
    return lockingThread() != LLThread::id_t();
}

bool LLMutex::isSelfLocked()
{
    // Relaxed is not just cheap, it is exact. If this thread owns the lock it is reading back
    // its own store from lock()/trylock(). If it does not, whatever it observes is another
    // thread's id or the empty id, and "false" is the right answer either way. Only a torn
    // read could ever have made this wrong, and that is what the atomic rules out.
    return mLockingThread.load(std::memory_order_relaxed) == LLThread::currentID();
}

LLThread::id_t LLMutex::lockingThread() const
{
    return mLockingThread.load(std::memory_order_relaxed);
}

bool LLMutex::trylock()
{
    if (isSelfLocked())
    { //redundant lock
        mCount++;
        return true;
    }

    if (!mMutex.try_lock())
    {
        return false;
    }

    mLockingThread.store(LLThread::currentID(), std::memory_order_relaxed);
    return true;
}


//---------------------------------------------------------------------
//
// LLSharedMutex
//
namespace
{
    // The shared locks this thread holds, and how many times it holds each. A thread
    // holds few at once and none for long, so a short scan beats any lookup.
    struct SharedHold
    {
        const LLSharedMutex* mMutex;
        U32 mDepth;
    };
    constexpr U32 MAX_SHARED_HOLDS = 8;
    thread_local SharedHold tSharedHolds[MAX_SHARED_HOLDS];
    thread_local U32 tSharedHoldCount = 0;

    SharedHold* findSharedHold(const LLSharedMutex* mutex)
    {
        for (U32 i = 0; i < tSharedHoldCount; ++i)
        {
            if (tSharedHolds[i].mMutex == mutex)
            {
                return &tSharedHolds[i];
            }
        }
        return nullptr;
    }
}

LLSharedMutex::LLSharedMutex()
: mExclusiveOwner(LLThread::id_t())
, mExclusiveDepth(0)
{
}

void LLSharedMutex::lockShared()
{
    if (mExclusiveOwner.load(std::memory_order_relaxed) == LLThread::currentID())
    {
        // Inside this thread's exclusive hold, which already covers it.
        ++mExclusiveDepth;
        return;
    }

    if (SharedHold* hold = findSharedHold(this))
    {
        // Never back to the lock itself: it queues a new reader behind a waiting writer,
        // which would here be waiting on this thread.
        ++hold->mDepth;
        return;
    }

    if (tSharedHoldCount == MAX_SHARED_HOLDS)
    {
        LL_ERRS() << "One thread holds " << MAX_SHARED_HOLDS << " shared locks at once" << LL_ENDL;
    }
    mSharedMutex.lock_shared();
    tSharedHolds[tSharedHoldCount++] = { this, 1 };
}

void LLSharedMutex::lockExclusive()
{
    const LLThread::id_t self = LLThread::currentID();
    if (mExclusiveOwner.load(std::memory_order_relaxed) == self)
    {
        ++mExclusiveDepth;
        return;
    }

    // Under this thread's own shared hold, the exclusive lock would wait on it forever.
    llassert_always(!findSharedHold(this));
    mSharedMutex.lock();
    mExclusiveOwner.store(self, std::memory_order_relaxed);
}

void LLSharedMutex::unlockShared()
{
    if (mExclusiveOwner.load(std::memory_order_relaxed) == LLThread::currentID())
    {
        // A shared lock taken inside the exclusive hold.
        unlockExclusive();
        return;
    }

    SharedHold* hold = findSharedHold(this);
    llassert(hold);
    if (hold && --hold->mDepth == 0)
    {
        *hold = tSharedHolds[--tSharedHoldCount];
        mSharedMutex.unlock_shared();
    }
}

void LLSharedMutex::unlockExclusive()
{
    llassert(mExclusiveOwner.load(std::memory_order_relaxed) == LLThread::currentID());
    if (mExclusiveDepth > 0)
    {
        --mExclusiveDepth;
        return;
    }

    mExclusiveOwner.store(LLThread::id_t(), std::memory_order_relaxed);
    mSharedMutex.unlock();
}


//---------------------------------------------------------------------
//
// LLCondition
//
LLCondition::LLCondition() :
    LLMutex()
{
}

LLCondition::~LLCondition()
{
}

LLCondition::Waiter::Waiter(LLCondition& condition) :
    mCondition(condition),
    mLock(condition.mMutex, std::defer_lock)
{
    // A wait gives the lock up while asleep, which would take it from under a caller
    // that already held it.
    llassert(!mCondition.isSelfLocked());
    mLock.lock();
    mCondition.mLockingThread.store(LLThread::currentID(), std::memory_order_relaxed);
}

LLCondition::Waiter::~Waiter()
{
    // Disowned before mLock unlocks, as unlock() does.
    mCondition.mLockingThread.store(LLThread::id_t(), std::memory_order_relaxed);
}

void LLCondition::Waiter::sleep()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    // A predicate that re-locked has unlocked again by now.
    llassert(mCondition.mCount == 0);
    const LLThread::id_t self = mCondition.mLockingThread.load(std::memory_order_relaxed);
    mCondition.mLockingThread.store(LLThread::id_t(), std::memory_order_relaxed);
    mCondition.mCond.wait(mLock);
    mCondition.mLockingThread.store(self, std::memory_order_relaxed);
}

bool LLCondition::Waiter::sleepUntil(const std::chrono::steady_clock::time_point& deadline)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    llassert(mCondition.mCount == 0);
    const LLThread::id_t self = mCondition.mLockingThread.load(std::memory_order_relaxed);
    mCondition.mLockingThread.store(LLThread::id_t(), std::memory_order_relaxed);
    const std::cv_status status = mCondition.mCond.wait_until(mLock, deadline);
    mCondition.mLockingThread.store(self, std::memory_order_relaxed);
    return status == std::cv_status::no_timeout;
}

void LLCondition::signal()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    mCond.notify_one();
}

void LLCondition::broadcast()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    mCond.notify_all();
}

//============================================================================
