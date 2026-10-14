/**
 * @file almainloop.h
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

#pragma once

#include "workqueue.h"

// The queue the viewer's main loop services: what work done on another
// thread hands its result back through. Code that works off the main
// thread asks here rather than for the queue by its name, so that a test
// can say which way that code goes.
namespace ALMainLoop
{
    // The main loop's queue, or null where nothing services one -- a test
    // that has not chosen one -- and the work is done in place instead.
    LL_COMMON_API LL::WorkQueue::ptr_t queue();

    // While one lives, queue() answers the queue it was given, or null for
    // none, whatever queue of the main loop's name there is. Without it a
    // queue one test made would choose the way for every test after it in
    // the process. The newest living one wins. Main thread only.
    class LL_COMMON_API Choice
    {
    public:
        explicit Choice(LL::WorkQueue* chosen);
        ~Choice();

        Choice(const Choice&)            = delete;
        Choice& operator=(const Choice&) = delete;

    private:
        friend LL::WorkQueue::ptr_t queue();

        LL::WorkQueue::ptr_t mChosen;
        const Choice*        mOuter;
    };
}
