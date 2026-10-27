/**
 * @file lllegacyatmospherics.h
 * @brief The fog colour the tracker's beacons fade into: WindLight's sky near the horizon, worked on the CPU.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLLEGACYATMOSPHERICS_H
#define LL_LLLEGACYATMOSPHERICS_H

#include "stdtypes.h"
#include "v3color.h"
#include "v3math.h"
#include "v4color.h"
#include "v4math.h"
#include "llsettingssky.h"

const F32 HORIZON_DIST          = 1024.0f;

class AtmosphericsVars
{
public:
    AtmosphericsVars()
    : hazeColor(0,0,0)
    , blue_density()
    , blue_horizon()
    , haze_density(0.0f)
    , haze_horizon(0.0f)
    , density_multiplier(0.0f)
    , max_y(0.0f)
    , gamma(1.0f)
    , sun_norm(0.0f, 1.0f, 0.0f, 1.0f)
    , sunlight()
    , ambient()
    , glow()
    , dome_radius(1.0f)
    , dome_offset(1.0f)
    , light_atten()
    {
    }

    LLColor3  hazeColor;
    LLColor3  blue_density;
    LLColor3  blue_horizon;
    F32       haze_density;
    F32       haze_horizon;
    F32       density_multiplier;
    F32       max_y;
    F32       gamma;
    LLVector4 sun_norm;
    LLColor3  sunlight;
    LLColor3  ambient;
    LLColor3  glow;
    F32       dome_radius;
    F32       dome_offset;
    LLColor3 light_atten;
    LLColor3 total_density;
};

/// The sky's colour a little above the horizon, toward the light, across from it and between, which the tracker's
/// beacons fade into.
class LLAtmospherics
{
public:
    LLAtmospherics();
    ~LLAtmospherics();

    /// Works the colour out from the current sky, toward @a tosun (+z up), while the render debug feature Fog is on.
    void updateFog(const LLVector3& tosun);

    LLColor4 getFogColor() const                     { return mFogColor; }

private:
    LLColor4 calcSkyColorInDir(const LLSettingsSky::ptr_t &psky, AtmosphericsVars& vars, const LLVector3& dir);
    void     calcSkyColorWLVert(const LLSettingsSky::ptr_t &psky, LLVector3 & Pn, AtmosphericsVars& vars);

    LLColor4            mFogColor;
};

#endif
