/**
 * @file aldepthpyramid.cpp
 * @brief The farthest depth under each texel of a view's depth, at every level of a mip chain.
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

#include "aldepthpyramid.h"

#include "llglslshader.h"
#include "llrender.h"
#include "llshadermgr.h"

namespace
{
    // A work group's tile, and the levels one dispatch reduces it through (depthPyramidC.glsl).
    constexpr U32 TILE = 16;
    constexpr U32 LEVELS_A_DISPATCH = 5;

    // Level 0 a side: the depth halved, rounded up, then up to a multiple of 2^(LEVELS - 1), which also makes it whole
    // tiles.
    U32 base_size(U32 depth)
    {
        constexpr U32 ALIGN = 1u << (ALDepthPyramid::LEVELS - 1);
        static_assert(ALIGN % TILE == 0, "level 0 must be whole tiles");
        return ((depth + 1) / 2 + ALIGN - 1) / ALIGN * ALIGN;
    }
}

ALDepthPyramid::ALDepthPyramid()
{
    mTexture.setLabel("depth.pyramid");
}

bool ALDepthPyramid::build(LLGLSLShader& program, LLRenderTarget& depth)
{
    if (!LLRender::sCompute || !program.isComplete() || !depth.getDepth())
    {
        return false;
    }

    // The readers map the view's clip space onto the depth's pixels, which holds where the view is drawn into a target
    // of the depth's size, whose binding set the viewport to the whole of it.
    const U32 width = depth.getWidth();
    const U32 height = depth.getHeight();
    if (LLRenderTarget::sCurResX != width || LLRenderTarget::sCurResY != height)
    {
        return false;
    }

    const U32 base_x = base_size(width);
    const U32 base_y = base_size(height);
    if (!mTexture.isComplete() || mTexture.getWidth() != base_x || mTexture.getHeight() != base_y)
    {
        mTexture.release();
        mLevels = 0;
        if (base_x == mFailedX && base_y == mFailedY)
        {
            return false;
        }
        if (!mTexture.allocate(base_x, base_y, GL_R32F, false, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_MANUAL))
        {
            mTexture.release();
            mFailedX = base_x;
            mFailedY = base_y;
            return false;
        }
    }
    mDepthWidth = width;
    mDepthHeight = height;
    mLevels = LEVELS;

    LL_PROFILE_GPU_ZONE("depth pyramid");
    program.bind();

    // Levels 0 to 4 from the depth, then 5 to 7 from level 4.
    for (U32 first = 0; first < LEVELS; first += LEVELS_A_DISPATCH)
    {
        const U32 count = llmin(LEVELS_A_DISPATCH, LEVELS - first);
        if (first == 0)
        {
            program.bindDepthTexture(LLShaderMgr::DEPTH_PYRAMID_SOURCE, &depth);
        }
        else
        {
            program.bindTexture(LLShaderMgr::DEPTH_PYRAMID_SOURCE, &mTexture, ALSamplers::PointClamp);
        }
        program.uniform1i(LLShaderMgr::DEPTH_PYRAMID_LEVEL, first == 0 ? -1 : (S32)first - 1);
        program.uniform1i(LLShaderMgr::DEPTH_PYRAMID_COUNT, (S32)count);
        for (U32 i = 0; i < count; ++i)
        {
            program.bindImage(LLShaderMgr::DEPTH_PYRAMID_TARGET, &mTexture, ALImageSlot::WRITE_ONLY, 0, first + i, i);
        }
        const U32 side_x = base_x >> first;
        const U32 side_y = base_y >> first;
        program.dispatch((side_x + TILE - 1) / TILE, (side_y + TILE - 1) / TILE);
        // The next dispatch, and the pyramid's readers after the last, fetch what this one stored.
        gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_FETCH);
    }

    program.unbindImage(LLShaderMgr::DEPTH_PYRAMID_TARGET);
    program.unbindTexture(LLShaderMgr::DEPTH_PYRAMID_SOURCE);
    program.unbind();
    return true;
}

void ALDepthPyramid::bind(LLGLSLShader& shader)
{
    shader.bindTexture(LLShaderMgr::DEPTH_PYRAMID, &mTexture, ALSamplers::PointClamp);
    const S32 size[4] = { (S32)mDepthWidth, (S32)mDepthHeight, (S32)mLevels, 0 };
    shader.uniform4iv(LLShaderMgr::DEPTH_PYRAMID_SIZE, 1, size);
}

void ALDepthPyramid::release()
{
    mTexture.release();
    mDepthWidth = 0;
    mDepthHeight = 0;
    mLevels = 0;
    mFailedX = 0;
    mFailedY = 0;
}
