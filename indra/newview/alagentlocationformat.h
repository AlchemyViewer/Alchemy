/**
 * @file alagentlocationformat.h
 * @brief Turning where the agent is standing into the string a readout shows
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

#ifndef AL_ALAGENTLOCATIONFORMAT_H
#define AL_ALAGENTLOCATIONFORMAT_H

#include "stdtypes.h"
#include "llagentui.h"      // ELocationFormat

#include <string>
#include <string_view>

// The formatting half of LLAgentUI::buildLocationString, which is otherwise
// two jobs in one function: reading the region, the parcel and the agent, and
// arranging what it found into one of twelve shapes.
//
// Split out because the second half is a function of its arguments and the
// first is not. Twelve shapes and a truncation rule are worth a test, and
// nothing here needs an agent to exist to be asked.
class ALAgentLocationFormat
{
public:
    // A landmark name is cut to this many bytes, and on a character boundary:
    // the printf this replaced used "%.100s", which cuts between the bytes of
    // a multi-byte character and leaves the name ending in a replacement
    // glyph.
    static constexpr S32 LANDMARK_NAME_MAX_BYTES = 100;

    // Formats into `out`, which is cleared first and whose capacity is reused,
    // so a readout that rebuilds twice a second stops asking the allocator for
    // the same string every time.
    //
    // `sim_access` is the region's maturity rating, already resolved to the
    // word shown; empty when there is none, in which case the separator in
    // front of it is left out too.
    static void format(std::string& out,
                       LLAgentUI::ELocationFormat format,
                       std::string_view parcel_name,
                       std::string_view region_name,
                       std::string_view sim_access,
                       S32 pos_x, S32 pos_y, S32 pos_z);
};

#endif // AL_ALAGENTLOCATIONFORMAT_H
