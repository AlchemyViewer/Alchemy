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

#include "absl/synchronization/mutex.h"

#include <mutex>
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
	LLMutex();
	virtual ~LLMutex() = default;
	
	void lock();		// blocks
	bool trylock();		// non-blocking, returns true if lock held.
	void unlock();		// undefined behavior when called on mutex not being held
	bool isLocked(); 	// non-blocking, but does do a lock/unlock so not free
	bool isSelfLocked(); //return true if locked in a same thread
	LLThread::id_t lockingThread() const; //get ID of locking thread

protected:
	std::mutex			mMutex;
	mutable U32			mCount;
	mutable LLThread::id_t	mLockingThread;
	
#if MUTEX_DEBUG
	std::map<LLThread::id_t, BOOL> mIsLocked;
#endif
};

// Actually a condition/mutex pair (since each condition needs to be associated with a mutex).
class LL_COMMON_API LLCondition : public LLMutex
{
public:
	LLCondition();
	~LLCondition() = default;
	
	void wait();		// blocks
	void signal();
	void broadcast();
	
protected:
	std::condition_variable mCond;
};

class LLMutexLock
{
public:
	LLMutexLock(LLMutex* mutex)
	{
		mMutex = mutex;
		
		if(mMutex)
			mMutex->lock();
	}
	~LLMutexLock()
	{
		unlock();
	}
	void unlock()
	{
		if (mMutex)
			mMutex->unlock();
	}
private:
	LLMutex* mMutex;
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
	LLMutexTrylock(LLMutex* mutex);
	LLMutexTrylock(LLMutex* mutex, U32 aTries, U32 delay_ms = 10);
	~LLMutexTrylock();

	bool isLocked() const
	{
		return mLocked;
	}
	
private:
	LLMutex*	mMutex;
	bool		mLocked;
};


// This is here due to include order issues wrt llmutex.h and lockstatic.h
namespace llthread
{
	template <typename Static>
	class LockStaticLL
	{
		typedef LLMutexLock lock_t;
	public:
		LockStaticLL() :
			mData(getStatic()),
			mLock(&mData->mMutex)
		{}
		Static* get() const { return mData; }
		operator Static* () const { return get(); }
		Static* operator->() const { return get(); }
		// sometimes we must explicitly unlock...
		void unlock()
		{
			// but once we do, access is no longer permitted
			mData = nullptr;
			mLock.unlock();
		}
	protected:
		Static* mData;
		lock_t mLock;
	private:
		Static* getStatic()
		{
			// Static::mMutex must be function-local static rather than class-
			// static. Some of our consumers must function properly (therefore
			// lock properly) even when the containing module's static variables
			// have not yet been runtime-initialized. A mutex requires
			// construction. A static class member might not yet have been
			// constructed.
			//
			// We could store a dumb mutex_t*, notice when it's NULL and allocate a
			// heap mutex -- but that's vulnerable to race conditions. And we can't
			// defend the dumb pointer with another mutex.
			//
			// We could store a std::atomic<mutex_t*> -- but a default-constructed
			// std::atomic<T> does not contain a valid T, even a default-constructed
			// T! Which means std::atomic, too, requires runtime initialization.
			//
			// But a function-local static is guaranteed to be initialized exactly
			// once: the first time control reaches that declaration.
			static Static sData;
			return &sData;
		}
	};
}

class AbslMutexMaybeTrylock
{
public:
	AbslMutexMaybeTrylock(absl::Mutex* mutex)
		: mMutex(mutex),
		mLocked(false)
	{
		if (mMutex)
			mLocked = mMutex->TryLock();
	}

	AbslMutexMaybeTrylock(absl::Mutex* mutex, U32 aTries, U32 delay_ms = 10)
		: mMutex(mutex),
		mLocked(false)
	{
		if (!mMutex)
			return;

		for (U32 i = 0; i < aTries; ++i)
		{
			mLocked = mMutex->TryLock();
			if (mLocked)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
		}
	}

	~AbslMutexMaybeTrylock()
	{
		if (mMutex && mLocked)
			mMutex->Unlock();
	}

	bool isLocked() const
	{
		return mLocked;
	}

private:
	absl::Mutex* mMutex;
	bool		mLocked;
};

#endif // LL_LLMUTEX_H
