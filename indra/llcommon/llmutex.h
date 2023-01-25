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

#include <shared_mutex>
#include <condition_variable>

//============================================================================

#define MUTEX_DEBUG (LL_DEBUG || LL_RELEASE_WITH_DEBUG_INFO)

#if MUTEX_DEBUG
#include <map>
#endif


// This is a recursive mutex.
class LL_COMMON_API LLMutex
{
public:
	LLMutex() = default;
	
	void lock();		// blocks
	bool try_lock();		// non-blocking, returns true if lock held.
	void unlock();		// undefined behavior when called on mutex not being held
	bool isLocked(); 	// non-blocking, but does do a lock/unlock so not free
	bool isSelfLocked(); //return true if locked in a same thread
	LLThread::id_t lockingThread() const; //get ID of locking thread

protected:
	std::shared_mutex			mMutex;
	mutable U32			mCount = 0;
	mutable LLThread::id_t	mLockingThread;
	
#if MUTEX_DEBUG
	std::map<LLThread::id_t, BOOL> mIsLocked;
#endif
};

// Actually a condition/mutex pair (since each condition needs to be associated with a mutex).
class LL_COMMON_API LLCondition final : public LLMutex
{
public:
	LLCondition();
	
	void wait();		// blocks
	void signal();
	void broadcast();
	
protected:
	std::condition_variable_any mCond;
};

class LLMutexLock
{
public:
	LLMutexLock(LLMutex* mutex)
		: mMutex(mutex)
		, mLocked(false)
	{
		lock();
	}

	~LLMutexLock()
	{
		unlock();
	}

	void lock()
	{
		if (mMutex && !mLocked)
		{
			mMutex->lock();
			mLocked = true;
		}
	}

	void unlock()
	{
		if (mMutex && mLocked)
		{
			mLocked = false;
			mMutex->unlock();
		}
	}
private:
	LLMutex* mMutex;
	bool		mLocked;
};

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
	LLMutexTrylock(LLMutex* mutex)
		: mMutex(mutex),
		mLocked(false)
	{
		if (mMutex)
			mLocked = mMutex->try_lock();
	}

	LLMutexTrylock(LLMutex* mutex, U32 aTries, U32 delay_ms = 10)
		: mMutex(mutex),
		mLocked(false)
	{
		lock(aTries, delay_ms);
	}

	~LLMutexTrylock()
	{
		unlock();
	}

	bool isLocked() const
	{
		return mLocked;
	}

	void lock()
	{
		if (mMutex && !mLocked)
		{
			mLocked = mMutex->try_lock();
		}
	}

	void lock(U32 aTries, U32 delay_ms)
	{
		if (mMutex && !mLocked)
		{
			for (U32 i = 0; i < aTries; ++i)
			{
				mLocked = mMutex->try_lock();
				if (mLocked)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
			}
		}
	}

	void unlock()
	{
		if (mMutex && mLocked)
		{
			mMutex->unlock();
			mLocked = false;
		}
	}
	
private:
	LLMutex*	mMutex;
	bool		mLocked;
};

/**
* @class LLScopedLock
* @brief Small class to help lock and unlock mutexes.
*
* The constructor handles the lock, and the destructor handles
* the unlock. Instances of this class are <b>not</b> thread safe.
*/
class LL_COMMON_API LLScopedLock : private boost::noncopyable
{
public:
    /**
    * @brief Constructor which accepts a mutex, and locks it.
    *
    * @param mutex An allocated mutex. If you pass in NULL,
    * this wrapper will not lock.
    */
    LLScopedLock(std::shared_mutex* mutex);

    /**
    * @brief Destructor which unlocks the mutex if still locked.
    */
    ~LLScopedLock();

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
    std::shared_mutex* mMutex;
};

#endif // LL_LLMUTEX_H
