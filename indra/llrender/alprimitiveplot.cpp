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
#include "llglslshader.h"
#include "llrender.h"

void ALPrimitivePlot::plot()
{
#if LL_PROFILER_CONFIGURATION >= LL_PROFILER_CONFIG_TRACY
    // The slot after the last is the oldest, and becomes this frame's.
    mFrame = (mFrame + 1) % FRAMES;
    Frame& frame = mFrames[mFrame];
    if (frame.mCounted)
    {
        int64_t total = 0;
        bool    ready = true;
        for (U32 i = 0; i < frame.mSpans && ready; ++i)
        {
            GLuint available = 0;
            glGetQueryObjectuiv(frame.mQueries[i], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available)
            {
                GLuint count = 0;
                glGetQueryObjectuiv(frame.mQueries[i], GL_QUERY_RESULT, &count);
                total += count;
            }
            ready = available != 0;
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
    if (LLGLSLShader::sProfileEnabled)
    {
        frame.mCounted = false;
    }
    if (!frame.mCounted || frame.mSpans == SPANS)
    {
        return;
    }
    if (frame.mQueries[0] == 0)
    {
        glGenQueries(SPANS, frame.mQueries);
    }
    gGL.flush();
    glBeginQuery(GL_PRIMITIVES_GENERATED, frame.mQueries[frame.mSpans++]);
    mActive = true;
#endif
}

void ALPrimitivePlot::end()
{
    if (mActive)
    {
        gGL.flush();
        glEndQuery(GL_PRIMITIVES_GENERATED);
        mActive = false;
    }
}

void ALPrimitivePlot::release()
{
    end();
    for (Frame& frame : mFrames)
    {
        if (frame.mQueries[0] != 0)
        {
            glDeleteQueries(SPANS, frame.mQueries);
        }
        frame = Frame();
    }
    mFrame = 0;
}
