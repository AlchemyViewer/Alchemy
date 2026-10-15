/**
 * @file aldepthbounds.cpp
 * @brief The depth bounds test, and the full-screen passes held to what the world drew.
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

#include "aldepthbounds.h"

#include "llglheaders.h"
#include "llrender.h"

#include <cmath>
#include <limits>

bool ALDepthBounds::sActive = false;
bool ALDepthBounds::sEnabled = true;

ALDepthBounds::ALDepthBounds(F64 zmin, F64 zmax)
    : mMin(zmin), mMax(zmax)
{
    llassert(available() && !sActive);
    sActive = true;
    gGL.flush();
    glDepthBoundsEXT(zmin, zmax);
    glEnable(GL_DEPTH_BOUNDS_TEST_EXT);
}

ALDepthBounds::~ALDepthBounds()
{
    gGL.flush();
    glDisable(GL_DEPTH_BOUNDS_TEST_EXT);
    sActive = false;
}

void ALDepthBounds::set(F64 zmin, F64 zmax)
{
    if (zmin != mMin || zmax != mMax)
    {
        gGL.flush();
        glDepthBoundsEXT(zmin, zmax);
        mMin = zmin;
        mMax = zmax;
    }
}

// static
void ALDepthBounds::drawn(F64& zmin, F64& zmax)
{
    // The clear's depth is the far plane's, which only the sky still holds. The bounds stop one float short of it:
    // the nearest 24-bit depth below 1 rounds to the float below 1, and every drawn float depth is at least the
    // smallest normal float, so they keep everything drawn whether a driver compares in the buffer's own format or in
    // float.
    if (LLRender::sReverseZ)
    {
        zmin = std::numeric_limits<F32>::min();
        zmax = 1.0;
    }
    else
    {
        zmin = 0.0;
        zmax = std::nextafter(1.f, 0.f);
    }
}

// static
bool ALDepthBounds::viewSpan(const LLMatrix4a& proj, F32 z_a, F32 z_b, F64& zmin, F64& zmax)
{
    // Column-major: clip z = m[2] x + m[6] y + m[10] z + m[14], clip w = m[3] x + m[7] y + m[11] z + m[15].
    const F32* m = proj.getF32ptr();
    if (m[2] != 0.f || m[6] != 0.f || m[3] != 0.f || m[7] != 0.f)
    {
        return false;
    }

    const bool zero_to_one = LLRender::sReverseZ;
    const F64 nearest = al_window_near();
    const auto stored = [&](F64 z)
    {
        const F64 w = (F64)m[11] * z + (F64)m[15];
        if (w <= 0.0)
        {
            return nearest;
        }
        const F64 ndc = ((F64)m[10] * z + (F64)m[14]) / w;
        return llclamp(zero_to_one ? ndc : ndc * 0.5 + 0.5, 0.0, 1.0);
    };
    const F64 a = stored(z_a);
    const F64 b = stored(z_b);

    F64 drawn_min, drawn_max;
    drawn(drawn_min, drawn_max);
    zmin = llclamp(llmin(a, b), drawn_min, drawn_max);
    zmax = llclamp(llmax(a, b), drawn_min, drawn_max);
    return true;
}

ALDrawnOnly::ALDrawnOnly()
    // The depth test off leaves its function at the scene's, so nothing about it turns.
    : mDepthTest(ALDepthBounds::available() ? GL_FALSE : GL_TRUE, GL_FALSE, ALDepthBounds::available() ? GL_LEQUAL : GL_GREATER)
{
    if (ALDepthBounds::available())
    {
        F64 zmin, zmax;
        ALDepthBounds::drawn(zmin, zmax);
        mBounds.emplace(zmin, zmax);
    }
}
