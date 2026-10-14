/**
 * @file almainloop.cpp
 * @brief The queue the main loop services, and a test's say over it.
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

#include "linden_common.h"

#include "almainloop.h"

#include <atomic>

namespace
{
    // Read from the threads that ask whether to hand back, so a choice made
    // on the main thread is seen whole.
    std::atomic<const ALMainLoop::Choice*> sChoice{ nullptr };
}

LL::WorkQueue::ptr_t ALMainLoop::queue()
{
    if (const Choice* choice = sChoice.load(std::memory_order_acquire))
    {
        return choice->mChosen;
    }
    return LL::WorkQueue::getInstance("mainloop");
}

ALMainLoop::Choice::Choice(LL::WorkQueue* chosen)
    : mChosen(chosen ? LL::WorkQueue::getInstance(chosen->getKey()) : nullptr)
    , mOuter(sChoice.load(std::memory_order_relaxed))
{
    sChoice.store(this, std::memory_order_release);
}

ALMainLoop::Choice::~Choice()
{
    sChoice.store(mOuter, std::memory_order_release);
}
