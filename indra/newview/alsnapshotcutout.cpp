/**
 * @file alsnapshotcutout.cpp
 * @brief One snapshot cutout, as asked for
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "llviewerprecompiledheaders.h"
#include "alsnapshotcutout.h"

#include "alrenderisolation.h"
#include "llviewercontrol.h"
#include "rlvactions.h"

// static
bool ALSnapshotCutout::blockedByRlv()
{
    return RlvActions::hasBehaviour(RLV_BHVR_SETSPHERE) || RlvActions::hasBehaviour(RLV_BHVR_SETOVERLAY);
}

// static
std::optional<ALSnapshotCutout> ALSnapshotCutout::fromSettings(LLSnapshotModel::ESnapshotType type,
                                                               LLSnapshotModel::ESnapshotFormat format)
{
    static LLCachedControl<bool>     enabled(gSavedSettings, "ALSnapshotCutout", false);
    static LLCachedControl<bool>     include_seated(gSavedSettings, "ALSnapshotCutoutIncludeSeated", false);
    static LLCachedControl<LLColor4> fill(gSavedSettings, "ALSnapshotCutoutFill", LLColor4::transparent);
    static LLCachedControl<U32>      supersample(gSavedSettings, "ALSnapshotCutoutSupersample", 2);

    if (!enabled() || ALRenderIsolation::armedTarget().isNull() || blockedByRlv())
    {
        return std::nullopt;
    }

    ALSnapshotCutout cutout;
    cutout.mTarget = ALRenderIsolation::armedTarget();
    cutout.mIncludeSeated = include_seated();
    cutout.mFill = fill();
    cutout.mKeepAlpha = LLSnapshotModel::keepsAlpha(type, format);
    cutout.mSupersample = llclamp(supersample(), 1u, 4u);
    return cutout;
}

bool ALSnapshotCutout::drawsSameAs(const ALSnapshotCutout& other) const
{
    return mTarget == other.mTarget
        && mIncludeSeated == other.mIncludeSeated
        && mSupersample == other.mSupersample;
}

bool ALSnapshotCutout::operator==(const ALSnapshotCutout& other) const
{
    return drawsSameAs(other)
        && mFill == other.mFill
        && mKeepAlpha == other.mKeepAlpha;
}
