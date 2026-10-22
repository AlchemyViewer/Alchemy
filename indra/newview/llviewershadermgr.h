/**
 * @file llviewershadermgr.h
 * @brief Viewer Shader Manager
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

#ifndef LL_VIEWER_SHADER_MGR_H
#define LL_VIEWER_SHADER_MGR_H

#include "alrenderinputrequests.h"
#include "alshaderinputs.h"
#include "llshadermgr.h"
#include "llmaterial.h"

#define LL_DEFERRED_MULTI_LIGHT_COUNT 16

class LLViewerShaderMgr: public LLShaderMgr
{
public:
    static bool sInitialized;

    LLViewerShaderMgr();
    /* virtual */ ~LLViewerShaderMgr();

    // Post-load sanity pass; asserts on duplicate program names in debug builds
    // (checked over LLGLSLShader::sInstances -- no registration needed).
    void finalizeShaderList();

    // singleton pattern implementation
    static LLViewerShaderMgr * instance();
    static void releaseInstance();

    void initAttribsAndUniforms(void) override;
    /// Loads the programs for @a inputs, which the pipeline's application latched (LLPipeline::applyRenderInputs), and
    /// keeps them, as @a application says: every program built from its sources, or every loader run and only the
    /// programs it configures otherwise than they linked linked again, the shared objects compiled again only where it
    /// asks. A program no loader asks for is unloaded. Whether the programs were loaded: not before the pipeline is
    /// initialised, nor on a context that cannot build them.
    bool setShaders(const ALShaderInputs& inputs, const ALRenderApplication& application);
    /// The inputs the programs were last loaded for, which every loader reads in place of the settings.
    const ALShaderInputs& inputs() const { return mInputs; }
    void unloadShaders();
    S32  getShaderLevel(S32 type);

    // loadBasicShaders in case of a failure returns
    // name of a file error happened at, otherwise
    // returns an empty string
    std::string loadBasicShaders();
    // The objects only the water waves' programs attach, compiled with the global defines while the waves are wanted.
    void loadWaterWaveObjects();
    void loadShadersEffects();
    void loadShadersDeferred();
    void loadShadersObject();
    void loadShadersAvatar();
    void loadShadersWater();
    void loadShadersInterface();

    std::vector<S32> mShaderLevel;
    S32 mMaxAvatarShaderLevel;

    enum EShaderClass
    {
        SHADER_LIGHTING,
        SHADER_OBJECT,
        SHADER_AVATAR,
        SHADER_ENVIRONMENT,
        SHADER_INTERFACE,
        SHADER_EFFECT,
        SHADER_WINDLIGHT,
        SHADER_WATER,
        SHADER_DEFERRED,
        SHADER_COUNT
    };

    std::string getShaderDirPrefix(void) override;

    void updateShaderUniforms(LLGLSLShader * shader) override;

private:
    // A program a loader asked for, recorded where it did not build; whether it built. The loader goes on to the next
    // either way, and a program that did not build draws nothing (LLGLSLShader::bind).
    bool built(bool linked, const LLGLSLShader& shader);

    ALShaderInputs mInputs;
    // Whether the water waves' objects are compiled among the shared objects.
    bool mWaterWaveObjects = false;
    // The programs the load running could not build, by name.
    std::vector<std::string> mFailedPrograms;
}; //LLViewerShaderMgr

extern LLVector4            gShinyOrigin;

//utility shaders
extern LLGLSLShader         gOcclusionProgram;
extern LLGLSLShader         gOcclusionCubeProgram;
extern LLGLSLShader         gGlowCombineProgram;
extern LLGLSLShader         gReflectionMipProgram;
extern LLGLSLShader         gGaussianProgram;
extern LLGLSLShader         gRadianceGenProgram;
extern LLGLSLShader         gHeroRadianceGenProgram;
extern LLGLSLShader         gSHProjectionProgram;
extern LLGLSLShader         gSHProjectionRowsProgram;
extern LLGLSLShader         gSHProjectionReduceProgram;
extern LLGLSLShader         gDebugProgram;
enum NormalDebugShaderVariant : S32
{
    NORMAL_DEBUG_SHADER_DEFAULT,
    NORMAL_DEBUG_SHADER_WITH_TANGENTS,
    NORMAL_DEBUG_SHADER_COUNT
};
extern LLGLSLShader         gNormalDebugProgram[NORMAL_DEBUG_SHADER_COUNT];
extern LLGLSLShader         gClipProgram;
extern LLGLSLShader         gBenchmarkProgram;
extern LLGLSLShader         gReflectionProbeDisplayProgram;
extern LLGLSLShader         gCopyProgram;
extern LLGLSLShader         gCopyDepthProgram;
extern LLGLSLShader         gPBRTerrainBakeProgram;
extern LLGLSLShader         gDrawColorProgram;

