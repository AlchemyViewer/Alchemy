/**
 * @file alrenderinputrequests.cpp
 * @brief What the render inputs' changes since they were last applied ask of the next application, and what it does.
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

#include <numeric>

void ALRenderInputRequests::mark(Change change)
{
    ++mMarks[(size_t)change];
}

void ALRenderInputRequests::overrideBegins()
{
    ++mOverrides;
}

void ALRenderInputRequests::overrideEnds()
{
    llassert(mOverrides > 0);
    mOverrides -= mOverrides > 0 ? 1 : 0;
    mark(Change::Inputs);
}

bool ALRenderInputRequests::pending() const
{
    return !overriding() && std::accumulate(mMarks.begin(), mMarks.end(), 0u) > 0;
}

bool ALRenderInputRequests::windowResized() const
{
    return !overriding() && mMarks[(size_t)Change::Window] > 0;
}

std::optional<ALRenderInputRequests::Application> ALRenderInputRequests::take()
{
    if (!pending())
    {
        return std::nullopt;
    }

    Application application;
    application.mRebuildPrograms = mMarks[(size_t)Change::Programs] > 0;
    application.mSkipFrame       = mMarks[(size_t)Change::Window] > 0;
    application.mChanges         = std::accumulate(mMarks.begin(), mMarks.end(), 0u);

    mMarks = {};
    return application;
}

// static
ALRenderApplication ALRenderApplication::make(bool rebuild_programs, const std::optional<ALShaderInputs>& loaded,
                                              const ALShaderInputs& latched,
                                              const std::optional<ALRenderResourceInputs>& reconciled,
                                              const ALRenderResourceInputs& requested)
{
    ALRenderApplication application;
    if (rebuild_programs || !loaded)
    {
        application.mPrograms      = Programs::Rebuilt;
        application.mSharedObjects = true;
    }
    else if (latched != *loaded)
    {
        application.mPrograms      = Programs::Reloaded;
        application.mSharedObjects = latched.mGlobal != loaded->mGlobal;
    }
    // The resources' inputs carry the normal layout and the depth convention the programs compile for, so a reload that
    // moves either reconciles after it.
    application.mReconcile = !reconciled || requested != *reconciled;
    return application;
}
