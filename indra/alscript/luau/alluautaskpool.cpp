/**
 * @file alluautaskpool.cpp
 * @brief A few threads Luau checks a script's modules on, each as soon as what it requires is checked.
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

#include "alluautaskpool.h"

#include "alscriptstack.h"

#include <algorithm>

ALLuauTaskPool::ALLuauTaskPool(unsigned threads)
{
    for (unsigned i = 0; i < std::max(threads, 1u); ++i)
    {
        mThreads.emplace_back([this]() { work(); });
    }
}

ALLuauTaskPool::~ALLuauTaskPool()
{
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        mStopping = true;
    }
    mReady.notify_all();
    for (std::thread& thread : mThreads)
    {
        thread.join();
    }
}

void ALLuauTaskPool::run(std::vector<std::function<void()>> tasks)
{
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        for (std::function<void()>& task : tasks)
        {
            mTasks.push_back(std::move(task));
        }
    }
    mReady.notify_all();
}

// static
unsigned ALLuauTaskPool::threadsWanted()
{
    constexpr unsigned MOST = 3;
    const unsigned     have = std::thread::hardware_concurrency();
    return std::clamp(have > 1 ? have - 1 : 1u, 1u, MOST);
}

void ALLuauTaskPool::work()
{
    LL_PROFILER_SET_THREAD_NAME("Luau modules");
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mReady.wait(lock, [this]() { return mStopping || !mTasks.empty(); });
            if (mTasks.empty())
            {
                return;
            }
            task = std::move(mTasks.front());
            mTasks.pop_front();
        }
        // A module's check and lint recurse on how it nests, as the
        // script's own do on the lane, so on as deep a stack: this thread's
        // is the platform's, half a megabyte on macOS.
        alScriptOnLargeStack(task);
    }
}
