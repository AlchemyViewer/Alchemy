/**
 * @file llagentui.cpp
 * @brief Utility methods to process agent's data as slurl's etc. before displaying
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include "llagentui.h"

// Library includes
#include "llparcel.h"

#include <fmt/format.h>

// Viewer includes
#include "llagent.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "llviewerparcelmgr.h"
#include "llvoavatarself.h"
#include "llslurl.h"
// [RLVa:KB] - Checked: 2010-04-04 (RLVa-1.2.0d)
#include "rlvhandler.h"
// [/RLVa:KB]

//static
void LLAgentUI::buildFullname(std::string& name)
{
    if (isAgentAvatarValid())
        name = gAgentAvatarp->getFullname();
}

//static
void LLAgentUI::buildSLURL(LLSLURL& slurl, const bool escaped /*= true*/)
{
      LLSLURL return_slurl;
      LLViewerRegion *regionp = gAgent.getRegion();
      if (regionp)
      {
          // Make sure coordinates are within current region
          LLVector3d global_pos = gAgent.getPositionGlobal();
          LLVector3d region_origin = regionp->getOriginGlobal();
          // -1 otherwise slurl will fmod 256 to 0.
          // And valid slurl range is supposed to be 0..255
          F64 max_val = REGION_WIDTH_METERS - 1;
          global_pos.mdV[VX] = llclamp(global_pos[VX], region_origin[VX], region_origin[VX] + max_val);
          global_pos.mdV[VY] = llclamp(global_pos[VY], region_origin[VY], region_origin[VY] + max_val);

          return_slurl = LLSLURL(regionp->getName(), global_pos);
      }
    slurl = return_slurl;
}

//static
bool LLAgentUI::checkAgentDistance(const LLVector3& pole, F32 radius)
{
    F32 delta_x = gAgent.getPositionAgent().mV[VX] - pole.mV[VX];
    F32 delta_y = gAgent.getPositionAgent().mV[VY] - pole.mV[VY];

    return  sqrt( delta_x* delta_x + delta_y* delta_y ) < radius;
}
//static
void LLAgentUI::getDisplayPos(S32& pos_x, S32& pos_y, S32& pos_z)
{
    getDisplayPos(gAgent.getPositionAgent(), pos_x, pos_y, pos_z);
}

//static
void LLAgentUI::getDisplayPos(const LLVector3& agent_pos_region, S32& pos_x, S32& pos_y, S32& pos_z)
{
    pos_x = S32(agent_pos_region.mV[VX] + 0.5f);
    pos_y = S32(agent_pos_region.mV[VY] + 0.5f);
    pos_z = S32(agent_pos_region.mV[VZ] + 0.5f);

    // Round the numbers based on the velocity
    F32 velocity_mag_sq = gAgent.getVelocity().magVecSquared();

    const F32 FLY_CUTOFF = 6.f;     // meters/sec
    const F32 FLY_CUTOFF_SQ = FLY_CUTOFF * FLY_CUTOFF;
    const F32 WALK_CUTOFF = 1.5f;   // meters/sec
    const F32 WALK_CUTOFF_SQ = WALK_CUTOFF * WALK_CUTOFF;

    if (velocity_mag_sq > FLY_CUTOFF_SQ)
    {
        pos_x -= pos_x % 4;
        pos_y -= pos_y % 4;
    }
    else if (velocity_mag_sq > WALK_CUTOFF_SQ)
    {
        pos_x -= pos_x % 2;
        pos_y -= pos_y % 2;
    }
}

bool LLAgentUI::buildLocationString(std::string& str, ELocationFormat format,const LLVector3& agent_pos_region)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    LLViewerRegion* region = gAgent.getRegion();
    LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();

    if (!region || !parcel) return false;

    S32 pos_x, pos_y, pos_z;
    getDisplayPos(agent_pos_region, pos_x, pos_y, pos_z);

    // create a default name and description for the landmark
    //
    // Viewed, not copied: every source below hands back a reference that
    // outlives this call -- the parcel's own name, the region's own name, or
    // RLVa's string map -- and this runs whenever the readout rebuilds.
    std::string_view parcel_name = LLViewerParcelMgr::getInstance()->getAgentParcelName();
    std::string_view region_name = region->getName();
