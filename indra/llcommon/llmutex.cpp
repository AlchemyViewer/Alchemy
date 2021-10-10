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

//============================================================================

void LLMutex::lock()
{
	if(isSelfLocked())
	{ //redundant lock
		mCount++;
		return;
	}
	
	mMutex.Lock();
	
#if MUTEX_DEBUG
	// Have to have the lock before we can access the debug info
	auto id = LLThread::currentID();
	if (mIsLocked[id] != FALSE)
		LL_ERRS() << "Already locked in Thread: " << id << LL_ENDL;
	mIsLocked[id] = TRUE;
#endif

	mLockingThread = absl::Hash<std::thread::id>{}(LLThread::currentID());
}

void LLMutex::unlock()
{
	if (mCount > 0)
	{ //not the root unlock
		mCount--;
		return;
	}
	
#if MUTEX_DEBUG
	// Access the debug info while we have the lock
	auto id = LLThread::currentID();
	if (mIsLocked[id] != TRUE)
		LL_ERRS() << "Not locked in Thread: " << id << LL_ENDL;	
	mIsLocked[id] = FALSE;
#endif

	mLockingThread = 0;
	mMutex.Unlock();
}

bool LLMutex::isLocked()
{
	if (!mMutex.TryLock())
	{
		return true;
	}
	else
	{
		mMutex.Unlock();
		return false;
	}
}

bool LLMutex::isSelfLocked()
{
	return mLockingThread == absl::Hash<std::thread::id>{}(LLThread::currentID());
}

bool LLMutex::trylock()
{
	if(isSelfLocked())
	{ //redundant lock
		mCount++;
		return true;
	}
	
	if (!mMutex.TryLock())
	{
		return false;
	}
	
#if MUTEX_DEBUG
	// Have to have the lock before we can access the debug info
	auto id = LLThread::currentID();
	if (mIsLocked[id] != FALSE)
		LL_ERRS() << "Already locked in Thread: " << id << LL_ENDL;
	mIsLocked[id] = TRUE;
#endif

	mLockingThread = absl::Hash<std::thread::id>{}(LLThread::currentID());
	return true;
}

//============================================================================

LLCondition::LLCondition() :
	LLMutex()
{
}


void LLCondition::wait()
{
	mMutex.Lock();
	mCond.Wait(&mMutex);
	mMutex.Unlock();
}

void LLCondition::signal()
{
	mCond.Signal();
}

void LLCondition::broadcast()
{
	mCond.SignalAll();
}

//============================================================================