//output tex0[tc0] - tex1[tc1]
extern LLGLSLShader         gTwoTextureCompareProgram;
//discard some fragments based on user-set color tolerance
extern LLGLSLShader         gOneTextureFilterProgram;


//object shaders
extern LLGLSLShader     gObjectPreviewProgram;
extern LLGLSLShader        gPhysicsPreviewProgram;
extern LLGLSLShader     gObjectBumpProgram;
extern LLGLSLShader     gObjectAlphaMaskNoColorProgram;

//environment shaders
extern LLGLSLShader         gWaterProgram;
extern LLGLSLShader         gUnderWaterProgram;
// The flat field without TRANSPARENT_WATER, for the views whose water does not refract while the main view's does
// (ALRenderResourcePlan::refractsWater); built while RenderTransparentWater is on.
extern LLGLSLShader         gWaterOpaqueProgram;
extern LLGLSLShader         gUnderWaterOpaqueProgram;
// The patches within the tessellation reach, displaced by the waves; built while they are wanted (ALWaterSurface::wavesWanted).
extern LLGLSLShader         gWaterWavesProgram;
extern LLGLSLShader         gUnderWaterWavesProgram;
extern LLGLSLShader         gWaterExclusionProgram;
extern LLGLSLShader         gWaterFieldProgram;
// One level of ALDepthPyramid; built where the session computes.
extern LLGLSLShader         gDepthPyramidProgram;
extern LLGLSLShader         gGlowProgram;
extern LLGLSLShader         gGlowExtractProgram;
extern LLGLSLShader         gBloomExtractProgram;
extern LLGLSLShader         gBloomDownsampleProgram;
extern LLGLSLShader         gBloomDownsampleFirstProgram;
extern LLGLSLShader         gBloomUpsampleProgram;
extern LLGLSLShader         gBloomCompositeProgram;
// Taps per cross-filter pass. Injected into crossFilterF.glsl as CROSS_TAPS
// and used by pipeline.cpp for the pass strides and the falloff remap, so the
// chain's exact base-N tiling has one source of truth instead of three sites
// that each hard-coded 4 or 63.
constexpr S32               CROSS_FILTER_TAPS = 4;
// Shared by the generation gate and the composite, which read the same setting
// in two different files and must agree, or the effect changes brightness
// between "is it on" and "how bright".
constexpr F32               CROSS_FILTER_MAX_STRENGTH = 32.f;
extern LLGLSLShader         gCrossFilterProgram;
// Upper bound on the generator's segment loops. Injected into lensDirtGenF as
// DIRT_MAX_LINES and used by pipeline.cpp to clamp the scratch count, so the
// loop bound and the CPU clamp cannot drift apart.
constexpr S32               LENS_DIRT_MAX_LINES = 32;
extern LLGLSLShader         gLensDirtGenProgram;

//interface shaders
extern LLGLSLShader         gHighlightProgram;
extern LLGLSLShader         gHighlightNormalProgram;
extern LLGLSLShader         gHighlightSpecularProgram;
// Selection outlines (ALSelectionOutline): the id pass and the wireframe, with rigged variants and linked with
// interface/selectionAlphaF.glsl, the jump pass, the tile pass and the edge pass, each linked with
// interface/selectionUtilF.glsl.
extern LLGLSLShader         gSelectionIdProgram;
extern LLGLSLShader         gSelectionWireframeProgram;
extern LLGLSLShader         gSelectionJumpProgram;
extern LLGLSLShader         gSelectionTileProgram;
extern LLGLSLShader         gSelectionOutlineProgram;
extern LLGLSLShader         gSelectionGlowReachProgram;
extern LLGLSLShader         gSelectionGlowRowProgram;
extern LLGLSLShader         gSelectionBackdropMaskProgram;

extern LLGLSLShader         gDeferredHighlightProgram;

extern LLGLSLShader         gPathfindingProgram;
extern LLGLSLShader         gPathfindingNoNormalsProgram;

