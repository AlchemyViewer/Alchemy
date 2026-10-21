/**
 * @file alondemandtarget.cpp
 * @brief A transient target that a run of draws shares, taken from the pool only if one of them draws.
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

#include "alondemandtarget.h"

ALOnDemandTarget::ALOnDemandTarget(ALTransientTargetPool& pool, const std::optional<ALRenderTargetDesc>& desc,
                                   std::string_view name, U32 trim_frames)
: mScope(pool), mDesc(desc), mName(name), mTrimFrames(trim_frames)
{
}

LLRenderTarget* ALOnDemandTarget::bind()
{
    if (mTarget || mDone)
    {
        return mTarget;
    }

    mTarget = mDesc ? mScope.acquire(*mDesc, mName, mTrimFrames) : nullptr;
    if (!mTarget)
    {
        mDone = true;
        return nullptr;
    }
    mTarget->bindTarget();
    mTarget->clear();
    return mTarget;
}

void ALOnDemandTarget::end()
{
    if (mTarget)
    {
        mTarget->flush();
        mScope.release(mTarget);
        mTarget = nullptr;
    }
    mDone = true;
}
