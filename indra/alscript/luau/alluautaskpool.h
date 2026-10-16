/**
 * @file alluautaskpool.h
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

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// A few threads of the analysis's own, each taking the next task as it is
// free: what Luau's front end hands over as it checks a script's modules
// together (Luau::Frontend::checkQueuedModules), each module as soon as the
// modules it requires are checked. Luau waits for its own tasks; this only
// runs them. Its threads end with it, once their tasks are done.
class ALLuauTaskPool
{
public:
    explicit ALLuauTaskPool(unsigned threads);
    ~ALLuauTaskPool();
    ALLuauTaskPool(const ALLuauTaskPool&)            = delete;
    ALLuauTaskPool& operator=(const ALLuauTaskPool&) = delete;

    // Each task run on whichever thread is free first; returns at once.
    void run(std::vector<std::function<void()>> tasks);

    // How many threads one has here: a few, leaving the machine the rest.
    static unsigned threadsWanted();

private:
    void work();

    std::mutex                        mMutex;
    std::condition_variable           mReady;
    std::deque<std::function<void()>> mTasks;
    bool                              mStopping = false;
    std::vector<std::thread>          mThreads;
};
