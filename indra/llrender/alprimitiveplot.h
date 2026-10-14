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

/// The primitives a set of draws generates in a frame, summed and plotted as one Tracy track. Each span of draws between
/// begin() and end() runs inside its own GL_PRIMITIVES_GENERATED query. plot(), once a frame before the frame's first
/// span, reads the spans issued FRAMES - 1 frames before the last, by which point a GPU queuing a few frames behind has
/// their results in, so the readback never waits on it: a frame whose results are not all in is left out of the track,
/// and a frame that issued none plots 0. A frame with more spans than SPANS draws the rest uncounted. Nothing is issued
/// without Tracy, while no profiler is connected, or while the shader profiler holds the query target for the programs
/// it binds (LLGLSLShader::placeProfileQuery).
class ALPrimitivePlot
{
public:
    /// `name` must be a string literal, as for LL_PROFILE_PLOT. `tessellated` says the spans draw patches, which not
    /// every driver can count (LLGLManager::canQueryPrimitives).
    ALPrimitivePlot(const char* name, bool tessellated) : mName(name), mTessellated(tessellated) {}

    /// Plots what the spans of the oldest frame held drew, and starts a frame.
    void plot();

    /// Starts counting a span of draws.
    void begin();

    /// Ends the span begin() started, if it started one.
    void end();

    /// Deletes the queries. Called while the context they were made in is current, before it goes.
    void release();

private:
    static constexpr U32 FRAMES = 4;
    static constexpr U32 SPANS  = 8;

    // A frame's spans: counted when the frame could issue them, so that one which issued none plots 0.
    struct Frame
    {
        U32  mQueries[SPANS] = {};
        U32  mSpans          = 0;
        bool mCounted        = false;
    };

    const char* mName;
    bool        mTessellated;
    Frame       mFrames[FRAMES];
    U32         mFrame  = 0;
    bool        mActive = false;
};
