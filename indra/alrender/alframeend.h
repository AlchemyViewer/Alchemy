/**
 * @file alframeend.h
 * @brief How a pass of the viewer's display ends its frame.
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

/// How a pass of the viewer's display ends its frame (LLRender::endFrame). Every pass ends one, a hidden window's too,
/// so that what is kept a frame at a time -- the uploads other contexts hand over, the queries, the memory the GPU
/// reads -- moves on whether or not anything was drawn. Only a frame the user was shown is paced, and its end is the
/// main loop's to make once display() has returned (display_end_frame): the work in its slack, then the wait, out of
/// display()'s scope. Every other pass ends its frame unpaced inside display().
struct ALFrameEnd
{
    /// The frames the GPU may still be drawing when the CPU begins the next: 0 for an unpaced end.
    U32  mFramesAhead = 0;
    /// The main loop ends the frame after display() returns, rather than display() itself.
    bool mAfterDisplay = false;

    /// `hidden`: the window shows nothing, so nothing was drawn. `presented`: the world's frame was swapped to the
    /// screen, which a capture holds back. `max_frames_ahead`: AlchemyRenderMaxFramesAhead as latched.
    static constexpr ALFrameEnd make(bool hidden, bool presented, U32 max_frames_ahead)
    {
        ALFrameEnd end;
        if (!hidden && presented && max_frames_ahead > 0)
        {
            end.mFramesAhead  = max_frames_ahead;
            end.mAfterDisplay = true;
        }
        return end;
    }
};
