/**
 * @file alrenderinputrequests.cpp
 * @brief What the render inputs' changes since they were last applied ask of the next application.
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

#include "alrenderinputrequests.h"

#include <algorithm>
#include <numeric>

void ALRenderInputRequests::mark(Change change)
{
    ++mMarks[(size_t)change];
}

bool ALRenderInputRequests::pending() const
{
    return mMarks[(size_t)Change::Resources] > 0 || mMarks[(size_t)Change::Shaders] > 0 ||
           mMarks[(size_t)Change::Window] > 0;
}

bool ALRenderInputRequests::windowResized() const
{
    return mMarks[(size_t)Change::Window] > 0;
}

ALRenderInputRequests::Application ALRenderInputRequests::take(bool inputs_changed)
{
    Application application;
    application.mReloadShaders = mMarks[(size_t)Change::Shaders] > 0;
    application.mReconcile     = !application.mReloadShaders && inputs_changed &&
                                 (mMarks[(size_t)Change::Resources] > 0 || mMarks[(size_t)Change::Window] > 0);
    application.mSkipFrame     = mMarks[(size_t)Change::Window] > 0;
    application.mChanges       = std::accumulate(mMarks.begin(), mMarks.end(), 0u);

    mMarks = {};
    return application;
}

void ALRenderInputRequests::reloadBegins()
{
    mReloading = mMarks;
}

void ALRenderInputRequests::shadersReloaded()
{
    // An application taken while the reload ran would leave fewer marks than it began with.
    for (Change change : { Change::Shaders, Change::Resources })
    {
        U32& marks = mMarks[(size_t)change];
        marks -= std::min(marks, mReloading[(size_t)change]);
    }
    mReloading = {};
}
