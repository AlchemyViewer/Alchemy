/**
 * @file alprobereach.h
 * @brief ALProbeReach: which reflection probes the view can reach, and the cube slots dynamic allocation asks for
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

#include "llmath.h"
#include "v3math.h"

#include <algorithm>

struct ALProbeReach
{
    // A probe affects only the pixels inside its influence volume, so one whose sphere lies wholly beyond draw
    // distance affects nothing drawn: every probe of a far region, and of the region a teleport left, is one.
    static bool inReach(const LLVector3& origin, F32 radius, const LLVector3& camera, F32 draw_distance)
    {
        return inReach(dist_vec(origin, camera) - radius, draw_distance);
    }

    // Metres past draw distance a probe in reach may go before it is out of it, so a probe at the edge does not lose
    // its cube slot, and need its twelve passes again, each time it crosses.
    static constexpr F32 REACH_MARGIN = 16.f;

    // The same, from the distance between the camera and the sphere's surface, negative inside it, and whether the
    // probe was in reach before.
    static bool inReach(F32 distance_to_sphere, F32 draw_distance, bool was_in_reach = false)
    {
        return distance_to_sphere <= std::max(draw_distance, 0.f) + (was_in_reach ? REACH_MARGIN : 0.f);
    }

    // Probes that matter, counted for dynamic allocation in steps of this many: every change of the count
    // reallocates the cube array and regenerates every probe, so it moves only a step at a time.
    static constexpr U32 RELEVANT_STEP = 16;

    // The count dynamic allocation sizes from, given the probes that matter now and the count it sized from last:
    // up a step as soon as they need it, down only once they are more than a step fewer, so a probe at the edge of
    // draw distance does not reallocate the cube array each time it crosses.
    static U32 stepRelevant(U32 relevant, U32 current)
    {
        const U32 stepped = ((relevant + RELEVANT_STEP - 1) / RELEVANT_STEP) * RELEVANT_STEP;
        return (stepped > current || stepped + RELEVANT_STEP < current) ? stepped : current;
    }

    // The cube slots RenderReflectionProbeDynamicAllocation asks for at probe_level, from the probes that matter
    // (stepped), within probe_count, the slots RenderReflectionProbeCount allows; -1 asks for all of them.
    static U32 dynamicProbeCount(S32 probe_level, S32 allocation, U32 relevant, U32 probe_count)
    {
        if (allocation <= -1)
        {
            return probe_count;
        }

        U32 count = 256;
        if (probe_level == 0)
        {
            count = 1;
        }
        else if (probe_level == 1)
        {
            count = relevant;
        }
        else if (probe_level == 2)
        {
            count = std::max(relevant, 128u);
        }

        if (allocation > 1)
        {
            // The count in units of allocation, rounded, times 16: the nearest multiple of 16 at an allocation of 16,
            // and the count scaled by 16 / allocation at any other.
            count = ((count + allocation / 2) / allocation) * 16;
        }
        else
        {
            count += allocation;
        }
        return llclamp(count, 1u, probe_count);
    }
};
