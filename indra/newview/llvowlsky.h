/**
 * @file llvowlsky.h
 * @brief LLVOWLSky class definition
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_VOWLSKY_H
#define LL_VOWLSKY_H

#include "llviewerobject.h"

#include "alskylayers.h"
#include "alstarfield.h"

#include <boost/signals2/connection.hpp>

class LLVOWLSky : public LLStaticViewerObject {
public:
    LLVOWLSky(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp);

    /*virtual*/ void         idleUpdate(LLAgent &agent, const F64 &time);
    /*virtual*/ bool         isActive(void) const;
    /*virtual*/ LLDrawable * createDrawable(LLPipeline *pipeline);
    /*virtual*/ bool         updateGeometry(LLDrawable *drawable);

    // The stars drawn as points, those of the cells in view through @a frustum, and the spiked stars as quads
    // (ALStarField::Layout), each with its own program bound; only those at @a faintest or brighter
    // (ALStarField::faintestVisible).
    void drawStarPoints(const ALStarField::Frustum& frustum, U32 faintest);
    void drawStarQuads(U32 faintest);
    void resetVertexBuffers(void);

    // Meteors: CPU-side simulation of short-lived streaks. Tick once per frame in a view that draws them, then hand
    // their streaks to the sky's composite (LLDrawPoolWLSky::bindComposite).
    void tickMeteors(F32 dt_seconds);

    // The meteors in flight, as the sky's composite takes them (ALSkyLayers::packMeteors): each one's streak about the
    // camera as its envelope has carried it this far, its colour times that envelope, and its width class.
    void meteors(std::vector<ALSkyLayers::Meteor>& out) const;

    void cleanupGL();
    void restoreGL();

private:

    // helper function for initializing the stars.
    void initStars();

    // helper function for updating the stars geometry.
    bool updateStarGeometry(LLDrawable *drawable);

private:
    LLPointer<LLVertexBuffer>                   mStarsVerts;

    // The star field RenderStarCount asked for, in the order the buffer takes it, and where each kind lies in it.
    std::vector<ALStarField::Star> mStars;
    ALStarField::Layout            mStarLayout;
    ALStarField::Draws             mStarDraws; // the frame's runs of points, kept for their storage

    struct MeteorState
    {
        LLVector3 origin_world;        // starting head position
        LLVector3 direction_world;     // unit direction of travel
        F32       path_length_world;   // distance head travels across its lifetime
        F32       trail_length_world;  // length of the glowing trail behind the head
        F32       age;                 // seconds since spawn
        F32       lifetime;            // total seconds to live
        LLColor3  color;               // sRGB color
        F32       peak_intensity;      // 0..1 scale on the envelope peak
        F32       width_scale;         // per-meteor thickness multiplier (0..4, packed to color.a)
    };
    std::vector<MeteorState> mMeteors;

    // Connection that re-runs star generation when RenderStarCount changes.
    boost::signals2::scoped_connection mStarCountConnection;
};

#endif // LL_VOWLSKY_H
