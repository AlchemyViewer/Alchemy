/**
 * @file alshaderinputs.cpp
 * @brief Everything the viewer's programs are built from, as values.
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

#include "alshaderinputs.h"

#include "alrendertargetdesc.h"
#include "hbxxh.h"

#include <algorithm>

LLUUID ALShaderInputs::Global::digest() const
{
    static_assert(al_render_target_desc::field_count<Global>() == 15,
                  "ALShaderInputs::Global gained or lost a field: fold it into digest()");

    HBXXH128   hash;
    const auto add = [&hash](const auto& value) { hash.update(&value, sizeof(value)); };
    add(mReverseZ);
    add(mGBufferNormHDR);
    add(mEmissive);
    add(mLightingClass);
    add(mIndexedTextureChannels);
    add(mSSR);
    add(mMirrors);
    add(mProbes);
    add(mProbeLevel);
    add(mShadowDetail);
    add(mShadowFilterQuality);
    add(mTerrainPlanarSampleCount);
    add(mTerrainPBRDetail);
    add(mTerrainHexTiling);
    add(mWaterHextile);
    return hash.digest();
}

// static
S32 ALShaderInputs::terrainMapping(S32 setting)
{
    const S32 mapping = std::clamp(setting, 1, 3);
    return mapping == 2 ? 1 : mapping;
}

// static
S32 ALShaderInputs::terrainDetail(S32 setting, S32 mapping, S32 texture_units)
{
    const S32 fixed_samplers = 2 + (mapping == 3 ? 1 : 0);
    S32 detail = std::clamp(setting, (S32)TERRAIN_PBR_DETAIL_MIN, (S32)TERRAIN_PBR_DETAIL_MAX);
    while (detail > TERRAIN_PBR_DETAIL_MIN)
    {
        const S32 samplers = fixed_samplers + 4 * (1 + (detail >= TERRAIN_PBR_DETAIL_NORMAL) +
                                                   (detail >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS) +
                                                   (detail >= TERRAIN_PBR_DETAIL_EMISSIVE));
        if (samplers <= texture_units)
        {
            break;
        }
        --detail;
    }
    return detail;
}

// static
ALShaderInputs ALShaderInputs::make(const ALShaderSettings& s)
{
    ALShaderInputs in;
    Global&        g = in.mGlobal;
    Local&         l = in.mLocal;

    g.mReverseZ       = s.mHasClipControl && s.mReverseZ;
    g.mGBufferNormHDR = s.mHDR;
    g.mEmissive       = s.mEmissive;
    // The class the light-summing objects are asked for, no higher than the feature table allows.
    g.mLightingClass = std::min(3, s.mLightingMaxLevel);
    // An indexed draw leaves the texture units it reserves free for shadow and reflection maps, and spreads its
    // textures over four at the least.
    g.mIndexedTextureChannels = std::max(4, s.mTextureImageUnits - s.mReservedTextureIndices);

    g.mSSR        = s.mSSR;
    g.mMirrors    = s.mMirrors;
    g.mProbes     = s.mProbes;
    g.mProbeLevel = s.mProbes ? std::clamp(s.mProbeLevel, 0, 3) : 0;

    g.mShadowDetail        = std::clamp(s.mShadowDetail, 0, 2);
    g.mShadowFilterQuality = g.mShadowDetail > 0 ? std::clamp((S32)s.mShadowFilterQuality, 0, 3) : 0;

    g.mTerrainPlanarSampleCount = terrainMapping(s.mTerrainPlanarSampleCount);
    g.mTerrainPBRDetail         = terrainDetail(s.mTerrainPBRDetail, g.mTerrainPlanarSampleCount, s.mTextureImageUnits);
    g.mTerrainHexTiling         = s.mTerrainHexTiling;
    g.mWaterHextile             = (S32)std::min(s.mWaterHextile, 2u);

    l.mHDR              = s.mHDR;
    l.mTransparentWater = s.mTransparentWater;
    l.mSSAO             = s.mSSAO;
    l.mAvatarCloth      = s.mAvatarCloth;
    l.mGlowNoise        = s.mGlowNoise;
    l.mBloomHalation    = s.mBloomHalation;
    l.mSMAAPredication  = s.mSMAAPredication;
    l.mTerrainPBRNormals = s.mTerrainPBRNormals;
    l.mTerrainTessPinned = !s.mDynamicLOD;
    l.mWaterWaves        = s.mWaterWaves;
    l.mCompute           = s.mHasCompute && !s.mDisableCompute;
    // One material per four texture units (base colour, normal, ORM, emissive), with no units reserved: the passes
    // drawing indexed materials bind no shadow or reflection maps. GLOn12 builds none: Mesa's d3d12 translates a
    // program to DXIL at its first draw and crashes inside the driver on an indexed legacy material's, with no GL call
    // that could report it first.
    l.mIndexedGLTFChannels =
        s.mIsD3D12 ? 0 : std::clamp(s.mTextureImageUnits / 4, 1, MAX_INDEXED_GLTF_CHANNELS);
    // The bake reads into an 8-bit paint map.
    l.mTerrainPaintBitDepth = s.mLocalTerrainPaint ? std::clamp(s.mTerrainPaintBitDepth, 1u, 8u) : 0;

    return in;
}
