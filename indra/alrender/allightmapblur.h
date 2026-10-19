/**
 * @file allightmapblur.h
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

#pragma once

#include "v3math.h"

class LLGLSLShader;
class LLRenderTarget;

/// The light map's ambient occlusion blur: a Gaussian along each axis in turn that keeps to each pixel's plane, its
/// taps blurLightUtil.glsl's, so the two ways of running it blur alike.
/// - Two fragment passes (gDeferredBlurLightProgram, blurLightF.glsl): across into a scratch of the light map's
///   description, then down from it back into the map. Wherever the session does not compute.
/// - One dispatch (gDeferredBlurLightComputeProgram, blurLightC.glsl): a work group blurs its tile's rows, and the
///   rows above and below them its taps down reach, across into shared memory, and then down from there, into a
///   target of the sun's shadow and the blurred occlusion alone, which the soften pass reads in place of the light
///   map. Nothing is written between the two axes, so it takes no scratch, and the spot lights' shadows, which nothing
///   blurs, stay in the light map for the passes that read them.
class ALLightMapBlur
{
public:
    /// blurLightC.glsl's tile, a work group's, and the most rows above and below it its taps down may reach: a kernel
    /// reaching further is blurred by the fragment passes.
    static constexpr U32 TILE_WIDTH  = 32;
    static constexpr U32 TILE_HEIGHT = 128;
    static constexpr U32 MAX_APRON   = 32;

    /// What both ways of blurring take: four weights out from the centre, the steps between them doubled by
    /// blurLightUtil.glsl's taps halfway between each two.
    struct Kernel
    {
        LLVector3 mTaps[4];      ///< kern: the shadow's weight (unused), the occlusion's, and the offset in steps
        F32       mScale = 0.f;  ///< kern_scale: a step in pixels, before the normal and the distance shorten it
        F32       mDistFactor = 0.f; ///< dist_factor: how much the distance shortens a step
    };

    /// The kernel from RenderShadowGaussian's two widths, RenderShadowBlurSize and RenderShadowBlurDistFactor.
    static Kernel kernel(const LLVector3& gaussian, F32 blur_size, F32 dist_factor);

    /// Sets @a kernel on @a shader, bound: either way's program.
    static void setKernel(LLGLSLShader& shader, const Kernel& kernel);

    /// The rows above and below a tile that @a kernel's taps down can reach, or -1 past MAX_APRON.
    static S32 apron(const Kernel& kernel);

    /// Dispatches @a program, bound as a deferred program over the light map with @a kernel set
    /// (LLPipeline::bindDeferredShader), over the light map's size, into @a target, an RG8 target, the format
    /// blurLightC.glsl stores: the sun's shadow and the blurred occlusion, read wherever the light map is, and the sky
    /// the light map's own. Fetches of @a target after it see what it stored. False, with nothing dispatched, where
    /// @a kernel reaches past MAX_APRON, @a target is of another format, or the program has no image for it, which is
    /// then the fragment passes'.
    static bool dispatch(LLGLSLShader& program, const Kernel& kernel, LLRenderTarget& target);
};