// avatar shader handles
extern LLGLSLShader         gAvatarProgram;
extern LLGLSLShader         gImpostorProgram;

// Deferred rendering shaders
extern LLGLSLShader         gDeferredImpostorProgram;
extern LLGLSLShader         gDeferredDiffuseProgram;
extern LLGLSLShader         gDeferredDiffuseAlphaMaskProgram;
extern LLGLSLShader         gDeferredNonIndexedDiffuseAlphaMaskProgram;
extern LLGLSLShader         gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram;
extern LLGLSLShader         gDeferredBumpProgram;
extern LLGLSLShader         gDeferredBumpAlphaMaskProgram; // the bump faces that can discard
extern LLGLSLShader         gDeferredTerrainProgram;
extern LLGLSLShader         gDeferredTreeProgram;
extern LLGLSLShader         gDeferredTreeShadowProgram;
extern LLGLSLShader         gDeferredLightProgram;
extern LLGLSLShader         gDeferredMultiLightProgram[LL_DEFERRED_MULTI_LIGHT_COUNT];
extern LLGLSLShader         gDeferredSpotLightProgram;
extern LLGLSLShader         gDeferredMultiSpotLightProgram;
extern LLGLSLShader         gDeferredSunProgram;
extern LLGLSLShader         gDeferredSunProbeProgram;
extern LLGLSLShader         gHazeProgram;
extern LLGLSLShader         gHazeOnWaterProgram;
extern LLGLSLShader         gHazeWaterProgram;
extern LLGLSLShader         gDeferredBlurLightProgram;
extern LLGLSLShader         gDeferredBlurLightComputeProgram;
extern LLGLSLShader         gDeferredAvatarProgram;
extern LLGLSLShader         gDeferredSoftenProgram;
extern LLGLSLShader         gDeferredSoftenSkyProgram;
extern LLGLSLShader         gDeferredShadowProgram;
extern LLGLSLShader         gDeferredTerrainShadowProgram;
extern LLGLSLShader         gDeferredShadowCubeProgram;
extern LLGLSLShader         gDeferredShadowAlphaMaskProgram;
extern LLGLSLShader         gDeferredShadowGLTFAlphaMaskProgram;
extern LLGLSLShader         gDeferredShadowGLTFAlphaMaskIndexedProgram; // multi-material indexed
extern LLGLSLShader         gDeferredShadowMaterialIndexedProgram; // multi-material indexed legacy mask shadow
extern LLGLSLShader         gDeferredShadowGLTFAlphaBlendProgram;
extern LLGLSLShader         gDeferredShadowFullbrightAlphaMaskProgram;
// DoF gather blur, four variants over two orthogonal compile-time axes:
// FRONT_BLUR (RenderDepthOfFieldNearBlur) and DOF_SHAPED (any of the shaped
// aperture, cat's-eye or defocus fringe being active). The shaped code sits in
// the innermost sample loop, so it is compiled out rather than branched over.
extern LLGLSLShader         gDeferredPostProgram;
extern LLGLSLShader         gDeferredPostProgramNoNear;
extern LLGLSLShader         gDeferredPostProgramShaped;
extern LLGLSLShader         gDeferredPostProgramNoNearShaped;
extern LLGLSLShader         gDeferredDoFCombineProgram;
extern LLGLSLShader         gDeferredDoFCombineProgramNoNear;
extern LLGLSLShader         gFXAAProgram[4];
extern LLGLSLShader         gSMAAEdgeDetectProgram[4];
extern LLGLSLShader         gSMAABlendWeightsProgram[4];
extern LLGLSLShader         gSMAANeighborhoodBlendProgram[4];
extern LLGLSLShader         gCASProgram;
extern LLGLSLShader         gDeferredPostNoDoFProgram;
extern LLGLSLShader         gExposureProgram;
extern LLGLSLShader         gExposureProgramNoFade;
extern LLGLSLShader         gExposureHistogramProgram;
extern LLGLSLShader         gLuminanceProgram;
extern LLGLSLShader         gLensFlareStateProgram;
extern LLGLSLShader         gDeferredAvatarShadowProgram;
extern LLGLSLShader         gDeferredAvatarCutShadowProgram;
extern LLGLSLShader         gDeferredAvatarAlphaShadowProgram;
extern LLGLSLShader         gDeferredAvatarAlphaMaskShadowProgram;
extern LLGLSLShader         gDeferredAlphaProgram;
extern LLGLSLShader         gHUDAlphaProgram;
extern LLGLSLShader         gDeferredAlphaImpostorProgram;
extern LLGLSLShader         gDeferredFullbrightProgram;
extern LLGLSLShader         gHUDFullbrightProgram;
extern LLGLSLShader         gDeferredFullbrightAlphaMaskProgram;
extern LLGLSLShader         gHUDFullbrightAlphaMaskProgram;
extern LLGLSLShader         gDeferredFullbrightAlphaMaskAlphaProgram;
extern LLGLSLShader         gHUDFullbrightAlphaMaskAlphaProgram;
extern LLGLSLShader         gDeferredEmissiveProgram;
extern LLGLSLShader         gDeferredEmissiveIndexedProgram; // multi-material indexed legacy glow
extern LLGLSLShader         gDeferredAvatarAlphaProgram;
extern LLGLSLShader         gEnvironmentMapProgram;
extern LLGLSLShader         gDeferredWLSkyProgram;
extern LLGLSLShader         gDeferredWLCloudProgram;
extern LLGLSLShader         gDeferredWLSunProgram;
extern LLGLSLShader         gDeferredWLMoonProgram;
extern LLGLSLShader         gDeferredStarProgram;
extern LLGLSLShader         gDeferredMeteorProgram;
extern LLGLSLShader         gDeferredAuroraProgram;
extern LLGLSLShader         gDeferredFullbrightShinyProgram;
extern LLGLSLShader         gHUDFullbrightShinyProgram;
extern LLGLSLShader         gNormalMapGenProgram;
extern LLGLSLShader         gDeferredGenBrdfLutProgram;
extern LLGLSLShader         gDeferredBufferVisualProgram;
extern LLGLSLShader         gBlitWithEffectsProgram;
extern LLGLSLShader         gBlitWithEffectsSharpenProgram;
extern LLGLSLShader         gCGGammaProgram;
extern LLGLSLShader         gCGLegacyGammaProgram;
extern LLGLSLShader         gCGTonemapProgram;
extern LLGLSLShader         gCGTonemapLegacyGammaProgram;
extern LLGLSLShader         gCGColorgradeGammaProgram;
extern LLGLSLShader         gCGColorgradeLegacyGammaProgram;
extern LLGLSLShader         gCGTonemapColorgradeProgram;
extern LLGLSLShader         gCGTonemapColorgradeLegacyGammaProgram;
// [RLVa:KB] - @setsphere
extern LLGLSLShader         gRlvSphereProgram;
// [/RLVa:KB]

