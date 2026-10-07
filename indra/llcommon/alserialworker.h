/**
 * @file alserialworker.h
 * @brief One thread for work that keeps its state between jobs, which the
 *        viewer quitting stops at once.
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

#include "llevents.h"
#include "llpreprocessor.h"
#include "threadpool_fwd.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// One thread that runs jobs one after another, for work that keeps state
// between them and is not safe on two threads at once: the script
// analysers, the preprocessor. Its width is one whatever the
// ThreadPoolSizes setting says.
//
// The viewer starting to quit closes it at once -- what waits is passed
// over as it is reached, not run -- where a thread pool of LL's runs
// everything waiting before it lets its thread go. The owner's `stop`,
// where it gives one, is called first, to stop the job that is running.
//
// Posted to and closed from one thread, the main one; the thread starts
// with the first job. A job may post another from the thread itself, which
// started with the first: closed, that is refused like any other.
class LL_COMMON_API ALSerialWorker
{
public:
    ALSerialWorker(std::string name, std::function<void()> stop = nullptr);
    ~ALSerialWorker();
    ALSerialWorker(const ALSerialWorker&)            = delete;
    ALSerialWorker& operator=(const ALSerialWorker&) = delete;

    // Run on the thread, after the jobs posted before it. False, and
    // nothing run, once it is closed: the caller answers for the job.
    bool post(std::function<void()> job);

    // Whether it is closing, or closed: what a long job looks at between
    // its parts, to give up.
    bool closing() const { return mClosing; }

    // No more jobs taken, and those waiting passed over; the owner's stop
    // called, then the thread let go of once the running job returns.
    // Once closed, it stays closed.
    void close();

private:
    std::string                     mName;
    std::function<void()>           mStop;
    std::atomic<bool>               mClosing{ false };
    std::unique_ptr<LL::ThreadPool> mPool;
    LLTempBoundListener             mQuitting;
};
