/**
 * @file lllegacyatmospherics.cpp
 * @brief LLAtmospherics class implementation
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

#include "llviewerprecompiledheaders.h"

#include "lllegacyatmospherics.h"

#include "pipeline.h"
#include "v3colorutil.h"

#include "llsettingssky.h"
#include "llenvironment.h"

class LLFastLn
{
public:
    LLFastLn()
    {
        mTable[0] = 0;
        for( S32 i = 1; i < 257; i++ )
        {
            mTable[i] = log((F32)i);
        }
    }

    F32 ln( F32 x )
    {
        const F32 OO_255 = 0.003921568627450980392156862745098f;
        const F32 LN_255 = 5.5412635451584261462455391880218f;

        if( x < OO_255 )
        {
            return log(x);
        }
        else
        if( x < 1 )
        {
            x *= 255.f;
            S32 index = llfloor(x);
            F32 t = x - index;
            F32 low = mTable[index];
            F32 high = mTable[index + 1];
            return low + t * (high - low) - LN_255;
        }
        else
        if( x <= 255 )
        {
            S32 index = llfloor(x);
            F32 t = x - index;
            F32 low = mTable[index];
            F32 high = mTable[index + 1];
            return low + t * (high - low);
        }
        else
        {
            return log( x );
        }
    }

    F32 pow( F32 x, F32 y )
    {
        return (F32)ll_fast_exp(y * ln(x));
    }


private:
    F32 mTable[257]; // index 0 is unused
};

static LLFastLn gFastLn;


// Functions used a lot.

inline LLColor3 color_norm(const LLColor3 &col)
{
    const F32 m = color_max(col);
    if (m > 1.f)
    {
        return 1.f/m * col;
    }
    else return col;
}

inline void color_gamma_correct(LLColor3 &col)
{
    const F32 gamma_inv = 1.f/1.2f;
    if (col.mV[0] != 0.f)
    {
        col.mV[0] = gFastLn.pow(col.mV[0], gamma_inv);
    }
    if (col.mV[1] != 0.f)
    {
        col.mV[1] = gFastLn.pow(col.mV[1], gamma_inv);
    }
    if (col.mV[2] != 0.f)
    {
        col.mV[2] = gFastLn.pow(col.mV[2], gamma_inv);
    }
}

/***************************************
        Atmospherics
***************************************/

LLAtmospherics::LLAtmospherics()
{
    mFogColor.mV[VRED] = mFogColor.mV[VGREEN] = mFogColor.mV[VBLUE] = 0.5f;
    mFogColor.mV[VALPHA] = 0.0f;
}


LLAtmospherics::~LLAtmospherics()
{
}

LLColor4 LLAtmospherics::calcSkyColorInDir(const LLSettingsSky::ptr_t &psky, AtmosphericsVars& vars, const LLVector3 &dir)
{
    // undo OGL_TO_CFR_ROTATION and negate vertical direction.
    LLVector3 Pn = LLVector3(-dir[1] , -dir[2], -dir[0]);

    //calculates hazeColor
    calcSkyColorWLVert(psky, Pn, vars);

    LLColor3 sky_color = psky->gammaCorrect(vars.hazeColor * 2.0f, vars.gamma);

    return LLColor4(sky_color, 0.0f);
}

