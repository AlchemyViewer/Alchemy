/**
 * @file aldepthpyramid.h
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

#pragma once

#include "llrendertarget.h"

class LLGLSLShader;

/// The farthest depth under each texel of a view's depth, at LEVELS levels of a mip chain: what a cull tests a box's
/// nearest depth against (depthPyramidUtil.glsl), so a box the scene hides entirely is not drawn. The farthest, never
/// the nearest: a box behind the nearest surface under a texel can still show where that texel also covers sky.
///
/// Level k's texel t is the farthest depth of pixels [t * 2^(k+1), (t + 1) * 2^(k+1)) each way, so depth pixel p lies
/// under texel p >> (k + 1). Level 0 is the depth halved, rounded up, and then rounded up to a multiple of
/// 2^(LEVELS - 1), so every level is half the one before exactly; texels past the depth's edge repeat its last row and
/// column, which every texel over them also covers, so none holds a depth from outside its own pixels.
///
/// Built by compute (gDepthPyramidProgram, depthPyramidC.glsl): two dispatches of 16 x 16 tiles, each reduced through
/// five and then three levels in shared memory. Without compute there is no pyramid, and nothing reads one.
///
/// Built from whichever view's depth it is given; it holds that view's until the next build. Owned by LLPipeline, its
/// texture released with the pipeline's other GL objects.
class ALDepthPyramid
{
public:
    /// The levels built: the finest is the depth halved, the coarsest 2^LEVELS pixels a texel, so a box covering much
    /// of a 4K screen still lies under a few of its texels.
    static constexpr U32 LEVELS = 8;

    ALDepthPyramid();

    /// Builds the pyramid from `depth`'s depth attachment with `program` (gDepthPyramidProgram), the depth final for
    /// what the pyramid's readers test and the view it holds drawn over the whole of it, into a bound target of its
    /// size. Allocates again when the depth's size changed. False, with nothing built, without compute or the program,
    /// where the bound target is another size, or if the allocation failed, which is not tried again at that size
    /// until a release.
    bool build(LLGLSLShader& program, LLRenderTarget& depth);

    /// Binds the pyramid and its size on `shader`, bound, for depth_pyramid_box_visible. After a build, in its view.
    void bind(LLGLSLShader& shader);

    /// Releases the pyramid's texture; the next build allocates it again.
    void release();

    /// Whether the pyramid holds a texture.
    bool isAllocated() const { return mTexture.isComplete(); }

    /// The pyramid: an R32F target whose first LEVELS levels are the pyramid's.
    LLRenderTarget& getTarget() { return mTexture; }
    /// Its levels, and the size of the depth it was built from; 0 before a build.
    U32 getLevels() const { return mLevels; }
    U32 getDepthWidth() const { return mDepthWidth; }
    U32 getDepthHeight() const { return mDepthHeight; }

private:
    LLRenderTarget mTexture;
    U32            mDepthWidth = 0;
    U32            mDepthHeight = 0;
    U32            mLevels = 0;
    // The size an allocation last failed at, not tried again until a release.
    U32            mFailedX = 0;
    U32            mFailedY = 0;
};
