/**
 * @file altransienttargets.cpp
 * @brief What one pass or effect takes from the transient target pool, held until the scope ends.
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

#include "altransienttargets.h"

#include <algorithm>

LLRenderTarget* ALTransientTargets::acquire(const ALRenderTargetDesc& desc, std::string_view name, U32 trim_frames)
{
    return held(mPool.acquire(desc, name, trim_frames));
}

LLRenderTarget* ALTransientTargets::acquire(const ALRenderTargetDesc& desc, const LLRenderTarget& depth_from,
                                            std::string_view name, U32 trim_frames)
{
    return held(mPool.acquire(desc, depth_from, name, trim_frames));
}

bool ALTransientTargets::release(LLRenderTarget* target)
{
    const auto found =
        std::find_if(mHeld.begin(), mHeld.end(), [target](const Held& held) { return held.mTarget == target; });
    if (!target || found == mHeld.end())
    {
        mPool.noteBadRelease(target);
        return false;
    }
    const U64 lease = found->mLease;
    mHeld.erase(found);
    return mPool.release(target, lease);
}

void ALTransientTargets::releaseAll()
{
    // Last acquired first, as nested lifetimes end.
    while (!mHeld.empty())
    {
        const Held held = mHeld.back();
        mHeld.pop_back();
        mPool.release(held.mTarget, held.mLease);
    }
}

bool ALTransientTargets::holds(const LLRenderTarget* target) const
{
    return target &&
           std::any_of(mHeld.begin(), mHeld.end(), [target](const Held& held) { return held.mTarget == target; });
}

LLRenderTarget* ALTransientTargets::held(LLRenderTarget* target)
{
    if (target)
    {
        mHeld.push_back({ target, mPool.lease(target) });
    }
    return target;
}
