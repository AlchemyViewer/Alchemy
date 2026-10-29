/**
 * @file alprobeeclipse.h
 * @brief Whether a placed reflection probe has swallowed an automatic one, for every pair of a frame's at once.
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

#pragma once

#include "llmath.h"
#include "llquaternion.h"
#include "stdtypes.h"
#include "v3math.h"

#include <vector>

/// The placed probes that may evict automatic ones this frame, each as the eclipse test reads it, worked out once: an
/// automatic probe a placed one has swallowed, so that it cannot change a pixel, gives up its cube slot (the reflection
/// map manager's eclipse pass). The pass asks every automatic probe of every placed one, so the test is made cheap
/// first: each placed probe's reach, the farthest an automatic probe's centre may lie from its own and be swallowed at
/// the largest margin asked, rejects all but the pairs that are near, and only those take the shape's own test.
///
/// What swallowed means is the shader's:
/// - A box's influence volume is what the shader tests against, and inside it the shader samples no automatic probe at
///   all, so containment in the box is the whole condition: the automatic probe's bounding sphere clears all three axes
///   of the box, in its own frame, where it is an axis-aligned box of its half-scale.
/// - A sphere does not exclude automatic probes; it blends against them by sphereWeight's dw, and probeMix takes the
///   placed probes alone wherever their dw reaches 1 (class3/deferred/reflectionProbeF.glsl). For a probe faded fully
///   in that is everywhere within sphereFullWeightRadius(), which mirrors dw, and the two have to agree, or this evicts
///   a probe still contributing. Toward the sphere's edge, past that radius, the automatic probes show through, so a
///   sphere merely enclosing one does not hide it: containment within that radius is the condition under which
///   dropping the automatic probe cannot change a pixel.
///
/// A margin is metres of slack added to the placed probe's volume, for hysteresis. Values only; no GL.
class ALProbeEclipse
{
public:
    /// The radius within which a sphere probe of `radius`, faded fully in, weighs fully against the automatic probes:
    /// where sphereWeight's dw, 4 max(r, 1) (1 - max(d - r/2, 0) / (r/2)) / d, is at least 1. That is everywhere within
    /// half the radius, where the attenuation has not begun, and out to 8 m r / (r + 8 m) with m = max(r, 1): eight
    /// ninths of the radius from a metre up.
    static F32 sphereFullWeightRadius(F32 radius);

    /// Forgets the placed probes, keeping the room they took. `max_margin` is the largest margin eclipses() will be
    /// asked with.
    void clear(F32 max_margin);

    /// A box probe at `centre`, turned by `rotation`, of half-extent `half` on its own axes.
    void addBox(const LLVector3& centre, const LLQuaternion& rotation, const LLVector3& half);
    /// A sphere probe at `centre` of `radius`.
    void addSphere(const LLVector3& centre, F32 radius);

    /// Whether any placed probe swallows the automatic probe at `centre` of `radius`, with `margin` (no more than
    /// clear()'s) added to the placed probe's volume.
    bool eclipses(const LLVector3& centre, F32 radius, F32 margin) const;

    size_t size() const { return mEclipsers.size(); }

private:
    struct Eclipser
    {
        LLVector3 mCentre;
        F32       mReachSquared = 0.f; // of the centre an automatic probe's centre must lie within to be swallowed
        LLVector3 mAxes[3];            // the box's axes in the world; a centre's distance along each is its own frame's
        LLVector3 mHalf;               // the box's half-extent
        F32       mInner = 0.f;        // the sphere's fully weighted radius
        bool      mBox = false;
    };

    std::vector<Eclipser> mEclipsers;
    F32                   mMaxMargin = 0.f;
};