// NOTE: Keep these in sync!
//       indra\newview\app_settings\shaders\class1\windlight\windlightDomeF.glsl
//       indra\newview\app_settings\shaders\class1\deferred\skyCloudsF.glsl
//       indra\newview\lllegacyatmospherics.cpp
void LLAtmospherics::calcSkyColorWLVert(const LLSettingsSky::ptr_t &psky, LLVector3 & Pn, AtmosphericsVars& vars)
{
    const LLColor3    blue_density = vars.blue_density;
    const LLColor3    blue_horizon = vars.blue_horizon;
    const F32         haze_horizon = vars.haze_horizon;
    const F32         haze_density = vars.haze_density;
    const F32         density_multiplier = vars.density_multiplier;

    F32         max_y = vars.max_y;
    LLVector4   sun_norm = vars.sun_norm;

    // project the direction ray onto the sky dome.
    F32 phi = acos(Pn[1]);
    F32 sinA = sin(F_PI - phi);
    if (fabsf(sinA) < 0.01f)
    { //avoid division by zero
        sinA = 0.01f;
    }

    F32 Plen = vars.dome_radius * sin(F_PI + phi + asin(vars.dome_offset * sinA)) / sinA;

    Pn *= Plen;

    // Set altitude
    if (Pn[1] > 0.f)
    {
        Pn *= (max_y / Pn[1]);
    }
    else
    {
        Pn *= (-32000.f / Pn[1]);
    }

    Plen = Pn.length();
    Pn /= Plen;

    // Initialize temp variables
    LLColor3 sunlight = vars.sunlight;
    LLColor3 ambient = vars.ambient;

    LLColor3 glow = vars.glow;

    // Sunlight attenuation effect (hue and brightness) due to atmosphere
    // this is used later for sunlight modulation at various altitudes
    LLColor3 light_atten = vars.light_atten;
    LLColor3 light_transmittance = psky->getLightTransmittanceFast(vars.total_density, vars.density_multiplier, Plen);
    (void)light_transmittance; // silence Clang warn-error

    // Calculate relative weights
    LLColor3 temp2(0.f, 0.f, 0.f);
    LLColor3 temp1 = vars.total_density;

    LLColor3 blue_weight = componentDiv(blue_density, temp1);
    LLColor3 blue_factor = blue_horizon * blue_weight;
    LLColor3 haze_weight = componentDiv(smear(haze_density), temp1);
    LLColor3 haze_factor = haze_horizon * haze_weight;


    // Compute sunlight from P & lightnorm (for long rays like sky)
    temp2.mV[1] = llmax(F_APPROXIMATELY_ZERO, llmax(0.f, Pn[1]) * 1.0f + sun_norm.mV[1] );

    temp2.mV[1] = 1.f / temp2.mV[1];
    componentMultBy(sunlight, componentExp((light_atten * -1.f) * temp2.mV[1]));
    componentMultBy(sunlight, light_transmittance);

    // Distance
    temp2.mV[2] = Plen * density_multiplier;

    // Transparency (-> temp1)
    temp1 = componentExp((temp1 * -1.f) * temp2.mV[2]);

    // Compute haze glow
    temp2.mV[0] = Pn * LLVector3(sun_norm);

    temp2.mV[0] = 1.f - temp2.mV[0];
        // temp2.x is 0 at the sun and increases away from sun
    temp2.mV[0] = llmax(temp2.mV[0], .001f);
        // Set a minimum "angle" (smaller glow.y allows tighter, brighter hotspot)

    // Higher glow.x gives dimmer glow (because next step is 1 / "angle")
    temp2.mV[0] *= glow.mV[0];

    temp2.mV[0] = pow(temp2.mV[0], glow.mV[2]);
        // glow.z should be negative, so we're doing a sort of (1 / "angle") function

    // Add "minimum anti-solar illumination"
    temp2.mV[0] += .25f;


    // Haze color above cloud
    vars.hazeColor = (blue_factor * (sunlight + ambient) + componentMult(haze_factor, sunlight * temp2.mV[0] + ambient));

    // Final atmosphere additive
    componentMultBy(vars.hazeColor, LLColor3::white - temp1);
}

void LLAtmospherics::updateFog(const LLVector3& tosun_in)
{
    LLVector3 tosun = tosun_in;

    if (!gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_FOG))
    {
        return;
    }

    LLColor3 res_color[3];

    LLColor3 sky_fog_color = LLColor3::white;

    const F32 tosun_z = tosun.mV[VZ];
    tosun.mV[VZ] = 0.f;
    tosun.normalize();
    LLVector3 perp_tosun;
    perp_tosun.mV[VX] = -tosun.mV[VY];
    perp_tosun.mV[VY] = tosun.mV[VX];
    LLVector3 tosun_45 = tosun + perp_tosun;
    tosun_45.normalize();

    F32 delta = 0.06f;
    tosun.mV[VZ] = delta;
    perp_tosun.mV[VZ] = delta;
    tosun_45.mV[VZ] = delta;
    tosun.normalize();
    perp_tosun.normalize();
    tosun_45.normalize();

    // Sky colors, just slightly above the horizon in the direction of the sun, perpendicular to the sun, and at a 45 degree angle to the sun.
    AtmosphericsVars vars;

    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

    vars.blue_density = psky->getBlueDensity();
    vars.blue_horizon = psky->getBlueHorizon();
    vars.haze_density = psky->getHazeDensity();
    vars.haze_horizon = psky->getHazeHorizon();
    vars.density_multiplier = psky->getDensityMultiplier();
    vars.max_y = psky->getMaxY();
    // calcSkyColorWLVert wants OGL order ([1] is elevation), not CFR (y, x, z)
    vars.sun_norm = LLEnvironment::instance().getClampedLightNorm();
    vars.sunlight = psky->getSunlightColor();
    vars.ambient = psky->getAmbientColor();
    vars.glow = psky->getGlow();
    vars.dome_radius = psky->getDomeRadius();
    vars.dome_offset = psky->getDomeOffset();
    vars.light_atten = psky->getLightAttenuation(vars.max_y);
    vars.total_density = psky->getTotalDensity();
    vars.gamma = psky->getGamma();

    res_color[0] = calcSkyColorInDir(psky, vars, tosun);
    res_color[1] = calcSkyColorInDir(psky, vars, perp_tosun);
    res_color[2] = calcSkyColorInDir(psky, vars, tosun_45);

    sky_fog_color = color_norm(res_color[0] + res_color[1] + res_color[2]);

    F32 full_off = -0.25f;
    F32 full_on = 0.00f;
    F32 on = (tosun_z - full_off) / (full_on - full_off);
    on = llclamp(on, 0.01f, 1.f);
    sky_fog_color *= 0.5f * on;


    // We need to clamp these to non-zero, in order for the gamma correction to work. 0^y = ???
    S32 i;
    for (i = 0; i < 3; i++)
    {
        sky_fog_color.mV[i] = llmax(0.0001f, sky_fog_color.mV[i]);
    }

    color_gamma_correct(sky_fog_color);

    mFogColor = sky_fog_color;
    mFogColor.setAlpha(1);
}
