/**
 * @file allightmapblur.cpp
 * @brief The light map's ambient occlusion blur: its kernel, and its one dispatch where the session computes.
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

#include "allightmapblur.h"

#include "llglslshader.h"
#include "llmath.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"

#include <cmath>

// static
ALLightMapBlur::Kernel ALLightMapBlur::kernel(const LLVector3& gaussian, F32 blur_size, F32 dist_factor)
{
    // Sampled symmetrically, the middle sample falling exactly on 0.
    Kernel kernel;
    for (U32 i = 0; i < 4; ++i)
    {
        const F32 x = (F32)i;
        kernel.mTaps[i].set(llgaussian(x, gaussian.mV[VX]), llgaussian(x, gaussian.mV[VY]), x);
    }
    kernel.mScale      = blur_size * (4 / 2.f - 0.5f);
    kernel.mDistFactor = dist_factor;
    return kernel;
}

// static
void ALLightMapBlur::setKernel(LLGLSLShader& shader, const Kernel& kernel)
{
    shader.uniform1f(LLShaderMgr::DIST_FACTOR, kernel.mDistFactor);
    shader.uniform3fv(LLShaderMgr::KERN, 4, kernel.mTaps[0].mV);
    shader.uniform1f(LLShaderMgr::KERN_SCALE, kernel.mScale);
}

// static
S32 ALLightMapBlur::apron(const Kernel& kernel)
{
    // A tap lies at its offset times two steps out from an origin the checkerboard moves by a quarter of the second
    // weight's offset, a step no longer than the scale: the normal and the distance only shorten it.
    F32 offset = 0.f;
    for (const LLVector3& tap : kernel.mTaps)
    {
        offset = llmax(offset, std::fabs(tap.mV[VZ]));
    }
    const F32 reach = std::fabs(kernel.mScale) * (2.f * offset + 0.25f * std::fabs(kernel.mTaps[1].mV[VZ]));
    if (!std::isfinite(reach) || reach >= (F32)(MAX_APRON - 1))
    {
        return -1;
    }
    // A tap reads the row its offset lands in, which is at most ceil(reach) away; one more for the arithmetic.
    return (S32)std::ceil(reach) + 1;
}

// static
bool ALLightMapBlur::dispatch(LLGLSLShader& program, const Kernel& kernel, LLRenderTarget& target)
{
    // blurLightC.glsl stores through an rg8 image: the unit's format must be its.
    const S32 rows = apron(kernel);
    if (rows < 0 || target.getInternalFormat() != GL_RG8 ||
        program.bindImage(LLShaderMgr::BLUR_LIGHT_TARGET, &target, ALImageSlot::WRITE_ONLY) < 0)
    {
        return false;
    }

    program.uniform1i(LLShaderMgr::BLUR_LIGHT_APRON, rows);
    program.dispatch((target.getWidth() + TILE_WIDTH - 1) / TILE_WIDTH, (target.getHeight() + TILE_HEIGHT - 1) / TILE_HEIGHT);
    // The soften pass samples what this stored.
    gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_FETCH);
    program.unbindImage(LLShaderMgr::BLUR_LIGHT_TARGET);
    return true;
}
