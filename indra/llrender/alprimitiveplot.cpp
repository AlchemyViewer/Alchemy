/**
 * @file alprimitiveplot.cpp
 * @brief The primitives a set of draws generates, plotted once a frame.
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

#include "alprimitiveplot.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"

void ALPrimitivePlot::plot()
{
#if LL_PROFILER_CONFIGURATION >= LL_PROFILER_CONFIG_TRACY
    // The slot after the last is the oldest, and becomes this frame's.
    mFrame = (mFrame + 1) % FRAMES;
    Frame& frame = mFrames[mFrame];
    if (frame.mCounted)
    {
        const ALQueryPool& queries = gGL.queries();
        int64_t total = 0;
        bool    ready = true;
        for (U32 i = 0; i < frame.mSpans && ready; ++i)
        {
            U64 count = 0;
            ready = queries.result(frame.mQueries[i], count) == ALQueryPool::Status::Ready;
            total += (int64_t)count;
        }
        if (ready)
        {
            LL_PROFILE_PLOT(mName, total);
        }
    }
    frame.mSpans   = 0;
    frame.mCounted = TracyIsConnected && gGLManager.canQueryPrimitives(mTessellated);
#endif
}

void ALPrimitivePlot::begin()
{
#if LL_PROFILER_CONFIGURATION >= LL_PROFILER_CONFIG_TRACY
    Frame& frame = mFrames[mFrame];
    if (!frame.mCounted || frame.mSpans == SPANS)
    {
        return;
    }
    mOpen = gGL.queries().begin(ALQueryKind::Primitives);
    if (!mOpen.isValid())
    {
        // The frame's total would be short a span.
        frame.mCounted = false;
        return;
    }
    frame.mQueries[frame.mSpans++] = mOpen;
#endif
}

void ALPrimitivePlot::end()
{
    gGL.queries().end(mOpen);
    mOpen = {};
}

void ALPrimitivePlot::release()
{
    // A span still open would hold the pool's primitives target.
    end();
    for (Frame& frame : mFrames)
    {
        frame = Frame();
    }
    mFrame = 0;
}
