/**
 * @file llagentui.h
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

#ifndef LLAGENTUI_H
#define LLAGENTUI_H

// Self-contained: everything below is spelled in these types, and a header
// that only compiles behind the viewer's precompiled header cannot be
// included by anything that does not use it -- a test, for one.
#include "stdtypes.h"
#include "v3math.h"

#include <string>

class LLSLURL;

class LLAgentUI
{
public:
    enum ELocationFormat
    {
        LOCATION_FORMAT_NORMAL,         // Parcel
        LOCATION_FORMAT_NORMAL_COORDS,  // Parcel (x, y, z)
        LOCATION_FORMAT_LANDMARK,       // Parcel, Region
        LOCATION_FORMAT_NO_MATURITY,    // Parcel, Region (x, y, z)
        LOCATION_FORMAT_NO_COORDS,      // Parcel, Region - Maturity
        LOCATION_FORMAT_FULL,           // Parcel, Region (x, y, z) - Maturity
    };

    static void buildFullname(std::string &name);

    static void buildSLURL(LLSLURL& slurl, const bool escaped = true);
    //build location string using the current position of gAgent.
    static bool buildLocationString(std::string& str, ELocationFormat format = LOCATION_FORMAT_LANDMARK);
    //build location string using a region position of the avatar.
    static bool buildLocationString(std::string& str, ELocationFormat format,const LLVector3& agent_pos_region);

    // The agent's position as the location readouts print it: whole metres,
    // and snapped to 2 or 4 of them as the agent speeds up. A readout can ask
    // for this and compare it against what it last drew to find out whether
    // its text could have moved, which costs three integers -- where building
    // the text to find out costs a format, several allocations and a shaping
    // pass. The rounding lives here so the two can never disagree.
    static void getDisplayPos(S32& pos_x, S32& pos_y, S32& pos_z);
    static void getDisplayPos(const LLVector3& agent_pos_region, S32& pos_x, S32& pos_y, S32& pos_z);
    /**
     * @brief Check whether  the agent is in neighborhood of the pole  Within same region
     * @return true if the agent is in neighborhood.
     */
    static bool checkAgentDistance(const LLVector3& local_pole, F32 radius);
};

#endif //LLAGENTUI_H
