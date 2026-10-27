/**
 * @file _mutex.hpp
 * @brief mutex type abstraction
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
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

#ifndef LLCOREINT_MUTEX_H_
#define LLCOREINT_MUTEX_H_

#include "llcoros.h"
#include LLCOROS_MUTEX_HEADER
#include LLCOROS_CONDVAR_HEADER

namespace LLCoreInt
{

// Requests are queued from coroutines on the main thread, and the worker
// waits on the queue: a coroutine that has to wait for one of these parks
// alone rather than holding up its thread.

// MUTEX TYPES

// unique mutex type
typedef LLCoros::Mutex HttpMutex;

// CONDITION VARIABLES

// standard condition variable
typedef LLCoros::ConditionVariable HttpConditionVariable;

// LOCKS AND FENCES

// scoped unique lock
typedef LLCoros::LockType HttpScopedLock;

}

#endif  // LLCOREINT_MUTEX_H

