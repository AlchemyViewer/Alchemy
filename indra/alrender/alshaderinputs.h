/**
 * @file alshaderinputs.h
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

#pragma once

#include "lluuid.h"
#include "stdtypes.h"

// Encodes detail level for dropping textures, in accordance with the GLTF spec where possible
// 0 is highest detail, -1 drops emissive, etc
// Dropping metallic roughness is off-spec - Reserve for potato machines as needed
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#additional-textures
enum TerrainPBRDetail : S32
{
    TERRAIN_PBR_DETAIL_MAX                = 0,
    TERRAIN_PBR_DETAIL_EMISSIVE           = 0,
    TERRAIN_PBR_DETAIL_OCCLUSION          = -1,
    TERRAIN_PBR_DETAIL_NORMAL             = -2,
    TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS = -3,
    TERRAIN_PBR_DETAIL_BASE_COLOR         = -4,
    TERRAIN_PBR_DETAIL_MIN                = -4,
};

/// The settings a shader load reads, and the limits of the context it is judged against, as they stand: what the
/// viewer gathers in one place (LLPipeline::shaderInputsFromSettings) and a test writes by hand.
struct ALShaderSettings
{
    // The context.
    S32  mTextureImageUnits = 16;   ///< GL_MAX_TEXTURE_IMAGE_UNITS
    bool mHasClipControl    = true; ///< glClipControl, which reverse-Z needs
    bool mHasCompute        = true; ///< compute and GLSL 4.30, on a driver not held back (LLGLManager::isIntelLegacy)
    bool mIsD3D12           = false; ///< GLOn12, which builds no indexed material program
    bool mHasBufferStorage  = true; ///< persistent buffer maps (ALGPUBuffer::canMapPersistently), the scratch UBO ring's

    // The settings, each by its name.
    bool mReverseZ               = true;  ///< AlchemyRenderReverseZ
    bool mHDR                    = true;  ///< RenderHDREnabled
    bool mEmissive               = true;  ///< RenderEnableEmissiveBuffer
    S32  mLightingMaxLevel       = 3;     ///< RenderShaderLightingMaxLevel
    S32  mReservedTextureIndices = 12;    ///< RenderReservedTextureIndices
    bool mSSR                    = false; ///< RenderScreenSpaceReflections
    bool mMirrors                = false; ///< RenderMirrors
    bool mProbes                 = true;  ///< RenderReflectionsEnabled, where the feature table allows it
    S32  mProbeLevel             = 3;     ///< RenderReflectionProbeLevel
    S32  mShadowDetail           = 2;     ///< RenderShadowDetail
    U32  mShadowFilterQuality    = 1;     ///< AlchemyRenderShadowFilterQuality
    S32  mTerrainPlanarSampleCount = 1;   ///< RenderTerrainPBRPlanarSampleCount
    S32  mTerrainPBRDetail       = 0;     ///< RenderTerrainPBRDetail
    bool mTerrainHexTiling       = false; ///< AlchemyRenderTerrainHexTiling
    bool mTerrainPBRNormals      = false; ///< RenderTerrainPBRNormalsEnabled
    bool mDynamicLOD             = true;  ///< RenderDynamicLOD
    U32  mWaterHextile           = 1;     ///< AlchemyRenderWaterHextile
    bool mWaterWaves             = true;  ///< AlchemyRenderWaterWaves
    bool mDisableCompute         = false; ///< AlchemyRenderDisableCompute
    bool mTransparentWater       = true;  ///< RenderTransparentWater
    bool mSSAO                   = true;  ///< RenderDeferredSSAO
    bool mAvatarCloth            = false; ///< RenderAvatarCloth
    bool mGlowNoise              = true;  ///< RenderGlowNoise
    bool mBloomHalation          = false; ///< RenderBloomHalation
    bool mSMAAPredication        = true;  ///< RenderSMAAPredication
    bool mLocalTerrainPaint      = false; ///< LocalTerrainPaintEnabled
    U32  mTerrainPaintBitDepth   = 8;     ///< TerrainPaintBitDepth
};

/// Everything the programs are built from, as the load takes it: the settings clamped to what the context can build
/// and to what reaches a program, so that a setting no program reads under the others leaves them equal. Settings
/// change it only where they change a program, so equal inputs need no reload; unequal ones reload only what they
/// changed. A setting the programs read as a uniform, set by the passes that bind its readers, is no input at all.
///
/// Its two halves are what a change costs. The global inputs feed the defines every shared object compiles with
/// (LLGLSLShader::sGlobalDefines), the defines injected into every compile (REVERSE_Z, GBUFFER_NORM_HDR) or the shared
/// objects themselves, so every program attaching them is built again. The local inputs feed only the permutations,
/// features and files of the programs that read them, and only those are built again.
struct ALShaderInputs
{
    /// The most indexed GLTF material slots a program samples (LLGLSLShader::MAX_INDEXED_GLTF_CHANNELS).
    static constexpr S32 MAX_INDEXED_GLTF_CHANNELS = 8;

    struct Global
    {
        bool mReverseZ       = true;  ///< REVERSE_Z: the setting where the context has clip control
        bool mGBufferNormHDR = true;  ///< GBUFFER_NORM_HDR: the normal attachment's layout, which follows HDR
        bool mEmissive       = true;  ///< HAS_EMISSIVE
        S32  mLightingClass  = 3;     ///< the class the shared light-summing objects load at
        S32  mIndexedTextureChannels = 4; ///< the texture units an indexed draw spreads its textures over
        bool mSSR            = false; ///< SSR, and the screen-space reflections' class
        bool mMirrors        = false; ///< HERO_PROBES, the mirror-clip copy of a shared object and the mirror variants
        bool mProbes         = true;  ///< REFMAP_LEVEL, and the reflection probes' class
        S32  mProbeLevel     = 3;     ///< REFMAP_LEVEL, with probes on
        S32  mShadowDetail   = 2;     ///< SUN_SHADOW, SPOT_SHADOW: 0 none, 1 the sun's, 2 the spot lights' too
        S32  mShadowFilterQuality = 1; ///< SHADOW_PCF_KERNEL, and SHADOW_PCSS at 3, with shadows on
        S32  mTerrainPlanarSampleCount = 1; ///< TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT: 1 flat, 3 triplanar
        S32  mTerrainPBRDetail = 0;   ///< TERRAIN_PBR_DETAIL, as much as the context's texture units can sample
        bool mTerrainHexTiling = false; ///< TERRAIN_HEX_TILING
        S32  mWaterHextile   = 1;     ///< WATER_HEXTILE: the hex-tiled wave layers, 0 to 2
        bool mObjectSkinBlock = true; ///< OBJECT_SKIN_BLOCK: the joint palette from a uniform block, with buffer storage

        /// A digest of every field, which every program's hash folds in (LLGLSLShader::sSharedObjectsKey): the shared
        /// objects they link against are compiled from these, some of which reach no define.
        LLUUID digest() const;

        friend bool operator==(const Global&, const Global&) = default;
    };

    struct Local
    {
        bool mHDR              = true;  ///< the post chain's dither and bloom permutations
        bool mTransparentWater = true;  ///< the water programs' TRANSPARENT_WATER
        bool mSSAO             = true;  ///< the sun pass's file and the soften programs' HAS_SSAO
        bool mAvatarCloth      = false; ///< the deferred avatar program's AVATAR_CLOTH
        bool mGlowNoise        = true;  ///< the glow extract's HAS_NOISE
        bool mBloomHalation    = false; ///< the bloom extract's and composites' BLOOM_HALATION
        bool mSMAAPredication  = true;  ///< the SMAA programs' predication
        bool mTerrainPBRNormals = false; ///< the PBR terrain programs' TERRAIN_FLAT_NORMALS
        bool mTerrainTessPinned = false; ///< the terrain tessellation programs' TERRAIN_TESS_PINNED, without dynamic LOD
        bool mWaterWaves       = true;  ///< the water waves' programs and the three objects only they attach
        bool mCompute          = true;  ///< the compute programs, where the context has compute and it is not switched off
        S32  mIndexedGLTFChannels = 4;  ///< the indexed material programs' GLTF_INDEXED_CHANNELS, 0 for none
        U32  mTerrainPaintBitDepth = 0; ///< the local terrain paint's bake program, its precision 1 to 8, 0 for none

        friend bool operator==(const Local&, const Local&) = default;
    };

    Global mGlobal;
    Local  mLocal;

    /// The inputs @a settings make.
    static ALShaderInputs make(const ALShaderSettings& settings);

    /// RenderTerrainPBRPlanarSampleCount as the terrain takes it: 1 flat or 3 triplanar, 2 being flat.
    static S32 terrainMapping(S32 setting);
    /// RenderTerrainPBRDetail as much as @a texture_units can sample under @a mapping (terrainMapping's): the PBR
    /// terrain's fragment stage reads a paint or ramp sampler, parcel_overlay, terrain_height_map when it maps
    /// triplanar (terrainSurface's terrain_facet), and per detail level four maps for each of its four materials, so
    /// full detail needs 18 units flat and 19 triplanar, past the 16 Apple's GL 4.1 gives. Mirrors pbrterrainF.glsl's
    /// #if guards: the materials take 16 units at detail 0, 12 at -1 and -2, 8 at -3, 4 at -4.
    static S32 terrainDetail(S32 setting, S32 mapping, S32 texture_units);

    friend bool operator==(const ALShaderInputs&, const ALShaderInputs&) = default;
};
