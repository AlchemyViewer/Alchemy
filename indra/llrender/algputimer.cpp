/**
 * @file algputimer.cpp
 * @brief The GPU time a span of draws takes, read back without waiting on the GPU.
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

#include "algputimer.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"

ALGPUTimer::~ALGPUTimer()
{
    release();
}

bool ALGPUTimer::begin()
{
    if (mOpen || mPending)
    {
        return false;
    }
    if (mQuery == 0)
    {
        glGenQueries(1, &mQuery);
    }
    // Draws still batched in gGL were issued before the span.
    gGL.flush();
    glBeginQuery(GL_TIME_ELAPSED, mQuery);
    mOpen = true;
    return true;
}

void ALGPUTimer::end()
{
    if (mOpen)
    {
        gGL.flush();
        glEndQuery(GL_TIME_ELAPSED);
        mOpen    = false;
        mPending = true;
    }
}

bool ALGPUTimer::poll(U64& nanoseconds, bool wait)
{
    if (!mPending)
    {
        return false;
    }
    if (!wait)
    {
        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(mQuery, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
        {
            return false;
        }
    }
    GLuint64 elapsed = 0;
    glGetQueryObjectui64v(mQuery, GL_QUERY_RESULT, &elapsed);
    nanoseconds = elapsed;
    mPending    = false;
    return true;
}

void ALGPUTimer::release()
{
    end();
    if (mQuery != 0)
    {
        glDeleteQueries(1, &mQuery);
        mQuery = 0;
    }
    mPending = false;
}
