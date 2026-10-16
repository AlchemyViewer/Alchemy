/**
 * @file alserialworker.cpp
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

#include "linden_common.h"

#include "alserialworker.h"

#include "threadpool.h"

ALSerialWorker::ALSerialWorker(std::string name, std::function<void()> stop)
:   mName(std::move(name)),
    mStop(std::move(stop))
{
}

ALSerialWorker::~ALSerialWorker()
{
    close();
}

bool ALSerialWorker::post(std::function<void()> job)
{
    if (mClosing)
    {
        return false;
    }
    if (!mPool)
    {
        // One thread whatever ThreadPoolSizes says, and not let go of by
        // the pool's own hearing of the viewer quitting, which would run
        // everything waiting first: this hears it instead.
        mPool = std::make_unique<LL::ThreadPool>(mName, 1, 1024 * 1024, /*auto_shutdown*/ false, /*fixed_width*/ true);
        mPool->start();
        mQuitting = LLEventPumps::instance().obtain("LLApp").listen("ALSerialWorker:" + mName, [this](const LLSD& stat) {
            if (stat["status"].asString() != "running")
            {
                close();
            }
            return false;
        });
    }
    return mPool->getQueue().post([this, job = std::move(job)]() {
        // Waiting when the worker closed: passed over.
        if (!mClosing)
        {
            job();
        }
    });
}

void ALSerialWorker::close()
{
    if (!mClosing.exchange(true) && mStop)
    {
        mStop();
    }
    if (mPool)
    {
        mPool->close();
    }
}
