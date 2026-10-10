/**
 * @file alprimitiveplot.h
 * @brief The primitives a set of draws generates, plotted once a frame.
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

#include "stdtypes.h"

#include <vector>

/// The primitives a set of draws generates in a frame, summed and plotted as one Tracy track. Each span of draws between
/// begin() and end() runs inside its own GL_PRIMITIVES_GENERATED query. plot(), once a frame before the frame's first
/// span, reads the spans the frame before issued, by which point their results are in, so the readback never waits on
/// the GPU: a frame whose results are not all in is left out of the track, and a frame that issued none plots 0. A
/// frame with more spans than the ring holds draws the rest uncounted. Without Tracy nothing is issued.
class ALPrimitivePlot
{
public:
    /// `name` must be a string literal, as for LL_PROFILE_PLOT. `tessellated` says the spans draw patches, which not
    /// every driver can count (LLGLManager::canQueryPrimitives).
    ALPrimitivePlot(const char* name, bool tessellated) : mName(name), mTessellated(tessellated) {}

    /// Plots what the spans issued since the last call drew.
    void plot();

    /// Starts counting a span of draws.
    void begin();

    /// Ends the span begin() started, if it started one.
    void end();

    /// Deletes the queries. Called while the context they were made in is current, before it goes.
    void release();

private:
    static constexpr U32 RING = 32;

    const char*         mName;
    bool                mTessellated;
    U32                 mQueries[RING] = {};
    std::vector<U32>    mIssued;
    U32                 mNext   = 0;
    bool                mActive = false;
};
