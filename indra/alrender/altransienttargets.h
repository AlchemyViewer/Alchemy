/**
 * @file altransienttargets.h
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

#pragma once

#include "altransienttargetpool.h"

#include <boost/container/small_vector.hpp>

#include <string_view>

/// What one pass or effect takes from an ALTransientTargetPool, and for how long: each target it acquires is the
/// caller's until it is released here or the scope ends, and the pool's to lend again from then on, to the next pass in
/// the same frame as readily as to the next frame.
///
///     ALTransientTargets scope(pool);
///     LLRenderTarget* sharp = scope.acquire(desc, "dof.sharp");
///
/// A target's contents are undefined on acquire: the caller clears it or writes all of it before reading it.
class ALTransientTargets
{
public:
    explicit ALTransientTargets(ALTransientTargetPool& pool) : mPool(pool) {}
    ~ALTransientTargets() { releaseAll(); }

    ALTransientTargets(const ALTransientTargets&)            = delete;
    ALTransientTargets& operator=(const ALTransientTargets&) = delete;

    /// A target that is what `desc` describes, held until the scope ends; null where none can be built. `name` is what
    /// debuggers call it. A target the pool has to build for it leaves texture unit 0 unbound, so a pass acquires what
    /// it needs before it binds. The pool keeps it through `trim_frames` whole frames unacquired after
    /// (ALTransientTargetPool::acquire).
    LLRenderTarget* acquire(const ALRenderTargetDesc& desc, std::string_view name,
                            U32 trim_frames = ALTransientTargetPool::TRIM_FRAMES);
    /// The same with `depth_from`'s depth attached, `desc` describing the colour alone at depth_from's size.
    LLRenderTarget* acquire(const ALRenderTargetDesc& desc, const LLRenderTarget& depth_from, std::string_view name,
                            U32 trim_frames = ALTransientTargetPool::TRIM_FRAMES);

    /// Give `target` back before the scope ends. False, and counted by the pool as a bad release, for a target this
    /// scope does not hold, null included.
    bool release(LLRenderTarget* target);
    /// Give back everything the scope holds.
    void releaseAll();

    bool holds(const LLRenderTarget* target) const;

private:
    LLRenderTarget* held(LLRenderTarget* target);

    /// A target the scope holds, and the lease it was lent under: released by it, so that one the pool took back from
    /// a leak by this scope, and lent again, stays its new holder's.
    struct Held
    {
        LLRenderTarget* mTarget;
        U64             mLease;
    };

    ALTransientTargetPool&                  mPool;
    boost::container::small_vector<Held, 8> mHeld;
};
