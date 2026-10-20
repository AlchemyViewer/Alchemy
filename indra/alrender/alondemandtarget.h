/**
 * @file alondemandtarget.h
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

#pragma once

#include "altransienttargets.h"

#include <optional>
#include <string_view>

/// A target from the transient pool that a run of draws shares, taken only if one of them draws: acquired, bound and
/// cleared for the first that does, and flushed and given back when the run ends. A run with nothing to draw acquires,
/// binds and clears nothing.
///
///     ALOnDemandTarget target(pool, plan.desc(Id::Preview), "preview");
///     target.drawEach(instances, wants_draw, draw);
class ALOnDemandTarget
{
public:
    /// A target of `desc` named `name`, which outlives the run; without a description, one that cannot be had. The pool
    /// keeps it through `trim_frames` whole frames unacquired after the run (ALTransientTargetPool::acquire).
    ALOnDemandTarget(ALTransientTargetPool& pool, const std::optional<ALRenderTargetDesc>& desc, std::string_view name,
                     U32 trim_frames = ALTransientTargetPool::TRIM_FRAMES);
    /// Ends the run.
    ~ALOnDemandTarget() { end(); }

    ALOnDemandTarget(const ALOnDemandTarget&)            = delete;
    ALOnDemandTarget& operator=(const ALOnDemandTarget&) = delete;

    /// The target, bound: acquired, bound and cleared the first time, what it holds being undefined on acquire. Null
    /// where it cannot be had, and from then on. A target the pool has to build leaves texture unit 0 unbound, so the
    /// first draw binds its textures after this.
    LLRenderTarget* bind();

    /// Each of `items` that `wants(item)` says has something to draw, drawn in order by `draw(item, target)` into the
    /// bound target. `wants` is asked once of each, in order, up to the first that finds no target to be had, which
    /// with the rest waits for another run: false then.
    template <typename Items, typename Wants, typename Draw> bool drawEach(Items&& items, Wants&& wants, Draw&& draw)
    {
        for (auto&& item : items)
        {
            if (!wants(item))
            {
                continue;
            }
            LLRenderTarget* target = bind();
            if (!target)
            {
                return false;
            }
            draw(item, *target);
        }
        return true;
    }

    /// Flush the target where it is bound, and give it back to the pool. The run is over: bind() gives nothing after.
    void end();

private:
    ALTransientTargets                mScope;
    std::optional<ALRenderTargetDesc> mDesc;
    std::string_view                  mName;
    U32                               mTrimFrames;
    LLRenderTarget*                   mTarget = nullptr;
    /// Acquiring failed, or the run ended.
    bool                              mDone = false;
};
