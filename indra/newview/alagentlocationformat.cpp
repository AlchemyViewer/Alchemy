/**
 * @file alagentlocationformat.cpp
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

#include "linden_common.h"

#include "alagentlocationformat.h"

#include "llstring.h"       // utf8str_truncate

#include <fmt/format.h>

#include <iterator>

//static
void ALAgentLocationFormat::format(std::string& out,
                                   LLAgentUI::ELocationFormat format,
                                   std::string_view parcel_name,
                                   std::string_view region_name,
                                   std::string_view sim_access,
                                   S32 pos_x, S32 pos_y, S32 pos_z)
{
    const std::string_view access_sep = sim_access.empty() ? "" : " - ";

    // Formatted into the caller's buffer rather than a local that is then
    // copied out. Callers keep the buffer between rebuilds, so a warm one
    // stops asking the allocator anything at all.
    out.clear();
    auto sink = std::back_inserter(out);

    if (parcel_name.empty())
    {
        // the parcel doesn't have a name
        switch (format)
        {
        case LLAgentUI::LOCATION_FORMAT_LANDMARK:
            out = utf8str_truncate(region_name, LANDMARK_NAME_MAX_BYTES);
            break;
        case LLAgentUI::LOCATION_FORMAT_NORMAL:
            out = region_name;
            break;
        case LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS:
        case LLAgentUI::LOCATION_FORMAT_NO_MATURITY:
            fmt::format_to(sink, "{} ({}, {}, {})",
                region_name,
                pos_x, pos_y, pos_z);
            break;
        case LLAgentUI::LOCATION_FORMAT_NO_COORDS:
            fmt::format_to(sink, "{}{}{}",
                region_name,
                access_sep,
                sim_access);
            break;
        case LLAgentUI::LOCATION_FORMAT_FULL:
            fmt::format_to(sink, "{} ({}, {}, {}){}{}",
                region_name,
                pos_x, pos_y, pos_z,
                access_sep,
                sim_access);
            break;
        }
    }
    else
    {
        // the parcel has a name, so include it in the landmark name
        switch (format)
        {
        case LLAgentUI::LOCATION_FORMAT_LANDMARK:
            out = utf8str_truncate(parcel_name, LANDMARK_NAME_MAX_BYTES);
            break;
        case LLAgentUI::LOCATION_FORMAT_NORMAL:
            fmt::format_to(sink, "{}, {}", parcel_name, region_name);
            break;
        case LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS:
            fmt::format_to(sink, "{} ({}, {}, {})",
                parcel_name,
                pos_x, pos_y, pos_z);
            break;
        case LLAgentUI::LOCATION_FORMAT_NO_MATURITY:
            fmt::format_to(sink, "{}, {} ({}, {}, {})",
                parcel_name,
                region_name,
                pos_x, pos_y, pos_z);
            break;
        case LLAgentUI::LOCATION_FORMAT_NO_COORDS:
            fmt::format_to(sink, "{}, {}{}{}",
                parcel_name,
                region_name,
                access_sep,
                sim_access);
            break;
        case LLAgentUI::LOCATION_FORMAT_FULL:
            fmt::format_to(sink, "{}, {} ({}, {}, {}){}{}",
                parcel_name,
                region_name,
                pos_x, pos_y, pos_z,
                access_sep,
                sim_access);
            break;
        }
    }
}
