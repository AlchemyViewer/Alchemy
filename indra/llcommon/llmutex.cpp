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
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
	mMutex.lock();
}

void LLMutex::unlock()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
	mMutex.unlock();
}

bool LLMutex::isLocked()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
	if (!mMutex.try_lock())
	{
		return true;
	}
	else
	{
		mMutex.unlock();
		return false;
	}
}

bool LLMutex::try_lock()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
	return mMutex.try_lock();
}

//============================================================================

LLCondition::LLCondition() :
	LLMutex()
{
}


void LLCondition::wait()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
	std::unique_lock<std::recursive_mutex> lock(mMutex);
	mCond.wait(lock);
}

void LLCondition::signal()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
	mCond.notify_one();
}

void LLCondition::broadcast()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD
	mCond.notify_all();
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
