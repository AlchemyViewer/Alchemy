/**
 * @file alskydome.cpp
 * @brief The WindLight sky dome's mesh, in the dome's frame.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alskydome.h"

#include "llmath.h"
#include "v2math.h"
#include "v3math.h"

#include <cmath>

namespace ALSkyDome
{

F32 ringAngle(U32 ring)
{
    // Calc: PI/8 * 1-((1-t^4)*(1-t^4))  { 0<t<1 }
    // Demos: \pi/8*\left(1-((1-x^{4})*(1-x^{4}))\right)\ \left\{0<x\le1\right\}
    F32 t = float(ring) * (1.f / STACKS);

    // ^4 the parameter of the tesselation to bias things toward 0 (the dome's apex)
    t *= t;
    t *= t;

    // invert and square the parameter of the tesselation to bias things toward 1 (the horizon)
    t = 1.f - t;
    t = t * t;
    t = 1.f - t;

    return (F_PI / 8.f) * t;
}

void buildStrips(U32 begin_stack, U32 end_stack, F32 radius, LLStrider<LLVector3>& vertices,
                 LLStrider<LLVector2>& tex_coords, LLStrider<U16>& indices)
{
    llassert(end_stack <= STACKS);

    for (U32 i = begin_stack + 1; i <= end_stack + 1; ++i)
    {
        const F32 phi0 = ringAngle(i);

        for (U32 j = 0; j < SLICES; ++j)
        {
            const F32 theta = F_TWO_PI * (float(j) / float(SLICES));

            // standard transformation from  spherical to
            // rectangular coordinates
            const F32 x0 = sin(phi0) * cos(theta);
            const F32 y0 = cos(phi0);
            const F32 z0 = sin(phi0) * sin(theta);

            if (i == STACKS - 2)
            {
                *vertices++ = LLVector3(x0 * radius, y0 * radius - 1024.f * 2.f, z0 * radius);
            }
            else if (i == STACKS - 1)
            {
                *vertices++ = LLVector3(0, y0 * radius - 1024.f * 2.f, 0);
            }
            else
            {
                *vertices++ = LLVector3(x0 * radius, y0 * radius, z0 * radius);
            }

            // generate planar uv coordinates
            // note: x and z are transposed in order for things to animate
            // correctly in the global coordinate system where +x is east and
            // +y is north
            *tex_coords++ = LLVector2((-z0 + 1.f) / 2.f, (-x0 + 1.f) / 2.f);
        }
    }

    //build triangle strip...
    *indices++ = 0;

    S32 k = 0;
    for (U32 i = 1; i <= end_stack - begin_stack; ++i)
    {
        *indices++ = (U16)(i * SLICES + (U32)k);

        k = (k + 1) % (S32)SLICES;
        for (U32 j = 0; j < SLICES; ++j)
        {
            *indices++ = (U16)((i - 1) * SLICES + (U32)k);
            *indices++ = (U16)(i * SLICES + (U32)k);

            k = (k + 1) % (S32)SLICES;
        }

        if ((--k) < 0)
        {
            k = (S32)SLICES - 1;
        }

        *indices++ = (U16)(i * SLICES + (U32)k);
    }
}

} // namespace ALSkyDome
