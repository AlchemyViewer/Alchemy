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

#include <algorithm>
#include <iterator>

void ALPrimitivePlot::plot()
{
#if LL_PROFILER_CONFIGURATION >= LL_PROFILER_CONFIG_TRACY
    int64_t total = 0;
    for (U32 query : mIssued)
    {
        GLuint available = 0;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
        {
            mIssued.clear();
            return;
        }
        GLuint count = 0;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &count);
        total += count;
    }
    mIssued.clear();
    LL_PROFILE_PLOT(mName, total);
#endif
}

void ALPrimitivePlot::begin()
{
#if LL_PROFILER_CONFIGURATION >= LL_PROFILER_CONFIG_TRACY
    if (!gGLManager.canQueryPrimitives(mTessellated) || mIssued.size() == RING)
    {
        return;
    }
    if (mQueries[0] == 0)
    {
        glGenQueries(RING, mQueries);
    }
    const U32 query = mQueries[mNext];
    mNext = (mNext + 1) % RING;
    glBeginQuery(GL_PRIMITIVES_GENERATED, query);
    mIssued.push_back(query);
    mActive = true;
#endif
}

void ALPrimitivePlot::end()
{
    if (mActive)
    {
        glEndQuery(GL_PRIMITIVES_GENERATED);
        mActive = false;
    }
}

void ALPrimitivePlot::release()
{
    end();
    if (mQueries[0] != 0)
    {
        glDeleteQueries(RING, mQueries);
        std::fill(std::begin(mQueries), std::end(mQueries), 0);
    }
    mIssued.clear();
    mNext = 0;
}
