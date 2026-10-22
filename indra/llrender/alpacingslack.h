/**
 * @file alpacingslack.h
 * @brief How long paced frames' ends wait for the GPU, and how much of that the work before the next wait may take.
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

#include <algorithm>

/// The slack of the frames whose ends pace the CPU (ALFrameFences::endFrame), and the share of it that work may take
/// before the next one's wait. GPU-bound, a paced frame's end waits about as long as the GPU's frame outlasts the CPU's,
/// every frame, time the main loop's queued work could have had. Whether the GPU is still drawing cannot be asked
/// between work items: that is a poll, which a threaded driver answers only once its own thread has caught up. So the
/// time is predicted from the frames before: the budget is a share of the least slack of the last FRAMES paced frames.
/// Work that runs past the slack delays the frame by as much; the share, and the reserve within which no work item
/// begins, bound that.
///
/// A frame's slack is the work done before its wait and the wait together, so that the work does not shrink the next
/// budget. A wait that found the GPU done shows no slack: the work before it may have delayed the frame. The budget is
/// then 0 until FRAMES frames have waited again, and so it is after a frame's end that paced nothing. No GL, and the
/// clock is the caller's.
class ALPacingSlack
{
public:
    /// The paced frames whose least slack the budget is a share of.
    static constexpr U32 FRAMES = 8;
    /// The share of that slack the budget is.
    static constexpr F64 SHARE = 0.5;
    /// The frames' ends whose longest work item the reserve covers.
    static constexpr U32 ITEM_FRAMES = 32;
    /// The least reserve, in seconds: what a work item is taken to cost before one has been timed.
    static constexpr F64 RESERVE_FLOOR = 0.0005;

    /// The seconds of work the next paced frame's end may have before its wait, the last item's to its end: SHARE of
    /// the least slack of the last FRAMES paced frames.
    F64 budget() const;
    /// The seconds before the budget runs out within which no work item begins, since an item's time is known only
    /// once it has run: the longest item of the last ITEM_FRAMES frames' ends, RESERVE_FLOOR at the least.
    F64 reserve() const;

    /// Runs work until the budget less the reserve has gone: `run_one()` runs one item and says whether there was one,
    /// and `now()` reads a steady clock in seconds. Each item is timed for the reserve, and the time the work took
    /// counts toward the slack of the frame whose end comes next. The seconds it ran.
    template <typename RUN_ONE, typename NOW>
    F64 fill(RUN_ONE&& run_one, NOW&& now);

    /// A paced frame's end: its wait took `waited` seconds, and `behind` says whether the GPU was still drawing the
    /// frame it waited for. A frame's end that had no frame to wait for found the GPU done.
    void endPaced(F64 waited, bool behind);
    /// A frame's end that paced nothing: the slack of the frames before says nothing of the next paced one's.
    void endUnpaced();

private:
    // Notes the ending frame's longest work item for the reserve, and begins the next frame's work.
    void endWork();

    F64 mSlack[FRAMES] = {};        // the paced frames' slack, by their count % FRAMES, 0 where one showed none
    F64 mLongest[ITEM_FRAMES] = {}; // each frame's end's longest work item, by their count % ITEM_FRAMES
    U32 mPaced       = 0;           // the paced frames' ends since the last that paced nothing
    U32 mEnds        = 0;           // the frames' ends
    F64 mWorked      = 0.0;         // the work done since the last frame's end
    F64 mLongestItem = 0.0;         // the longest work item since the last frame's end
};

template <typename RUN_ONE, typename NOW>
F64 ALPacingSlack::fill(RUN_ONE&& run_one, NOW&& now)
{
    const F64 start = now();
    // An item that begins before this ends within the budget if it takes no longer than the longest of late.
    const F64 until = start + budget() - reserve();
    F64 at = start;
    while (at < until && run_one())
    {
        const F64 done = now();
        mLongestItem = std::max(mLongestItem, done - at);
        at = done;
    }
    mWorked += at - start;
    return at - start;
}
