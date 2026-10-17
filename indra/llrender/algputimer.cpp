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

#include "llrender.h"

ALQueryPool& ALGPUTimer::pool() const
{
    return mPool ? *mPool : gGL.queries();
}

bool ALGPUTimer::begin()
{
    if (mOpen || mPending)
    {
        return false;
    }
    mSpan = pool().beginSpan();
    mOpen = mSpan.isValid();
    return mOpen;
}

void ALGPUTimer::end()
{
    if (mOpen)
    {
        pool().endSpan(mSpan);
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
    switch (pool().result(mSpan, nanoseconds))
    {
    case ALQueryPool::Status::Ready:
        mPending = false;
        return true;
    case ALQueryPool::Status::Expired:
        mPending = false;
        return false;
    case ALQueryPool::Status::Pending:
    default:
        if (!wait)
        {
            return false;
        }
        mPending = false;
        return pool().wait(mSpan, nanoseconds);
    }
}