// Deferred materials shaders
extern LLGLSLShader         gDeferredMaterialProgram[LLMaterial::SHADER_COUNT];
extern LLGLSLShader         gDeferredMaterialIndexedProgram[LLMaterial::SHADER_COUNT]; // multi-material indexed (GBuffer masks only)

extern LLGLSLShader         gHUDPBROpaqueProgram;
extern LLGLSLShader         gPBRGlowProgram;
extern LLGLSLShader         gPBRGlowIndexedProgram; // multi-material indexed PBR glow
extern LLGLSLShader         gDeferredPBROpaqueProgram;
extern LLGLSLShader         gDeferredPBRAlphaMaskProgram;
extern LLGLSLShader         gDeferredPBROpaqueIndexedProgram; // multi-material indexed PBR opaque
extern LLGLSLShader         gDeferredPBRAlphaMaskIndexedProgram; // multi-material indexed PBR alpha mask
extern LLGLSLShader         gDeferredPBRAlphaProgram;
extern LLGLSLShader         gDeferredPBRAlphaImpostorProgram;
extern LLGLSLShader         gHUDPBRAlphaProgram;

enum TerrainPaintType : U32
{
    // Use LLVLComposition::mDatap (heightmap) generated by generateHeights, plus noise from TERRAIN_ALPHARAMP
    TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE = 0,
    // Use paint map if PBR terrain, otherwise fall back to TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE
    TERRAIN_PAINT_TYPE_PBR_PAINTMAP         = 1,
    TERRAIN_PAINT_TYPE_COUNT                = 2,
};
extern LLGLSLShader         gDeferredPBRTerrainProgram[TERRAIN_PAINT_TYPE_COUNT];
extern LLGLSLShader         gDeferredPBRTerrainAlphaMaskProgram[TERRAIN_PAINT_TYPE_COUNT]; // a region with a masked material
#endif