// [RLVa:KB] - Checked: 2010-04-04 (RLVa-1.2.0d) | Modified: RLVa-1.2.0d
    // RELEASE-RLVa: [SL-2.0.0] Check ELocationFormat to make sure our switch still makes sense
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
    {
        parcel_name = RlvStrings::getString(RlvStringKeys::Hidden::Parcel);
        region_name = RlvStrings::getString(RlvStringKeys::Hidden::Region);
        if (LOCATION_FORMAT_NO_MATURITY == format)
            format = LOCATION_FORMAT_LANDMARK;
        else if (LOCATION_FORMAT_FULL == format)
            format = LOCATION_FORMAT_NO_COORDS;
    }
// [/RLVa:KB]
    // The region hands this back by reference; only the separator in front of
    // it is decided here.
    const std::string& sim_access_string = region->getSimAccessString();
    const std::string_view access_sep = sim_access_string.empty() ? "" : " - ";

    // Formatted into the caller's buffer rather than a local that is then
    // copied out. Callers keep the buffer between rebuilds, so a warm one
    // stops asking the allocator anything at all.
    str.clear();
    auto out = std::back_inserter(str);

    if( parcel_name.empty() )
    {
        // the parcel doesn't have a name
        switch (format)
        {
        case LOCATION_FORMAT_LANDMARK:
            // On a character boundary, not a byte one: the printf this
            // replaced used "%.100s", which cuts a multi-byte character in
            // half and leaves the name ending in a replacement glyph.
            str = utf8str_truncate(region_name, 100);
            break;
        case LOCATION_FORMAT_NORMAL:
            str = region_name;
            break;
        case LOCATION_FORMAT_NORMAL_COORDS:
        case LOCATION_FORMAT_NO_MATURITY:
            fmt::format_to(out, "{} ({}, {}, {})",
                region_name,
                pos_x, pos_y, pos_z);
            break;
        case LOCATION_FORMAT_NO_COORDS:
            fmt::format_to(out, "{}{}{}",
                region_name,
                access_sep,
                sim_access_string);
            break;
        case LOCATION_FORMAT_FULL:
            fmt::format_to(out, "{} ({}, {}, {}){}{}",
                region_name,
                pos_x, pos_y, pos_z,
                access_sep,
                sim_access_string);
            break;
        }
    }
    else
    {
        // the parcel has a name, so include it in the landmark name
        switch (format)
        {
        case LOCATION_FORMAT_LANDMARK:
            str = utf8str_truncate(parcel_name, 100);
            break;
        case LOCATION_FORMAT_NORMAL:
            fmt::format_to(out, "{}, {}", parcel_name, region_name);
            break;
        case LOCATION_FORMAT_NORMAL_COORDS:
            fmt::format_to(out, "{} ({}, {}, {})",
                parcel_name,
                pos_x, pos_y, pos_z);
            break;
        case LOCATION_FORMAT_NO_MATURITY:
            fmt::format_to(out, "{}, {} ({}, {}, {})",
                parcel_name,
                region_name,
                pos_x, pos_y, pos_z);
            break;
        case LOCATION_FORMAT_NO_COORDS:
            fmt::format_to(out, "{}, {}{}{}",
                parcel_name,
                region_name,
                access_sep,
                sim_access_string);
            break;
        case LOCATION_FORMAT_FULL:
            fmt::format_to(out, "{}, {} ({}, {}, {}){}{}",
                parcel_name,
                region_name,
                pos_x, pos_y, pos_z,
                access_sep,
                sim_access_string);
            break;
        }
    }
    return true;
}
bool LLAgentUI::buildLocationString(std::string& str, ELocationFormat format)
{
    return buildLocationString(str, format, gAgent.getPositionAgent());
}
