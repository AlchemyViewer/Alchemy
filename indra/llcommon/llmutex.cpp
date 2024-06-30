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

//---------------------------------------------------------------------
//
// LLSharedMutex
//
LLSharedMutex::LLSharedMutex()
: mLockingThreads(2) // Reserve 2 slots in the map hash table
, mIsShared(false)
{
}

bool LLSharedMutex::isLocked() const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    std::lock_guard<std::mutex> lock(mLockMutex);

    return !mLockingThreads.empty();
}

bool LLSharedMutex::isThreadLocked() const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    LLThread::id_t current_thread = LLThread::currentID();
    std::lock_guard<std::mutex> lock(mLockMutex);

    const_iterator it = mLockingThreads.find(current_thread);
    return it != mLockingThreads.end();
}

void LLSharedMutex::lockShared()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    LLThread::id_t current_thread = LLThread::currentID();

    mLockMutex.lock();
    iterator it = mLockingThreads.find(current_thread);
    if (it != mLockingThreads.end())
    {
        it->second++;
    }
    else
    {
        // Acquire the mutex immediately if the mutex is not locked exclusively
        // or enter a locking state if the mutex is already locked exclusively
        mLockMutex.unlock();
        mSharedMutex.lock_shared();
        mLockMutex.lock();
        // Continue after acquiring the mutex
        mLockingThreads.emplace(std::make_pair(current_thread, 1));
        mIsShared = true;
    }
    mLockMutex.unlock();
}

void LLSharedMutex::lockExclusive()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    LLThread::id_t current_thread = LLThread::currentID();

    mLockMutex.lock();
    iterator it = mLockingThreads.find(current_thread);
    if (it != mLockingThreads.end())
    {
        if (mIsShared)
        {
            // The mutex is already locked in the current thread
            // but this lock is SHARED (not EXCLISIVE)
            // We can't lock it again, the lock stays shared
            // This can lead to a collision (theoretically)
            llassert_always(!"The current thread is already locked SHARED and can't be locked EXCLUSIVE");
        }
        it->second++;
    }
    else
    {
        // Acquire the mutex immediately if mLockingThreads is empty
        // or enter a locking state if mLockingThreads is not empty
        mLockMutex.unlock();
        mSharedMutex.lock();
        mLockMutex.lock();
        // Continue after acquiring the mutex (and possible quitting the locking state)
        mLockingThreads.emplace(std::make_pair(current_thread, 1));
        mIsShared = false;
    }
    mLockMutex.unlock();
}

bool LLSharedMutex::trylockShared()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    LLThread::id_t current_thread = LLThread::currentID();
    std::lock_guard<std::mutex> lock(mLockMutex);

    iterator it = mLockingThreads.find(current_thread);
    if (it != mLockingThreads.end())
    {
        it->second++;
    }
    else
    {
        if (!mSharedMutex.try_lock_shared())
            return false;

        mLockingThreads.emplace(std::make_pair(current_thread, 1));
        mIsShared = true;
    }

    return true;
}

bool LLSharedMutex::trylockExclusive()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    LLThread::id_t current_thread = LLThread::currentID();
    std::lock_guard<std::mutex> lock(mLockMutex);

    if (mLockingThreads.size() == 1 && mLockingThreads.begin()->first == current_thread)
    {
        mLockingThreads.begin()->second++;
    }
    else
    {
        if (!mSharedMutex.try_lock())
            return false;

        mLockingThreads.emplace(std::make_pair(current_thread, 1));
        mIsShared = false;
    }

    return true;
}

void LLSharedMutex::unlockShared()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    LLThread::id_t current_thread = LLThread::currentID();
    std::lock_guard<std::mutex> lock(mLockMutex);

    iterator it = mLockingThreads.find(current_thread);
    if (it != mLockingThreads.end())
    {
        if (it->second > 1)
        {
            it->second--;
        }
        else
        {
            mLockingThreads.erase(it);
            mSharedMutex.unlock_shared();
        }
    }
}

void LLSharedMutex::unlockExclusive()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    LLThread::id_t current_thread = LLThread::currentID();
    std::lock_guard<std::mutex> lock(mLockMutex);

    iterator it = mLockingThreads.find(current_thread);
    if (it != mLockingThreads.end())
    {
        if (it->second > 1)
        {
            it->second--;
        }
        else
        {
            mLockingThreads.erase(it);
            mSharedMutex.unlock();
        }
    }
}

//---------------------------------------------------------------------
//
// LLScopedLock
//
LLScopedLock::LLScopedLock(std::mutex* mutex) : mMutex(mutex)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    if(mutex)
    {
        mutex->lock();
        mLocked = true;
    }
    else
    {
        mLocked = false;
    }
}

LLScopedLock::~LLScopedLock()
{
    unlock();
}

void LLScopedLock::unlock()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
    if(mLocked)
    {
        mLocked = false;
        mMutex->unlock();
    }
}
//============================================================================
