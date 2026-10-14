/**
 * @file alsamplesquery.cpp
 * @brief Whether any sample of a set of draws passed, and draws that run only if one did.
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

#include "alsamplesquery.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"

void ALSamplesQuery::begin()
{
    llassert(!mActive);
    if (mQuery == 0)
    {
        glGenQueries(1, &mQuery);
        // A query object keeps the target it was first begun with.
        mTarget = gGLManager.mGLVersion >= 4.29f ? GL_ANY_SAMPLES_PASSED_CONSERVATIVE : GL_ANY_SAMPLES_PASSED;
    }
    gGL.flush();
    glBeginQuery(mTarget, mQuery);
    mActive = true;
}

void ALSamplesQuery::end()
{
    if (mActive)
    {
        gGL.flush();
        glEndQuery(mTarget);
        mActive = false;
    }
}

void ALSamplesQuery::release()
{
    end();
    if (mQuery != 0)
    {
        glDeleteQueries(1, &mQuery);
        mQuery = 0;
    }
}

ALSamplesQuery::Conditional::Conditional(const ALSamplesQuery& query)
{
    llassert(!query.mActive);
    if (query.mQuery != 0 && !query.mActive)
    {
        gGL.flush();
        glBeginConditionalRender(query.mQuery, GL_QUERY_WAIT);
        mConditional = true;
    }
}

ALSamplesQuery::Conditional::~Conditional()
{
    if (mConditional)
    {
        gGL.flush();
        glEndConditionalRender();
    }
}
