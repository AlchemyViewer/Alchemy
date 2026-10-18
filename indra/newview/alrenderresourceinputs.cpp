/**
 * @file alrenderresourceinputs.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alrenderresourceinputs.h"

#include "alrendersizeoverride.h"
#include "llavatarappearancedefines.h"
#include "llfeaturemanager.h"
#include "llgltfmaterialpreviewmgr.h"
#include "llrender.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"

// static
ALRenderResourceInputs ALRenderResourceInputs::fromSettings()
{
    ALRenderResourceInputs in;

    if (const std::optional<std::pair<U32, U32>>& size = ALRenderSizeOverride::current())
    {
        in.mWidth  = size->first;
        in.mHeight = size->second;
    }
    else if (gViewerWindow)
    {
        in.mWidth  = (U32)gViewerWindow->getWorldViewWidthRaw();
        in.mHeight = (U32)gViewerWindow->getWorldViewHeightRaw();
    }
    in.mResolutionDivisor    = gSavedSettings.getU32("RenderResolutionDivisor");
    in.mResolutionMultiplier = gSavedSettings.getF32("RenderResolutionMultiplier");

    in.mHDR            = gSavedSettings.getBOOL("RenderHDREnabled");
    in.mEmissive       = gSavedSettings.getBOOL("RenderEnableEmissiveBuffer");
    in.mGBufferNormHDR = LLRender::sGBufferNormHDR;
    in.mReverseZ       = LLRender::sReverseZ;

    in.mShadowDetail     = gSavedSettings.getS32("RenderShadowDetail");
    in.mShadowScale      = gSavedSettings.getF32("RenderShadowResolutionScale");
    in.mShadowDepth32F   = gSavedSettings.getBOOL("AlchemyRenderShadowDepth32F");
    in.mSSAO             = gSavedSettings.getBOOL("RenderDeferredSSAO");
    in.mSSR              = gSavedSettings.getBOOL("RenderScreenSpaceReflections");
    in.mTransparentWater = gSavedSettings.getBOOL("RenderTransparentWater");

    in.mProbes = LLFeatureManager::getInstance()->isFeatureAvailable("RenderReflectionsEnabled") &&
                 gSavedSettings.getBOOL("RenderReflectionsEnabled");
    in.mProbeResolution  = probeResolution(gSavedSettings.getU32("RenderReflectionProbeResolution"));
    in.mProbeSuperSample = probeSuperSample(gSavedSettings.getS32("ALProbeSuperSample"));
    in.mProbeAllCascades = gSavedSettings.getBOOL("AlchemyRenderProbeAllShadowCascades");
    in.mMirrors          = gSavedSettings.getBOOL("RenderMirrors");
    in.mMaterialPreview  = LLGLTFPreviewTexture::liveCount() > 0;
    in.mHeroResolution   = (U32)llmax(gSavedSettings.getS32("RenderHeroProbeResolution"), 1);

    in.mFSAAType          = gSavedSettings.getU32("RenderFSAAType");
    in.mSMAAStencil       = gSavedSettings.getBOOL("RenderSMAAUseStencil");
    in.mBloomMips         = gSavedSettings.getS32("RenderBloomMipCount");
    in.mBloomScale        = gSavedSettings.getS32("RenderBloomResolutionScale");
    in.mBloomHalation     = gSavedSettings.getBOOL("RenderBloomHalation");
    in.mGlowResolutionPow = gSavedSettings.getS32("RenderGlowResolutionPow");
    in.mGlowHDR           = gSavedSettings.getBOOL("RenderGlowHDR");

    in.mBakeWidth  = (U32)LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH;
    in.mBakeHeight = (U32)LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT;

    return in;
}
