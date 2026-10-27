/**
 * @file alsharedmutex.h
 * @brief A reader-writer lock that lets a waiting writer in ahead of the
 *        readers that come after it.
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

#pragma once

#include "stdtypes.h"

#include <atomic>
#include <shared_mutex>

#if defined(__GLIBC__)
#include <pthread.h>
#endif

// A reader-writer lock neither side may take again while it holds it. A writer
// that has to wait is let in ahead of the readers that come after it, so readers
// that keep overlapping cannot keep it out. It takes std::unique_lock and
// std::shared_lock. LLSharedMutex, which either side may re-enter, is built on it.
class ALSharedMutex
{
public:
#if defined(__GLIBC__)
    // glibc's default rwlock lets a new reader in past a waiting writer, so readers that
    // keep overlapping keep a writer out for as long as they do. This one prefers writers,
    // as libc++'s shared_mutex does. glibc calls that kind nonrecursive: a reader taking
    // it again behind a waiting writer would wait on itself.
    ALSharedMutex()
    {
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
        pthread_rwlock_init(&mLock, &attr);
        pthread_rwlockattr_destroy(&attr);
    }
    ~ALSharedMutex() { pthread_rwlock_destroy(&mLock); }
#else
    ALSharedMutex() = default;
#endif

    ALSharedMutex(const ALSharedMutex&) = delete;
    ALSharedMutex& operator=(const ALSharedMutex&) = delete;

#if defined(__GLIBC__)
    void lock() { pthread_rwlock_wrlock(&mLock); }
    void unlock() { pthread_rwlock_unlock(&mLock); }
    void lock_shared() { pthread_rwlock_rdlock(&mLock); }
    void unlock_shared() { pthread_rwlock_unlock(&mLock); }
#elif defined(LL_WINDOWS)
    // std::shared_mutex is an SRW lock, which lets in any thread that finds it free: a
    // writer it wakes can find it taken again before it runs, and readers that keep
    // overlapping kept one out for over 10 s. A writer that has to wait shuts a gate
    // first, and new readers wait at the gate instead of at the lock, so only a reader
    // already past it can come between the writer and the lock, and only once.
    void lock()
    {
        if (mLock.try_lock())
        {
            return;
        }
        // The count only sends readers to the gate and never decides who holds the
        // lock, so relaxed suffices: a reader that misses a rise gets in once, as it
        // would have a moment earlier.
        mWritersWaiting.fetch_add(1, std::memory_order_relaxed);
        mGate.lock();
        mLock.lock();
        mGate.unlock();
        mWritersWaiting.fetch_sub(1, std::memory_order_relaxed);
    }
    void unlock() { mLock.unlock(); }

    void lock_shared()
    {
        if (mWritersWaiting.load(std::memory_order_relaxed) != 0)
        {
            // Shut until the waiting writer has the lock.
            mGate.lock_shared();
            mGate.unlock_shared();
        }
        mLock.lock_shared();
    }
    void unlock_shared() { mLock.unlock_shared(); }
#else
    // macOS's: libc++'s shared_mutex already lets a waiting writer in first.
    void lock() { mLock.lock(); }
    void unlock() { mLock.unlock(); }
    void lock_shared() { mLock.lock_shared(); }
    void unlock_shared() { mLock.unlock_shared(); }
#endif

private:
#if defined(__GLIBC__)
    pthread_rwlock_t mLock;
#elif defined(LL_WINDOWS)
    std::shared_mutex mLock;
    // An SRW lock too: readers pass it together, and std::mutex is ten times its size.
    std::shared_mutex mGate;
    std::atomic<U32> mWritersWaiting{0};
#else
    std::shared_mutex mLock;
#endif
};
