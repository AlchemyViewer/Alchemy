/**
 * @file alrenderresourceinputs.h
 * @brief Everything that decides what the pipeline's render resources are, latched in one place.
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

#include "stdtypes.h"

#include <algorithm>
#include <bit>

/// Everything that decides what the pipeline's render resources are (ALRenderResourcePlan::make), as values. The
/// viewer builds one in one place from its settings and the latches set from them, so every resource is planned from
/// the same reading of the settings; a plan's test builds one by hand.
struct ALRenderResourceInputs
{
    /// The render size before RenderResolutionDivisor and RenderResolutionMultiplier: the world view's, or a
    /// snapshot's while an ALRenderSizeOverride holds one.
    U32 mWidth = 0;
    U32 mHeight = 0;
    U32 mResolutionDivisor = 1;
    F32 mResolutionMultiplier = 1.f;

    bool mHDR = true;            ///< RenderHDREnabled
    bool mEmissive = true;       ///< RenderEnableEmissiveBuffer
    bool mGBufferNormHDR = true; ///< the G-buffer normal layout the programs compile for (LLRender::sGBufferNormHDR)
    bool mReverseZ = true;       ///< the depth convention the programs compile for (LLRender::sReverseZ)
    bool mCompute = true;        ///< whether the session computes, as the programs are built for it (LLRender::sCompute)
    bool mTextureBarrier = true; ///< the context has glTextureBarrier (gGLManager.mHasTextureBarrier); without it the
                                 ///< haze passes read a copy of the scene's depth

    S32  mShadowDetail = 2;       ///< RenderShadowDetail: 0 none, 1 the sun's, 2 the sun's and two spot lights'
    F32  mShadowScale = 1.f;      ///< RenderShadowResolutionScale, of the main view's sun and spot shadows
    bool mShadowDepth32F = false; ///< AlchemyRenderShadowDepth32F
    bool mSSAO = true;            ///< RenderDeferredSSAO
    bool mSSAOBlurDispatched = true; ///< SSAO's blur reaches no further than a dispatch holds (ALLightMapBlur::apron of
                                     ///< RenderShadowBlurSize's kernel): a wider one is the fragment passes'
    bool mSSR = false;            ///< RenderScreenSpaceReflections
    bool mTransparentWater = true; ///< RenderTransparentWater: the water's copies of the scene are for it alone

    bool mProbes = true;          ///< RenderReflectionsEnabled, where the feature table allows it
    U32  mProbeResolution = 128;  ///< a probe's face edge, as probeResolution() takes RenderReflectionProbeResolution
    U32  mProbeSuperSample = 2;   ///< how much larger a probe face renders each way, as probeSuperSample() takes it
    bool mProbeAllCascades = false; ///< AlchemyRenderProbeAllShadowCascades: a probe's face renders four sun cascades
    bool mMirrors = false;        ///< RenderMirrors
    bool mMaterialPreview = false; ///< a GLTF material preview is alive, which renders through the probe pack
    U32  mHeroResolution = 1024;  ///< RenderHeroProbeResolution

    U32  mFSAAType = 0;        ///< RenderFSAAType: 0 none, 1 FXAA, 2 SMAA
    bool mSMAAStencil = true;  ///< RenderSMAAUseStencil
    S32  mBloomMips = 5;       ///< RenderBloomMipCount
    S32  mBloomScale = 0;      ///< RenderBloomResolutionScale: full, 3/4, 1/2, 1/4 or 1/8 of the render size
    bool mBloomHalation = false;
    S32  mGlowResolutionPow = 9; ///< RenderGlowResolutionPow: the legacy glow's height, as a power of two
    bool mGlowHDR = false;       ///< RenderGlowHDR
    F32  mDeferredNoise = 4.f;   ///< RenderDeferredNoise, which the deferred noise map's contents are made from

    /// The appearance bake's scratch target (LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH and _HEIGHT).
    U32 mBakeWidth = 2048;
    U32 mBakeHeight = 2048;

    /// @a width by @a height as the main view renders it: divided by @a divisor where it divides both into more than
    /// one pixel, scaled by @a multiplier otherwise.
    static constexpr void scaleToRenderResolution(U32& width, U32& height, U32 divisor, F32 multiplier)
    {
        if (divisor > 1 && divisor < width && divisor < height)
        {
            width /= divisor;
            height /= divisor;
        }
        else if (multiplier > 0.f && multiplier != 1.f)
        {
            width = (U32)((F32)width * multiplier);
            height = (U32)((F32)height * multiplier);
        }
    }

    /// The size the main view renders at.
    constexpr void renderSize(U32& width, U32& height) const
    {
        width = mWidth;
        height = mHeight;
        scaleToRenderResolution(width, height, mResolutionDivisor, mResolutionMultiplier);
    }

    /// RenderReflectionProbeResolution as the probes take it: a power of two from 64 to 512.
    static constexpr U32 probeResolution(U32 setting) { return std::bit_ceil(std::clamp(setting, 64u, 512u)); }

    /// ALProbeSuperSample as the probes take it: 1, 2 or 4, since the blur and downsample from a face to its probe
    /// assume a power of two.
    static constexpr U32 probeSuperSample(S32 setting) { return setting >= 4 ? 4u : (setting >= 2 ? 2u : 1u); }

    friend bool operator==(const ALRenderResourceInputs&, const ALRenderResourceInputs&) = default;
};
