/**
 * @file llviewershadermgr.cpp
 * @brief Viewer shader manager implementation.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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


#include "llviewerprecompiledheaders.h"

#include <boost/lexical_cast.hpp>

#include "aluniformbuffer.h"
#include "hbxxh.h"
#include "llfeaturemanager.h"
#include "llviewershadermgr.h"
#include "llviewercontrol.h"
#include "llversioninfo.h"

#include "llrender.h"
#include "llenvironment.h"
#include "llerrorcontrol.h"
#include "llworld.h"
#include "llsky.h"

#include "pipeline.h"

#include "llfile.h"

#include <filesystem>
#include "llviewerwindow.h"
#include "llwindow.h"

#include "lljoint.h"
#include "llskinningutil.h"

static LLStaticHashedString sTexture0("texture0");
static LLStaticHashedString sTexture1("texture1");
static LLStaticHashedString sTex0("tex0");
static LLStaticHashedString sTex1("tex1");
static LLStaticHashedString sDitherTex("dither_tex");

// Lots of STL stuff in here, using namespace std to keep things more readable
using std::vector;
using std::pair;
using std::make_pair;
using std::string;

bool                LLViewerShaderMgr::sInitialized = false;
bool                LLViewerShaderMgr::sSkipReload = false;

LLVector4           gShinyOrigin;

S32 clamp_terrain_mapping(S32 mapping)
{
    // 1 = "flat", 2 not implemented, 3 = triplanar mapping
    mapping = llclamp(mapping, 1, 3);
    if (mapping == 2) { mapping = 1; }
    return mapping;
}

S32 clamp_terrain_detail(S32 detail)
{
    const S32 requested = llclamp(detail, TERRAIN_PBR_DETAIL_MIN, TERRAIN_PBR_DETAIL_MAX);
    detail = requested;

    // The PBR terrain fragment shader declares one paint/ramp sampler (alpha_ramp or
    // paint_map) plus, per detail level, 4 maps for each of the 4 materials. At full
    // detail that is 17 samplers -- over the 16-per-stage floor that Apple's GL 4.1
    // (every macOS context) and some older desktop GPUs report, where the program then
    // fails to link. Drop detail until the count fits.
    //
    // The arithmetic mirrors pbrterrainF.glsl's #if guards exactly (detail 0 -> 17,
    // -1 and -2 -> 13, -3 -> 9, -4 -> 5); if those guards change, this must too.
    while (detail > TERRAIN_PBR_DETAIL_MIN)
    {
        S32 samplers = 1 + 4 * (1 + (detail >= TERRAIN_PBR_DETAIL_NORMAL)
                                  + (detail >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
                                  + (detail >= TERRAIN_PBR_DETAIL_EMISSIVE));
        if (samplers <= gGLManager.mNumTextureImageUnits)
        {
            break;
        }
        --detail;
    }

    if (detail < requested)
    {
        LL_INFOS_ONCE("ShaderLoading") << "RenderTerrainPBRDetail " << requested << " clamped to " << detail
                                       << ": full detail needs more fragment texture units than the "
                                       << gGLManager.mNumTextureImageUnits
                                       << " this GL implementation provides." << LL_ENDL;
    }
    return detail;
}

//utility shaders
LLGLSLShader    gOcclusionProgram;
LLGLSLShader    gOcclusionCubeProgram;
LLGLSLShader    gGlowCombineProgram;
LLGLSLShader    gReflectionMipProgram;
LLGLSLShader    gGaussianProgram;
LLGLSLShader    gRadianceGenProgram;
LLGLSLShader    gHeroRadianceGenProgram;
LLGLSLShader    gIrradianceGenProgram;
LLGLSLShader    gGlowCombineFXAAProgram;
LLGLSLShader    gTwoTextureCompareProgram;
LLGLSLShader    gOneTextureFilterProgram;
LLGLSLShader    gDebugProgram;
LLGLSLShader    gNormalDebugProgram[NORMAL_DEBUG_SHADER_COUNT];
LLGLSLShader    gClipProgram;
LLGLSLShader    gAlphaMaskProgram;
LLGLSLShader    gBenchmarkProgram;
LLGLSLShader    gReflectionProbeDisplayProgram;
LLGLSLShader    gCopyProgram;
LLGLSLShader    gPBRTerrainBakeProgram;
LLGLSLShader    gDrawColorProgram;

//object shaders
LLGLSLShader        gObjectPreviewProgram;
LLGLSLShader        gPhysicsPreviewProgram;
LLGLSLShader        gObjectBumpProgram;
LLGLSLShader        gObjectAlphaMaskNoColorProgram;

//environment shaders
LLGLSLShader        gWaterProgram;
LLGLSLShader        gUnderWaterProgram;

//interface shaders
LLGLSLShader        gHighlightProgram;
LLGLSLShader        gHighlightNormalProgram;
LLGLSLShader        gHighlightSpecularProgram;

LLGLSLShader        gDeferredHighlightProgram;

LLGLSLShader        gPathfindingProgram;
LLGLSLShader        gPathfindingNoNormalsProgram;

//avatar shader handles
LLGLSLShader        gAvatarProgram;
LLGLSLShader        gImpostorProgram;

// Effects Shaders
LLGLSLShader            gGlowProgram;
LLGLSLShader            gGlowExtractProgram;
LLGLSLShader            gBloomExtractProgram;
LLGLSLShader            gBloomDownsampleProgram;
LLGLSLShader            gBloomDownsampleFirstProgram;
LLGLSLShader            gBloomUpsampleProgram;
LLGLSLShader            gBloomCompositeProgram;

// Deferred rendering shaders
LLGLSLShader            gDeferredImpostorProgram;
LLGLSLShader            gDeferredDiffuseProgram;
LLGLSLShader            gDeferredDiffuseAlphaMaskProgram;
LLGLSLShader            gDeferredNonIndexedDiffuseAlphaMaskProgram;
LLGLSLShader            gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram;
LLGLSLShader            gDeferredBumpProgram;
LLGLSLShader            gDeferredTerrainProgram;
LLGLSLShader            gDeferredTreeProgram;
LLGLSLShader            gDeferredTreeShadowProgram;
LLGLSLShader            gDeferredAvatarProgram;
LLGLSLShader            gDeferredAvatarAlphaProgram;
LLGLSLShader            gDeferredLightProgram;
LLGLSLShader            gDeferredMultiLightProgram[16];
LLGLSLShader            gDeferredSpotLightProgram;
LLGLSLShader            gDeferredMultiSpotLightProgram;
LLGLSLShader            gDeferredSunProgram;
LLGLSLShader            gDeferredSunProbeProgram;
LLGLSLShader            gHazeProgram;
LLGLSLShader            gHazeWaterProgram;
LLGLSLShader            gDeferredBlurLightProgram;
LLGLSLShader            gDeferredSoftenProgram;
LLGLSLShader            gDeferredShadowProgram;
LLGLSLShader            gDeferredShadowCubeProgram;
LLGLSLShader            gDeferredShadowAlphaMaskProgram;
LLGLSLShader            gDeferredShadowGLTFAlphaMaskProgram;
LLGLSLShader            gDeferredShadowGLTFAlphaMaskIndexedProgram; // multi-material indexed
LLGLSLShader            gDeferredShadowMaterialIndexedProgram; // multi-material indexed legacy mask shadow
LLGLSLShader            gDeferredShadowGLTFAlphaBlendProgram;
LLGLSLShader            gDeferredShadowFullbrightAlphaMaskProgram;
LLGLSLShader            gDeferredAvatarShadowProgram;
LLGLSLShader            gDeferredAvatarAlphaShadowProgram;
LLGLSLShader            gDeferredAvatarAlphaMaskShadowProgram;
LLGLSLShader            gDeferredAlphaProgram;
LLGLSLShader            gHUDAlphaProgram;
LLGLSLShader            gDeferredAlphaImpostorProgram;
LLGLSLShader            gDeferredFullbrightProgram;
LLGLSLShader            gHUDFullbrightProgram;
LLGLSLShader            gDeferredFullbrightAlphaMaskProgram;
LLGLSLShader            gHUDFullbrightAlphaMaskProgram;
LLGLSLShader            gDeferredFullbrightAlphaMaskAlphaProgram;
LLGLSLShader            gHUDFullbrightAlphaMaskAlphaProgram;
LLGLSLShader            gDeferredEmissiveProgram;
LLGLSLShader            gDeferredEmissiveIndexedProgram; // multi-material indexed legacy glow
LLGLSLShader            gDeferredPostProgram;
LLGLSLShader            gDeferredPostProgramNoNear;
LLGLSLShader            gDeferredCoFProgram;
LLGLSLShader            gDeferredDoFCombineProgram;
LLGLSLShader            gExposureProgram;
LLGLSLShader            gExposureProgramNoFade;
LLGLSLShader            gLuminanceProgram;
LLGLSLShader            gFXAAProgram[4];
LLGLSLShader            gSMAAEdgeDetectProgram[4];
LLGLSLShader            gSMAABlendWeightsProgram[4];
LLGLSLShader            gSMAANeighborhoodBlendProgram[4];
LLGLSLShader            gCASProgram;
LLGLSLShader            gDeferredPostNoDoFProgram;
LLGLSLShader            gDeferredWLSkyProgram;
LLGLSLShader            gEnvironmentMapProgram;
LLGLSLShader            gDeferredWLCloudProgram;
LLGLSLShader            gDeferredWLSunProgram;
LLGLSLShader            gDeferredWLMoonProgram;
LLGLSLShader            gDeferredStarProgram;
LLGLSLShader            gDeferredMeteorProgram;
LLGLSLShader            gDeferredAuroraProgram;
LLGLSLShader            gDeferredFullbrightShinyProgram;
LLGLSLShader            gHUDFullbrightShinyProgram;
LLGLSLShader            gNormalMapGenProgram;
LLGLSLShader            gDeferredGenBrdfLutProgram;
LLGLSLShader            gDeferredBufferVisualProgram;
LLGLSLShader            gBlitWithEffectsProgram;
LLGLSLShader            gCGGammaProgram;
LLGLSLShader            gCGLegacyGammaProgram;
LLGLSLShader            gCGTonemapProgram;
LLGLSLShader            gCGTonemapLegacyGammaProgram;
LLGLSLShader            gCGColorgradeGammaProgram;
LLGLSLShader            gCGColorgradeLegacyGammaProgram;
LLGLSLShader            gCGTonemapColorgradeProgram;
LLGLSLShader            gCGTonemapColorgradeLegacyGammaProgram;
// [RLVa:KB] - @setsphere
LLGLSLShader            gRlvSphereProgram;
// [/RLVa:KB]

// Deferred materials shaders
LLGLSLShader            gDeferredMaterialProgram[LLMaterial::SHADER_COUNT];
LLGLSLShader            gDeferredMaterialIndexedProgram[LLMaterial::SHADER_COUNT]; // multi-material indexed (GBuffer masks only)
LLGLSLShader            gHUDPBROpaqueProgram;
LLGLSLShader            gPBRGlowProgram;
LLGLSLShader            gPBRGlowIndexedProgram; // multi-material indexed PBR glow
LLGLSLShader            gDeferredPBROpaqueProgram;
LLGLSLShader            gDeferredPBROpaqueIndexedProgram;
LLGLSLShader            gHUDPBRAlphaProgram;
LLGLSLShader            gDeferredPBRAlphaProgram;
LLGLSLShader            gDeferredPBRAlphaImpostorProgram;
LLGLSLShader            gDeferredPBRTerrainProgram[TERRAIN_PAINT_TYPE_COUNT];

// Mirror corners are only reachable during the hero-probe mirror pass, which RenderMirrors
// gates entirely; toggling it re-runs setShaders (handleReflectionProbeDetailChanged), so
// building them while it is off would be dozens of programs nothing can bind.
static U32 mirror_variant()
{
    return gSavedSettings.getBOOL("RenderMirrors") ? (U32)LLGLSLShader::VARIANT_MIRROR : 0u;
}

// Digest of every shader source on disk, folded into the binary cache version.
//
// A program's own hash() covers its file PATHS, its defines and its features -- not the CONTENT
// of anything. Shared objects are worse off still: they are compiled once and attached by name,
// so nothing about them reaches the hash at all. Editing a shader therefore leaves every cached
// binary looking valid, and the only other input to the cache version is the viewer version,
// which does not move between local builds.
//
// Reading the tree costs one pass over a few hundred small files at startup, against reading
// them all again to compile anyway.
static std::string hash_shader_sources()
{
    LL_PROFILE_ZONE_SCOPED;

    std::vector<std::filesystem::path> files;
    std::error_code ec;
    const std::string root = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "shaders");

    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         !ec && it != end;
         it.increment(ec))
    {
        if (it->is_regular_file(ec))
        {
            files.push_back(it->path());
        }
    }

    if (ec || files.empty())
    {   // no digest is better than a wrong one: fall back to invalidating every run
        LL_WARNS("Shader") << "Could not enumerate " << root
                           << " to version the shader cache; treating it as stale" << LL_ENDL;
        return LLUUID::generateNewID().asString();
    }

    std::sort(files.begin(), files.end());  // directory iteration order is unspecified

    HBXXH128 hash_obj;
    for (const auto& file : files)
    {
        // relative, not absolute: the digest must not change with the install location
        hash_obj.update(file.lexically_relative(root).generic_string());

        llifstream in(file.string(), std::ios::binary);
        if (!in.is_open())
        {
            LL_WARNS("Shader") << "Could not read " << file.string()
                               << " to version the shader cache; treating it as stale" << LL_ENDL;
            return LLUUID::generateNewID().asString();
        }

        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        hash_obj.update(text);
    }
    return hash_obj.digest().asString();
}

// Compile a second copy of a shared object under `suffix`, for programs carrying `define`.
//
// Two things this does not do naively. It uses the level the BASE copy RESOLVED to rather than
// the level that was asked for -- loadShaderFile walks down the class directories when a level
// is missing, and a copy built from a different class file than the base would link mismatched
// declarations into one program. And it skips the compile entirely when the file that level
// selects does not mention `define`: deferred/reflectionProbeF.glsl reads CLASSIC_MODE at class3
// and not at class2, so below class3 the second compile yields a byte-identical object under a
// different key -- a full compile of a large source at every setShaders().
// LLShaderMgr::variantObjectKey falls back to the base object when no copy exists, so the skip
// is invisible to attach.
//
// The single-file test is sound because the loader has no #include: a shared source cannot pull
// the define in from anywhere else.
static bool load_axis_copy(LLViewerShaderMgr& mgr,
                           const std::vector<std::pair<std::string, S32> >& loaded,
                           const std::string& path, GLenum stage,
                           std::map<std::string, std::string>& defines,
                           const char* define, const char* suffix)
{
    auto it = std::find_if(loaded.begin(), loaded.end(),
                           [&path](const std::pair<std::string, S32>& e) { return e.first == path; });
    if (it == loaded.end())
    {   // the base pass must have loaded it, or there is nothing for this to be a copy OF
        LL_WARNS("Shader") << "No base object for " << path << "; cannot build its " << suffix
                           << " copy" << LL_ENDL;
        return false;
    }

    S32 level = it->second;

    bool varies = false;
    for (S32 gpu_class = level; gpu_class > 0; --gpu_class)
    {
        const std::string full = mgr.getShaderDirPrefix() + std::to_string(gpu_class)
                               + gDirUtilp->getDirDelimiter() + path;
        llifstream in(full, std::ios::binary);
        if (!in.is_open())
        {
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        varies = text.find(define) != std::string::npos;
        break;  // the first file that exists is the one loadShaderFile picked
    }

    if (!varies)
    {
        return true;
    }

    return mgr.loadShaderFile(path, level, stage, &defines, -1, path + suffix) != 0;
}

static void add_common_permutations(LLGLSLShader* shader)
{
    static LLCachedControl<bool> emissive(gSavedSettings, "RenderEnableEmissiveBuffer", false);

    if (emissive)
    {
        shader->addPermutation("HAS_EMISSIVE", "1");
    }
}

// Map an indexed GLTF PBR program's per-slot samplers to texture units. Slot s
// uses base color unit s; when full (the GBuffer-write shaders, not the shadow
// alpha-mask shader) it also uses normal N+s, ORM 2N+s and emissive 3N+s. Inactive
// samplers resolve to -1 and are skipped by uniform1i. Safe to call on a program's
// rigged variant too.
static void setup_gltf_indexed_samplers(LLGLSLShader& shader, S32 n, bool full)
{
    shader.bind();
    for (S32 s = 0; s < n; ++s)
    {
        shader.uniform1i(LLStaticHashedString(llformat("basecolor%d", s)), s);
        if (full)
        {
            shader.uniform1i(LLStaticHashedString(llformat("normalmap%d", s)), n + s);
            shader.uniform1i(LLStaticHashedString(llformat("ormmap%d", s)), 2 * n + s);
            shader.uniform1i(LLStaticHashedString(llformat("emissivemap%d", s)), 3 * n + s);
        }
    }
    shader.unbind();
}

// Map an indexed legacy material program's per-slot samplers to texture units:
// diffuse slot s -> unit s; normal s -> N+s (HAS_NORMAL_MAP); spec s -> 2N+s
// (HAS_SPECULAR_MAP). Inactive samplers resolve to -1 and are skipped.
static void setup_material_indexed_samplers(LLGLSLShader& shader, S32 n, bool has_normal, bool has_spec)
{
    shader.bind();
    for (S32 s = 0; s < n; ++s)
    {
        shader.uniform1i(LLStaticHashedString(llformat("diffuse%d", s)), s);
        if (has_normal)
        {
            shader.uniform1i(LLStaticHashedString(llformat("bump%d", s)), n + s);
        }
        if (has_spec)
        {
            shader.uniform1i(LLStaticHashedString(llformat("spec%d", s)), 2 * n + s);
        }
    }
    shader.unbind();
}

#ifdef SHOW_ASSERT
// Return true if no two live programs share a name. sInstances tracks every program that
// created successfully, so this covers the whole population without a registration list --
// including the rigged variants, which the old list-walk only reached through the bases
// someone had remembered to register.
static bool no_redundant_shaders()
{
    std::set<std::string> names;
    for (LLGLSLShader* shader : LLGLSLShader::sInstances)
    {
        if (!names.insert(shader->mName).second)
        {
            LL_WARNS("Shader") << "Redundant shader: " << shader->mName << LL_ENDL;
            return false;
        }
    }
    return true;
}
#endif


LLViewerShaderMgr::LLViewerShaderMgr() :
    mShaderLevel(SHADER_COUNT, 0),
    mMaxAvatarShaderLevel(0)
{
}

LLViewerShaderMgr::~LLViewerShaderMgr()
{
    mShaderLevel.clear();
}

void LLViewerShaderMgr::finalizeShaderList()
{
    // make sure there are no redundancies
    llassert(no_redundant_shaders());
}

// static
LLViewerShaderMgr * LLViewerShaderMgr::instance()
{
    if(NULL == sInstance)
    {
        sInstance = new LLViewerShaderMgr();
    }

    return static_cast<LLViewerShaderMgr*>(sInstance);
}

// static
void LLViewerShaderMgr::releaseInstance()
{
    if (sInstance != NULL)
    {
        delete sInstance;
        sInstance = NULL;
    }
}

void LLViewerShaderMgr::initAttribsAndUniforms(void)
{
    if (mReservedAttribs.empty())
    {
        LLShaderMgr::initAttribsAndUniforms();
    }
}


//============================================================================
// Set Levels

S32 LLViewerShaderMgr::getShaderLevel(S32 type)
{
    return mShaderLevel[type];
}

//============================================================================
// Shader Management

void LLViewerShaderMgr::setShaders()
{
    LL_PROFILE_ZONE_SCOPED;
    //setShaders might be called redundantly by gSavedSettings, so return on reentrance
    static bool reentrance = false;

    if (!gPipeline.mInitialized || !sInitialized || reentrance || sSkipReload)
    {
        return;
    }

    if (!gGLManager.mHasRequirements)
    {
        // Viewer will show 'hardware requirements' warning later
        LL_INFOS("ShaderLoading") << "Not supported hardware/software" << LL_ENDL;
        return;
    }

    ALUniformBuffer::sUpdateMode = ALUniformBuffer::clampUpdateMode(gSavedSettings.getS32("AlchemyRenderUBOUpdateMode"));

    // Latch reverse-Z before anything downstream reads it: this same call releases and
    // reallocates GL buffers (mainDepthFormat), every shader compile below injects REVERSE_Z
    // from the latched state, and the cache key folds it in -- all within this one blocking
    // call, so clip control, clear depth, depth format and the shader define agree for the
    // whole next frame.
    LLPipeline::updateReverseZ();

    {
        static LLCachedControl<bool> shader_cache_enabled(gSavedSettings, "RenderShaderCacheEnabled", true);
        static LLUUID old_cache_version;
        static LLUUID current_cache_version;
        if (current_cache_version.isNull())
        {
            HBXXH128 hash_obj;
            hash_obj.update(LLVersionInfo::instance().getVersion());
            hash_obj.update(hash_shader_sources());
            current_cache_version = hash_obj.digest();

            old_cache_version = LLUUID(gSavedSettings.getString("RenderShaderCacheVersion"));
            gSavedSettings.setString("RenderShaderCacheVersion", current_cache_version.asString());
        }

        initShaderCache(
            shader_cache_enabled,
            old_cache_version,
            current_cache_version,
            LLAppViewer::instance()->isSecondInstance());
    }

    static LLCachedControl<U32> max_texture_index(gSavedSettings, "RenderMaxTextureIndex", 16);

    // when using indexed texture rendering, leave some texture units available for shadow and reflection maps
    // We assume we always have atleast 16 texunits available, but we clamp the reserved units to ensure we don't end up with a negative
    // number of texture channels
    static LLCachedControl<S32> reserved_texture_units(gSavedSettings, "RenderReservedTextureIndices", 12);

    LLGLSLShader::sIndexedTextureChannels = llmax(4, gGLManager.mNumTextureImageUnits - reserved_texture_units);

    // Indexed GLTF PBR batches one material per four texture units (base color,
    // normal, ORM, emissive). The PBR opaque GBuffer-write pass binds no
    // shadow/reflection maps, so the full fragment texture-unit budget is
    // available here -- unlike sIndexedTextureChannels above, no units are
    // reserved. Capped at 8 to bound shader sampler declarations.
    LLGLSLShader::sIndexedGLTFChannels = llclamp(gGLManager.mNumTextureImageUnits / 4, 1,
                                                LLGLSLShader::MAX_INDEXED_GLTF_CHANNELS);

    reentrance = true;

    // Make sure the compiled shader map is cleared before we recompile shaders.
    mVertexShaderObjects.clear();
    mFragmentShaderObjects.clear();

    initAttribsAndUniforms();
    gPipeline.releaseGLBuffers();

    unloadShaders();

    LLPipeline::sRenderGlow = gSavedSettings.getBOOL("RenderGlow");
    LLPipeline::sRenderTransparentWater = gSavedSettings.getBOOL("RenderTransparentWater");

    if (gViewerWindow)
    {
        gViewerWindow->setCursor(UI_CURSOR_WAIT);
    }

    // Shaders
    LL_INFOS("ShaderLoading") << "\n~~~~~~~~~~~~~~~~~~\n Loading Shaders:\n~~~~~~~~~~~~~~~~~~" << LL_ENDL;
    LL_INFOS("ShaderLoading") << llformat("Using GLSL %d.%d", gGLManager.mGLSLVersionMajor, gGLManager.mGLSLVersionMinor) << LL_ENDL;

    for (S32 i = 0; i < SHADER_COUNT; i++)
    {
        mShaderLevel[i] = 0;
    }
    mMaxAvatarShaderLevel = 0;

    LLVertexBuffer::unbind();

    llassert((gGLManager.mGLSLVersionMajor > 1 || gGLManager.mGLSLVersionMinor >= 10));


    S32 light_class = 3;
    S32 interface_class = 2;
    S32 env_class = 2;
    S32 obj_class = 2;
    S32 effect_class = 2;
    S32 wl_class = 2;
    S32 water_class = 3;
    S32 deferred_class = 3;

    // Trigger a full rebuild of the fallback skybox / cubemap if we've toggled windlight shaders
    if (!wl_class || (mShaderLevel[SHADER_WINDLIGHT] != wl_class && gSky.mVOSkyp.notNull()))
    {
        gSky.mVOSkyp->forceSkyUpdate();
    }

    // Load lighting shaders
    mShaderLevel[SHADER_LIGHTING] = light_class;
    mShaderLevel[SHADER_INTERFACE] = interface_class;
    mShaderLevel[SHADER_ENVIRONMENT] = env_class;
    mShaderLevel[SHADER_WATER] = water_class;
    mShaderLevel[SHADER_OBJECT] = obj_class;
    mShaderLevel[SHADER_EFFECT] = effect_class;
    mShaderLevel[SHADER_WINDLIGHT] = wl_class;
    mShaderLevel[SHADER_DEFERRED] = deferred_class;

    std::string shader_name = loadBasicShaders();
    if (shader_name.empty())
    {
        LL_INFOS("Shader") << "Loaded basic shaders." << LL_ENDL;
    }
    else
    {
        // "ShaderLoading" and "Shader" need to be logged
        LL_WARNS("Shader") << "Failed loading basic shaders.  Retrying with increased log level..." << LL_ENDL;

        LLError::ELevel lvl = LLError::getDefaultLevel();
        LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
        loadBasicShaders();
        LLError::setDefaultLevel(lvl);
        gGLManager.printGLInfoString();
        LL_ERRS() << "Unable to load basic shader " << shader_name << ", verify graphics driver installed and current." << LL_ENDL;
        reentrance = false; // For hygiene only, re-try probably helps nothing
        return;
    }

    gPipeline.mShadersLoaded = true;

    bool loaded = loadShadersWater();

    if (loaded)
    {
        LL_INFOS() << "Loaded water shaders." << LL_ENDL;
    }
    else
    {
        LL_WARNS() << "Failed to load water shaders." << LL_ENDL;
        llassert(loaded);
    }

    if (loaded)
    {
        loaded = loadShadersEffects();
        if (loaded)
        {
            LL_INFOS() << "Loaded effects shaders." << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "Failed to load effects shaders." << LL_ENDL;
            llassert(loaded);
        }
    }

    if (loaded)
    {
        loaded = loadShadersInterface();
        if (loaded)
        {
            LL_INFOS() << "Loaded interface shaders." << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "Failed to load interface shaders." << LL_ENDL;
            llassert(loaded);
        }
    }

    if (loaded)
    {
        // Load max avatar shaders to set the max level
        mShaderLevel[SHADER_AVATAR] = 3;
        mMaxAvatarShaderLevel = 3;

        if (loadShadersObject())
        { //hardware skinning is enabled and rigged attachment shaders loaded correctly
            // cloth is a class3 shader
            S32 avatar_class = 1;

            // Set the actual level
            mShaderLevel[SHADER_AVATAR] = avatar_class;

            loaded = loadShadersAvatar();
            llassert(loaded);
        }
        else
        { //hardware skinning not possible, neither is deferred rendering
            llassert(false); // SHOULD NOT BE POSSIBLE
        }
    }

    llassert(loaded);
    loaded = loaded && loadShadersDeferred();
    if (loaded)
    {
        LL_INFOS() << "Loaded deferred shaders." << LL_ENDL;
    }
    else
    {
        LL_WARNS() << "Failed to load deferred shaders." << LL_ENDL;
        llassert(loaded);
    }

    // We only want to persist shader cache metadata if we successfully loaded shaders, otherwise we might be caching failure states
    if (loaded && !LLAppViewer::instance()->isSecondInstance())
    {
        persistShaderCacheMetadata();
    }

    if (gViewerWindow)
    {
        gViewerWindow->setCursor(UI_CURSOR_ARROW);
    }
    gPipeline.createGLBuffers();

    finalizeShaderList();

    reentrance = false;
}

void LLViewerShaderMgr::unloadShaders()
{
    while (!LLGLSLShader::sInstances.empty())
    {
        LLGLSLShader* shader = *(LLGLSLShader::sInstances.begin());
        shader->unload();
    }

    mShaderLevel[SHADER_LIGHTING] = 0;
    mShaderLevel[SHADER_OBJECT] = 0;
    mShaderLevel[SHADER_AVATAR] = 0;
    mShaderLevel[SHADER_ENVIRONMENT] = 0;
    mShaderLevel[SHADER_WATER] = 0;
    mShaderLevel[SHADER_INTERFACE] = 0;
    mShaderLevel[SHADER_EFFECT] = 0;
    mShaderLevel[SHADER_WINDLIGHT] = 0;

    gPipeline.mShadersLoaded = false;
}

std::string LLViewerShaderMgr::loadBasicShaders()
{
    // Load basic dependency shaders first
    // All of these have to load for any shaders to function

    S32 sum_lights_class = 3;

    // Use the feature table to mask out the max light level to use.  Also make sure it's at least 1.
    S32 max_light_class = gSavedSettings.getS32("RenderShaderLightingMaxLevel");
    sum_lights_class = llclamp(sum_lights_class, 1, max_light_class);

    // Load the Basic Vertex Shaders at the appropriate level.
    // (in order of shader function call depth for reference purposes, deepest level first)

    vector< pair<string, S32> > shaders;
    shaders.push_back( make_pair( "windlight/atmosphericsVarsV.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "windlight/atmosphericsHelpersV.glsl",    mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "lighting/lightFuncV.glsl",               mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "lighting/sumLightsV.glsl",               sum_lights_class ) );
    shaders.push_back( make_pair( "lighting/lightV.glsl",                   mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "lighting/lightFuncSpecularV.glsl",       mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "lighting/sumLightsSpecularV.glsl",       sum_lights_class ) );
    shaders.push_back( make_pair( "lighting/lightSpecularV.glsl",           mShaderLevel[SHADER_LIGHTING] ) );
    shaders.push_back( make_pair( "windlight/atmosphericsFuncs.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "windlight/atmosphericsV.glsl",           mShaderLevel[SHADER_WINDLIGHT] ) );
    shaders.push_back( make_pair( "environment/srgbF.glsl",                 1 ) );
    shaders.push_back( make_pair( "avatar/avatarSkinV.glsl",                1 ) );
    shaders.push_back( make_pair( "avatar/objectSkinV.glsl",                1 ) );
    shaders.push_back( make_pair( "deferred/textureUtilV.glsl",             1 ) );
    if (gGLManager.mGLSLVersionMajor >= 2 || gGLManager.mGLSLVersionMinor >= 30)
    {
        shaders.push_back( make_pair( "objects/indexedTextureV.glsl",           1 ) );
    }
    shaders.push_back( make_pair( "objects/nonindexedTextureV.glsl",        1 ) );

    std::map<std::string, std::string> attribs;
    attribs["MAX_JOINTS_PER_MESH_OBJECT"] =
        std::to_string(LLSkinningUtil::getMaxJointCount());

    static LLCachedControl<bool> emissive(gSavedSettings, "RenderEnableEmissiveBuffer", false);

    if (emissive)
    {
        attribs["HAS_EMISSIVE"] = "1";
    }

    bool ssr = gSavedSettings.getBOOL("RenderScreenSpaceReflections");

    bool mirrors = gSavedSettings.getBOOL("RenderMirrors");

    bool has_reflection_probes = gSavedSettings.getBOOL("RenderReflectionsEnabled") && gGLManager.mGLVersion > 3.99f;

    S32 probe_level = llclamp(gSavedSettings.getS32("RenderReflectionProbeLevel"), 0, 3);

    S32 shadow_detail            = gSavedSettings.getS32("RenderShadowDetail");

    if (shadow_detail >= 1)
    {
        attribs["SUN_SHADOW"] = "1";

        if (shadow_detail >= 2)
        {
            attribs["SPOT_SHADOW"] = "1";
        }

        // PCF filter kernel width (texels) from the quality tier, overriding the shader's own
        // default (deferred/shadowUtil.glsl SHADOW_PCF_KERNEL == 4). Even values only: the
        // gather path tiles the kernel in 2x2 blocks, so K/2 gather-compare fetches per axis.
        static const S32 pcf_kernel[] = { 2, 4, 6, 4 }; // Low / Medium / High / Ultra(PCSS)
        const S32 quality = llclamp((S32)gSavedSettings.getU32("AlchemyRenderShadowFilterQuality"), 0, 3);
        attribs["SHADOW_PCF_KERNEL"] = std::to_string(pcf_kernel[quality]);

        if (quality >= 3)
        {
            // Ultra: contact-hardening PCSS. This ALSO switches the shadow maps from
            // depth-compare samplers to plain sampler2D reads (a blocker search needs the
            // raw depth), so LLPipeline::bindShadowMaps must bind a matching non-compare
            // sampler -- it reads this same setting. Keep the two in step.
            attribs["SHADOW_PCSS"] = "1";
            const F32 pcss_scale = gSavedSettings.getF32("AlchemyRenderShadowPCSSScale");
            attribs["SHADOW_PCSS_SCALE"] = llformat("%.2f", pcss_scale);
        }
    }

    if (ssr)
    {
        attribs["SSR"] = "1";
    }

    if (has_reflection_probes)
    {
        attribs["REFMAP_LEVEL"] = std::to_string(probe_level);
        attribs["REF_SAMPLE_COUNT"] = "32";
    }

    if (mirrors)
    {
        attribs["HERO_PROBES"] = "1";
    }

    // NOTE: REVERSE_Z is deliberately NOT here. This map reaches only the shared objects
    // compiled below -- a program's own mShaderFiles carry their permutations and nothing else
    // -- and reverse-Z is read by program-owned sources too. LLShaderMgr::loadShaderFile()
    // injects it for every compile instead, and LLGLSLShader::hash() folds LLRender::sReverseZ
    // in directly so a toggle still invalidates cached binaries.

    { // PBR terrain
        const S32 mapping = clamp_terrain_mapping(gSavedSettings.getS32("RenderTerrainPBRPlanarSampleCount"));
        attribs["TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT"] = llformat("%d", mapping);
        const F32 triplanar_factor = gSavedSettings.getF32("RenderTerrainPBRTriplanarBlendFactor");
        attribs["TERRAIN_TRIPLANAR_BLEND_FACTOR"] = llformat("%.2f", triplanar_factor);
        const S32 detail = clamp_terrain_detail(gSavedSettings.getS32("RenderTerrainPBRDetail"));
        attribs["TERRAIN_PBR_DETAIL"] = llformat("%d", detail);
    }

    LLGLSLShader::sGlobalDefines = attribs;

    // Shared objects are compiled once and attached by name, so a per-program permutation
    // cannot reach them. Sources that read classic_mode therefore get a second copy compiled
    // with CLASSIC_MODE=1, stored under a suffixed key; attachShaderFeatures() picks the copy
    // matching each program's own defines (see LLShaderMgr::variantObjectKey).
    std::map<std::string, std::string> classic_attribs = attribs;
    classic_attribs["CLASSIC_MODE"] = "1";

    // We no longer have to bind the shaders to global glhandles, they are automatically added to a map now.
    for (U32 i = 0; i < shaders.size(); i++)
    {
        // Note usage of GL_VERTEX_SHADER
        if (loadShaderFile(shaders[i].first, shaders[i].second, GL_VERTEX_SHADER, &attribs) == 0)
        {
            LL_WARNS("Shader") << "Failed to load basic vertex shader " << i << ": " << shaders[i].first << LL_ENDL;
            return shaders[i].first;
        }
    }

    if (!load_axis_copy(*this, shaders, "windlight/atmosphericsFuncs.glsl", GL_VERTEX_SHADER,
                        classic_attribs, "CLASSIC_MODE", LLShaderMgr::CLASSIC_OBJECT_SUFFIX))
    {
        return "windlight/atmosphericsFuncs.glsl";
    }

    // Load the Basic Fragment Shaders at the appropriate level.
    // (in order of shader function call depth for reference purposes, deepest level first)

    shaders.clear();
    S32 ch = 1;

    if (gGLManager.mGLSLVersionMajor > 1 || gGLManager.mGLSLVersionMinor >= 30)
    { //use indexed texture rendering for GLSL >= 1.30
        ch = llmax(LLGLSLShader::sIndexedTextureChannels, 1);
    }


    std::vector<S32> index_channels;
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsVarsF.glsl",      mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsHelpersF.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsFuncs.glsl",       mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "windlight/atmosphericsF.glsl",          mShaderLevel[SHADER_WINDLIGHT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "environment/waterFogF.glsl",                mShaderLevel[SHADER_WATER] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "environment/srgbF.glsl",                    mShaderLevel[SHADER_ENVIRONMENT] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/deferredUtil.glsl",                    1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/gbufferUtil.glsl",                    1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/globalF.glsl",                          1));
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/shadowUtil.glsl",                      1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/aoUtil.glsl",                          1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/pbrterrainUtilF.glsl",                 1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/tonemapUtilF.glsl",                    1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "alchemy/colorGradeUtilF.glsl",                 1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "alchemy/postEffectUtilsF.glsl",                 1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/reflectionProbeF.glsl",                has_reflection_probes ? 3 : 2) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "deferred/screenSpaceReflUtil.glsl",             ssr ? 3 : 1) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "lighting/lightNonIndexedF.glsl",                    mShaderLevel[SHADER_LIGHTING] ) );
    index_channels.push_back(-1);    shaders.push_back( make_pair( "lighting/lightAlphaMaskNonIndexedF.glsl",                   mShaderLevel[SHADER_LIGHTING] ) );
    index_channels.push_back(ch);    shaders.push_back( make_pair( "lighting/lightF.glsl",                  mShaderLevel[SHADER_LIGHTING] ) );
    index_channels.push_back(ch);    shaders.push_back( make_pair( "lighting/lightAlphaMaskF.glsl",                 mShaderLevel[SHADER_LIGHTING] ) );

    for (U32 i = 0; i < shaders.size(); i++)
    {
        // Note usage of GL_FRAGMENT_SHADER
        if (loadShaderFile(shaders[i].first, shaders[i].second, GL_FRAGMENT_SHADER, &attribs, index_channels[i]) == 0)
        {
            LL_WARNS("Shader") << "Failed to load fragment shader " << shaders[i].first << LL_ENDL;
            return shaders[i].first;
        }
    }

    if (mirrors)
    {
        // globalF's mirrorClip() is the only shared source that varies on MIRROR_CLIP. Only
        // compiled when RenderMirrors is on, matching the gate on the variants themselves --
        // nothing can attach this copy otherwise.
        std::map<std::string, std::string> mirror_attribs = attribs;
        mirror_attribs["MIRROR_CLIP"] = "1";

        if (!load_axis_copy(*this, shaders, "deferred/globalF.glsl", GL_FRAGMENT_SHADER,
                            mirror_attribs, "MIRROR_CLIP", LLShaderMgr::MIRROR_OBJECT_SUFFIX))
        {
            return "deferred/globalF.glsl";
        }
    }

    for (const char* path : { "windlight/atmosphericsFuncs.glsl",
                              "deferred/deferredUtil.glsl",
                              "deferred/reflectionProbeF.glsl" })
    {
        if (!load_axis_copy(*this, shaders, path, GL_FRAGMENT_SHADER,
                            classic_attribs, "CLASSIC_MODE", LLShaderMgr::CLASSIC_OBJECT_SUFFIX))
        {
            return path;
        }
    }

    return std::string();
}

bool LLViewerShaderMgr::loadShadersWater()
{
    LL_PROFILE_ZONE_SCOPED;
    bool success = true;
    bool terrainWaterSuccess = true;

    bool use_sun_shadow = mShaderLevel[SHADER_DEFERRED] > 1 &&
        gSavedSettings.getS32("RenderShadowDetail") > 0;

    if (mShaderLevel[SHADER_WATER] == 0)
    {
        gWaterProgram.unload();
        gUnderWaterProgram.unload();
        return true;
    }

    if (success)
    {
        // load water shader
        gWaterProgram.mName = "Water Shader";
        gWaterProgram.mFeatures.calculatesAtmospherics = true;
        gWaterProgram.mFeatures.hasAtmospherics = true;
        gWaterProgram.mFeatures.hasGamma = true;
        gWaterProgram.mFeatures.hasSrgb = true;
        gWaterProgram.mFeatures.hasReflectionProbes = true;
        gWaterProgram.mFeatures.hasTonemap = true;
        gWaterProgram.mFeatures.hasShadows = use_sun_shadow;
        gWaterProgram.mShaderFiles.clear();
        gWaterProgram.mShaderFiles.push_back(make_pair("environment/waterV.glsl", GL_VERTEX_SHADER));
        gWaterProgram.mShaderFiles.push_back(make_pair("environment/waterF.glsl", GL_FRAGMENT_SHADER));
        gWaterProgram.clearPermutations();
        if (LLPipeline::sRenderTransparentWater)
        {
            gWaterProgram.addPermutation("TRANSPARENT_WATER", "1");
        }

        if (use_sun_shadow)
        {
            gWaterProgram.addPermutation("HAS_SUN_SHADOW", "1");
        }

        gWaterProgram.mShaderGroup = LLGLSLShader::SG_WATER;
        gWaterProgram.mShaderLevel = mShaderLevel[SHADER_WATER];
        success = gWaterProgram.createShader(LLGLSLShader::VARIANT_CLASSIC | mirror_variant());
        llassert(success);
    }

    if (success)
    {
        //load under water vertex shader
        gUnderWaterProgram.mName = "Underwater Shader";
        gUnderWaterProgram.mFeatures.calculatesAtmospherics = true;
        gUnderWaterProgram.mFeatures.hasAtmospherics = true;
        gUnderWaterProgram.mShaderFiles.clear();
        gUnderWaterProgram.mShaderFiles.push_back(make_pair("environment/waterV.glsl", GL_VERTEX_SHADER));
        gUnderWaterProgram.mShaderFiles.push_back(make_pair("environment/underWaterF.glsl", GL_FRAGMENT_SHADER));
        gUnderWaterProgram.mShaderLevel = mShaderLevel[SHADER_WATER];
        gUnderWaterProgram.mShaderGroup = LLGLSLShader::SG_WATER;
        gUnderWaterProgram.clearPermutations();
        if (LLPipeline::sRenderTransparentWater)
        {
            gUnderWaterProgram.addPermutation("TRANSPARENT_WATER", "1");
        }
        success = gUnderWaterProgram.createShader(mirror_variant());
        llassert(success);
    }

    /// Keep track of water shader levels
    if (gWaterProgram.mShaderLevel != mShaderLevel[SHADER_WATER]
        || gUnderWaterProgram.mShaderLevel != mShaderLevel[SHADER_WATER])
    {
        mShaderLevel[SHADER_WATER] = llmin(gWaterProgram.mShaderLevel, gUnderWaterProgram.mShaderLevel);
    }

    if (!success)
    {
        mShaderLevel[SHADER_WATER] = 0;
        return false;
    }

    // if we failed to load the terrain water shaders and we need them (using class2 water),
    // then drop down to class1 water.
    if (mShaderLevel[SHADER_WATER] > 1 && !terrainWaterSuccess)
    {
        mShaderLevel[SHADER_WATER]--;
        return loadShadersWater();
    }

    if (LLWorld::instanceExists())
    {
        LLWorld::getInstance()->updateWaterObjects();
    }

    return true;
}

bool LLViewerShaderMgr::loadShadersEffects()
{
    LL_PROFILE_ZONE_SCOPED;
    bool success = true;

    if (mShaderLevel[SHADER_EFFECT] == 0)
    {
        gGlowProgram.unload();
        gGlowExtractProgram.unload();
        gBloomExtractProgram.unload();
        gBloomDownsampleProgram.unload();
        gBloomDownsampleFirstProgram.unload();
        gBloomUpsampleProgram.unload();
        gBloomCompositeProgram.unload();
        return true;
    }

    if (success)
    {
        gGlowProgram.mName = "Glow Shader (Post)";
        gGlowProgram.mShaderFiles.clear();
        gGlowProgram.mShaderFiles.push_back(make_pair("effects/glowV.glsl", GL_VERTEX_SHADER));
        gGlowProgram.mShaderFiles.push_back(make_pair("effects/glowF.glsl", GL_FRAGMENT_SHADER));
        gGlowProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];
        success = gGlowProgram.createShader();
        if (!success)
        {
            LLPipeline::sRenderGlow = false;
        }
    }

    if (success)
    {
        const bool use_glow_noise = gSavedSettings.getBOOL("RenderGlowNoise");
        const std::string glow_noise_label = use_glow_noise ? " (+Noise)" : "";

        gGlowExtractProgram.mName = llformat("Glow Extract Shader (Post)%s", glow_noise_label.c_str());
        gGlowExtractProgram.mShaderFiles.clear();
        gGlowExtractProgram.mShaderFiles.push_back(make_pair("effects/glowExtractV.glsl", GL_VERTEX_SHADER));
        gGlowExtractProgram.mShaderFiles.push_back(make_pair("effects/glowExtractF.glsl", GL_FRAGMENT_SHADER));
        gGlowExtractProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];

        if (use_glow_noise)
        {
            gGlowExtractProgram.addPermutation("HAS_NOISE", "1");
        }

        success = gGlowExtractProgram.createShader();
        if (!success)
        {
            LLPipeline::sRenderGlow = false;
        }
    }

    const bool bloom_halation = gSavedSettings.getBOOL("RenderBloomHalation");

    if (success)
    {
        gBloomExtractProgram.mName = "HDR Bloom Extract";
        gBloomExtractProgram.mShaderFiles.clear();
        gBloomExtractProgram.mShaderFiles.push_back(make_pair("effects/glowExtractV.glsl", GL_VERTEX_SHADER));
        gBloomExtractProgram.mShaderFiles.push_back(make_pair("effects/bloomExtractF.glsl", GL_FRAGMENT_SHADER));
        gBloomExtractProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];
        if (bloom_halation)
        {
            gBloomExtractProgram.addPermutation("BLOOM_HALATION", "1");
        }
        success = gBloomExtractProgram.createShader();
    }

    if (success)
    {
        gBloomDownsampleFirstProgram.mName = "HDR Bloom Downsample (First)";
        gBloomDownsampleFirstProgram.mShaderFiles.clear();
        gBloomDownsampleFirstProgram.mShaderFiles.push_back(make_pair("effects/glowExtractV.glsl", GL_VERTEX_SHADER));
        gBloomDownsampleFirstProgram.mShaderFiles.push_back(make_pair("effects/bloomDownsampleF.glsl", GL_FRAGMENT_SHADER));
        gBloomDownsampleFirstProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];
        gBloomDownsampleFirstProgram.addPermutation("FIRST_DOWNSAMPLE", "1");
        success = gBloomDownsampleFirstProgram.createShader();
    }

    if (success)
    {
        gBloomDownsampleProgram.mName = "HDR Bloom Downsample";
        gBloomDownsampleProgram.mShaderFiles.clear();
        gBloomDownsampleProgram.mShaderFiles.push_back(make_pair("effects/glowExtractV.glsl", GL_VERTEX_SHADER));
        gBloomDownsampleProgram.mShaderFiles.push_back(make_pair("effects/bloomDownsampleF.glsl", GL_FRAGMENT_SHADER));
        gBloomDownsampleProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];
        success = gBloomDownsampleProgram.createShader();
    }

    if (success)
    {
        gBloomUpsampleProgram.mName = "HDR Bloom Upsample";
        gBloomUpsampleProgram.mShaderFiles.clear();
        gBloomUpsampleProgram.mShaderFiles.push_back(make_pair("effects/glowExtractV.glsl", GL_VERTEX_SHADER));
        gBloomUpsampleProgram.mShaderFiles.push_back(make_pair("effects/bloomUpsampleF.glsl", GL_FRAGMENT_SHADER));
        gBloomUpsampleProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];
        success = gBloomUpsampleProgram.createShader();
    }

    if (success)
    {
        gBloomCompositeProgram.mName = "HDR Bloom Composite";
        gBloomCompositeProgram.mShaderFiles.clear();
        gBloomCompositeProgram.mShaderFiles.push_back(make_pair("effects/glowExtractV.glsl", GL_VERTEX_SHADER));
        gBloomCompositeProgram.mShaderFiles.push_back(make_pair("effects/bloomCompositeF.glsl", GL_FRAGMENT_SHADER));
        gBloomCompositeProgram.mShaderLevel = mShaderLevel[SHADER_EFFECT];
        if (bloom_halation)
        {
            gBloomCompositeProgram.addPermutation("BLOOM_HALATION", "1");
        }
        success = gBloomCompositeProgram.createShader();
    }

    return success;

}

bool LLViewerShaderMgr::loadShadersDeferred()
{
    LL_PROFILE_ZONE_SCOPED;
    bool use_sun_shadow = mShaderLevel[SHADER_DEFERRED] > 1 &&
        gSavedSettings.getS32("RenderShadowDetail") > 0;

    if (mShaderLevel[SHADER_DEFERRED] == 0)
    {
        gDeferredTreeProgram.unload();
        gDeferredTreeShadowProgram.unload();
        gDeferredDiffuseProgram.unload();
        gDeferredDiffuseAlphaMaskProgram.unload();
        gDeferredNonIndexedDiffuseAlphaMaskProgram.unload();
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.unload();
        gDeferredBumpProgram.unload();
        gDeferredImpostorProgram.unload();
        gDeferredTerrainProgram.unload();
        gDeferredLightProgram.unload();
        for (U32 i = 0; i < LL_DEFERRED_MULTI_LIGHT_COUNT; ++i)
        {
            gDeferredMultiLightProgram[i].unload();
        }
        gDeferredSpotLightProgram.unload();
        gDeferredMultiSpotLightProgram.unload();
        gDeferredSunProgram.unload();
        gDeferredBlurLightProgram.unload();
        gDeferredSoftenProgram.unload();
        gDeferredShadowProgram.unload();
        gDeferredShadowCubeProgram.unload();
        gDeferredShadowAlphaMaskProgram.unload();
        gDeferredShadowGLTFAlphaMaskProgram.unload();
        gDeferredShadowGLTFAlphaMaskIndexedProgram.unload();
        gDeferredShadowMaterialIndexedProgram.unload();
        gDeferredShadowFullbrightAlphaMaskProgram.unload();
        gDeferredAvatarShadowProgram.unload();
        gDeferredAvatarAlphaShadowProgram.unload();
        gDeferredAvatarAlphaMaskShadowProgram.unload();
        gDeferredAvatarProgram.unload();
        gDeferredAvatarAlphaProgram.unload();
        gDeferredAlphaProgram.unload();
        gHUDAlphaProgram.unload();
        gDeferredFullbrightProgram.unload();
        gHUDFullbrightProgram.unload();
        gDeferredFullbrightAlphaMaskProgram.unload();
        gHUDFullbrightAlphaMaskProgram.unload();
        gDeferredFullbrightAlphaMaskAlphaProgram.unload();
        gHUDFullbrightAlphaMaskAlphaProgram.unload();
        gDeferredEmissiveProgram.unload();
        gDeferredEmissiveIndexedProgram.unload();
        gDeferredPostProgram.unload();
        gDeferredCoFProgram.unload();
        gDeferredDoFCombineProgram.unload();
        gExposureProgram.unload();
        gExposureProgramNoFade.unload();
        gLuminanceProgram.unload();

        for (auto i = 0; i < 4; ++i)
        {
            gFXAAProgram[i].unload();
            gSMAAEdgeDetectProgram[i].unload();
            gSMAABlendWeightsProgram[i].unload();
            gSMAANeighborhoodBlendProgram[i].unload();
        }
        gCASProgram.unload();
        gEnvironmentMapProgram.unload();
        gDeferredWLSkyProgram.unload();
        gDeferredWLCloudProgram.unload();
        gDeferredWLSunProgram.unload();
        gDeferredWLMoonProgram.unload();
        gDeferredStarProgram.unload();
        gDeferredMeteorProgram.unload();
        gDeferredAuroraProgram.unload();
        gDeferredFullbrightShinyProgram.unload();
        gHUDFullbrightShinyProgram.unload();

        gDeferredHighlightProgram.unload();

        gNormalMapGenProgram.unload();
        gDeferredGenBrdfLutProgram.unload();
        gDeferredBufferVisualProgram.unload();

        for (U32 i = 0; i < LLMaterial::SHADER_COUNT; ++i)
        {
            gDeferredMaterialProgram[i].unload();
            gDeferredMaterialIndexedProgram[i].unload();
        }
        LLGLSLShader::sIndexedLegacyMaterials = false;

        gHUDPBROpaqueProgram.unload();
        gPBRGlowProgram.unload();
        gPBRGlowIndexedProgram.unload();
        gDeferredPBROpaqueProgram.unload();
        gDeferredPBROpaqueIndexedProgram.unload();
        gDeferredPBRAlphaProgram.unload();
        gDeferredPBRAlphaImpostorProgram.unload();
        for (U32 paint_type = 0; paint_type < TERRAIN_PAINT_TYPE_COUNT; ++paint_type)
        {
            gDeferredPBRTerrainProgram[paint_type].unload();
        }

// [RLVa:KB] - @setsphere
        gRlvSphereProgram.unload();
// [/RLVa:KB]

        return true;
    }

    bool success = true;

    if (success)
    {
        gDeferredHighlightProgram.mName = "Deferred Highlight Shader";
        gDeferredHighlightProgram.mShaderFiles.clear();
        gDeferredHighlightProgram.mShaderFiles.push_back(make_pair("interface/highlightV.glsl", GL_VERTEX_SHADER));
        gDeferredHighlightProgram.mShaderFiles.push_back(make_pair("deferred/highlightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredHighlightProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        add_common_permutations(&gDeferredHighlightProgram);
        success = gDeferredHighlightProgram.createShader();
    }

    if (success)
    {
        gDeferredDiffuseProgram.mName = "Deferred Diffuse Shader";
        gDeferredDiffuseProgram.mFeatures.hasSrgb = true;
        gDeferredDiffuseProgram.mShaderFiles.clear();
        gDeferredDiffuseProgram.mShaderFiles.push_back(make_pair("deferred/diffuseV.glsl", GL_VERTEX_SHADER));
        gDeferredDiffuseProgram.mShaderFiles.push_back(make_pair("deferred/diffuseIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredDiffuseProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gDeferredDiffuseProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredDiffuseProgram);
        gDeferredDiffuseProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredDiffuseProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
    }

    if (success)
    {
        gDeferredDiffuseAlphaMaskProgram.mName = "Deferred Diffuse Alpha Mask Shader";
        gDeferredDiffuseAlphaMaskProgram.mShaderFiles.clear();
        gDeferredDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseV.glsl", GL_VERTEX_SHADER));
        gDeferredDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseAlphaMaskIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredDiffuseAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gDeferredDiffuseAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredDiffuseAlphaMaskProgram);
        gDeferredDiffuseAlphaMaskProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredDiffuseAlphaMaskProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
    }

    if (success)
    {
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mName = "Deferred Diffuse Non-Indexed Alpha Mask Shader";
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderFiles.clear();
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseV.glsl", GL_VERTEX_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/diffuseAlphaMaskF.glsl", GL_FRAGMENT_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredNonIndexedDiffuseAlphaMaskProgram);
        gDeferredNonIndexedDiffuseAlphaMaskProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredNonIndexedDiffuseAlphaMaskProgram.createShader(mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mName = "Deferred Diffuse Non-Indexed Alpha Mask No Color Shader";
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderFiles.clear();
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("deferred/diffuseNoColorV.glsl", GL_VERTEX_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("deferred/diffuseAlphaMaskNoColorF.glsl", GL_FRAGMENT_SHADER));
        gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram);
        success = gDeferredNonIndexedDiffuseAlphaMaskNoColorProgram.createShader(mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gDeferredBumpProgram.mName = "Deferred Bump Shader";
        gDeferredBumpProgram.mShaderFiles.clear();
        gDeferredBumpProgram.mShaderFiles.push_back(make_pair("deferred/bumpV.glsl", GL_VERTEX_SHADER));
        gDeferredBumpProgram.mShaderFiles.push_back(make_pair("deferred/bumpF.glsl", GL_FRAGMENT_SHADER));
        gDeferredBumpProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        add_common_permutations(&gDeferredBumpProgram);
        gDeferredBumpProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredBumpProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
        llassert(success);
    }

    gDeferredMaterialProgram[1].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[5].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[9].mFeatures.hasLighting = false;
    gDeferredMaterialProgram[13].mFeatures.hasLighting = false;

    for (U32 i = 0; i < LLMaterial::SHADER_COUNT; ++i)
    {
        if (success)
        {
            gDeferredMaterialProgram[i].mName = llformat("Material Shader %d", i);

            U32 alpha_mode = i & 0x3;

            gDeferredMaterialProgram[i].mShaderFiles.clear();
            gDeferredMaterialProgram[i].mShaderFiles.push_back(make_pair("deferred/materialV.glsl", GL_VERTEX_SHADER));
            gDeferredMaterialProgram[i].mShaderFiles.push_back(make_pair("deferred/materialF.glsl", GL_FRAGMENT_SHADER));
            gDeferredMaterialProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];

            gDeferredMaterialProgram[i].clearPermutations();

            bool has_normal_map   = (i & 0x8) > 0;
            bool has_specular_map = (i & 0x4) > 0;

            if (has_normal_map)
            {
                gDeferredMaterialProgram[i].addPermutation("HAS_NORMAL_MAP", "1");
            }

            if (has_specular_map)
            {
                gDeferredMaterialProgram[i].addPermutation("HAS_SPECULAR_MAP", "1");
            }

            gDeferredMaterialProgram[i].addPermutation("DIFFUSE_ALPHA_MODE", llformat("%d", alpha_mode));

            if (alpha_mode != 0)
            {
                gDeferredMaterialProgram[i].mFeatures.hasAlphaMask = true;
                gDeferredMaterialProgram[i].addPermutation("HAS_ALPHA_MASK", "1");
            }

            if (use_sun_shadow)
            {
                gDeferredMaterialProgram[i].addPermutation("HAS_SUN_SHADOW", "1");
            }

            add_common_permutations(&gDeferredMaterialProgram[i]);

            gDeferredMaterialProgram[i].mFeatures.hasSrgb = true;
            gDeferredMaterialProgram[i].mFeatures.calculatesAtmospherics = true;
            gDeferredMaterialProgram[i].mFeatures.hasAtmospherics = true;
            gDeferredMaterialProgram[i].mFeatures.hasGamma = true;
            gDeferredMaterialProgram[i].mFeatures.hasShadows = use_sun_shadow;
            gDeferredMaterialProgram[i].mFeatures.hasReflectionProbes = true;

            gDeferredMaterialProgram[i].addPermutation("LINEAR_DIFFUSE", "1");

            // Only the forward alpha-blend mask compiles the classic_mode branches in
            // class3/materialF.glsl; every other mask is a GBuffer writer.
            U32 variants = LLGLSLShader::VARIANT_RIGGED | mirror_variant();
            if (alpha_mode == 1)
            {
                variants |= LLGLSLShader::VARIANT_CLASSIC;
            }
            success = gDeferredMaterialProgram[i].createShader(variants);
            llassert(success);
        }
    }

    for (U32 i : { 1u, 5u, 9u, 13u })
    {
        gDeferredMaterialProgram[i].forEachVariant([](LLGLSLShader& s) { s.mFeatures.hasLighting = true; });
    }

    // Clear any stale value from a previous load before (re)deciding legacy indexed
    // eligibility -- if the block below is skipped or fails partway, the flag must
    // not carry a prior 'true' while the indexed programs are unloaded/incomplete.
    LLGLSLShader::sIndexedLegacyMaterials = false;

    if (success && LLGLSLShader::sIndexedGLTFChannels >= 2)
    {
        // Indexed (multi-material) legacy material GBuffer-write programs, parallel to
        // gDeferredMaterialProgram but covering only the non-blend (GBuffer) masks and
        // sampling the GBuffer-relevant maps only. Optional: failure leaves
        // sIndexedLegacyMaterials false so legacy batching is skipped (the pool falls
        // back to scalar). Kept out of the `success` chain.
        bool material_indexed_ok = true;
        for (U32 i = 0; i < LLMaterial::SHADER_COUNT && material_indexed_ok; ++i)
        {
            U32 alpha_mode = i & 0x3;
            if (alpha_mode == 1) // DIFFUSE_ALPHA_MODE_BLEND -- forward/alpha pool, not indexed
            {
                continue;
            }

            bool has_spec   = (i & 0x4) != 0;
            bool has_normal = (i & 0x8) != 0;

            LLGLSLShader& prog = gDeferredMaterialIndexedProgram[i];
            prog.mName = llformat("Material Indexed Shader %d", i);
            // Converts specular between sRGB and linear in-shader, so it needs
            // environment/srgbF attached -- see attachShaderFeatures.
            prog.mFeatures.hasSrgb = true;
            prog.mShaderFiles.clear();
            prog.mShaderFiles.push_back(make_pair("deferred/materialIndexedV.glsl", GL_VERTEX_SHADER));
            prog.mShaderFiles.push_back(make_pair("deferred/materialIndexedF.glsl", GL_FRAGMENT_SHADER));
            prog.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
            prog.clearPermutations();
            prog.addPermutation("LINEAR_DIFFUSE", "1");
            if (has_normal) prog.addPermutation("HAS_NORMAL_MAP", "1");
            if (has_spec)   prog.addPermutation("HAS_SPECULAR_MAP", "1");
            prog.addPermutation("DIFFUSE_ALPHA_MODE", llformat("%d", alpha_mode));
            prog.addPermutation("GLTF_INDEXED_CHANNELS", llformat("%d", LLGLSLShader::sIndexedGLTFChannels));
            add_common_permutations(&prog);

            material_indexed_ok = prog.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
            if (material_indexed_ok)
            {
                const S32 n = LLGLSLShader::sIndexedGLTFChannels;
                prog.forEachVariant([n, has_normal, has_spec](LLGLSLShader& s)
                { setup_material_indexed_samplers(s, n, has_normal, has_spec); });
            }
        }

        if (material_indexed_ok)
        {
            LLGLSLShader::sIndexedLegacyMaterials = true;
        }
        else
        {
            LL_WARNS("ShaderLoading") << "Indexed legacy material shaders failed to load; legacy batching disabled." << LL_ENDL;
            for (U32 i = 0; i < LLMaterial::SHADER_COUNT; ++i)
            {
                gDeferredMaterialIndexedProgram[i].unload();
            }
        }
    }

    if (success)
    {
        gDeferredPBROpaqueProgram.mName = "Deferred PBR Opaque Shader";
        gDeferredPBROpaqueProgram.mFeatures.hasSrgb = true;

        gDeferredPBROpaqueProgram.mShaderFiles.clear();
        gDeferredPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueV.glsl", GL_VERTEX_SHADER));
        gDeferredPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPBROpaqueProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredPBROpaqueProgram.clearPermutations();

        add_common_permutations(&gDeferredPBROpaqueProgram);

        success = gDeferredPBROpaqueProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
        llassert(success);
    }

    if (success && LLGLSLShader::sIndexedGLTFChannels >= 2)
    {
        // Indexed (multi-material) PBR opaque. Optional acceleration: failure here
        // disables GLTF batching but must NOT fail overall shader loading, so the
        // result is kept out of the `success` chain.
        gDeferredPBROpaqueIndexedProgram.mName = "Deferred PBR Opaque Indexed Shader";
        gDeferredPBROpaqueIndexedProgram.mFeatures.hasSrgb = true;
        gDeferredPBROpaqueIndexedProgram.mShaderFiles.clear();
        gDeferredPBROpaqueIndexedProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueIndexedV.glsl", GL_VERTEX_SHADER));
        gDeferredPBROpaqueIndexedProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPBROpaqueIndexedProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredPBROpaqueIndexedProgram.clearPermutations();
        gDeferredPBROpaqueIndexedProgram.addPermutation("GLTF_INDEXED_CHANNELS", llformat("%d", LLGLSLShader::sIndexedGLTFChannels));
        add_common_permutations(&gDeferredPBROpaqueIndexedProgram);

        // rigged (skinned) variant for animesh / avatar attachments
        bool indexed_ok = gDeferredPBROpaqueIndexedProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());

        if (indexed_ok)
        {
            // Map each slot's four material samplers to texture units, on every variant.
            const S32 n = LLGLSLShader::sIndexedGLTFChannels;
            gDeferredPBROpaqueIndexedProgram.forEachVariant([n](LLGLSLShader& s) { setup_gltf_indexed_samplers(s, n, true); });
        }
        else
        {
            // Degrade gracefully: route all PBR faces back to the scalar path.
            LL_WARNS("ShaderLoading") << "Indexed PBR shader failed to load; GLTF batching disabled." << LL_ENDL;
            gDeferredPBROpaqueIndexedProgram.unload();
            LLGLSLShader::sIndexedGLTFChannels = 0;

            // The legacy material indexed programs were built earlier (above) with the
            // now-stale channel count and share sIndexedGLTFChannels. Tear them down so
            // the invariant sIndexedLegacyMaterials => sIndexedGLTFChannels >= 2 holds.
            if (LLGLSLShader::sIndexedLegacyMaterials)
            {
                for (U32 i = 0; i < LLMaterial::SHADER_COUNT; ++i)
                {
                    gDeferredMaterialIndexedProgram[i].unload();
                }
                LLGLSLShader::sIndexedLegacyMaterials = false;
            }
        }
    }
    else
    {
        LLGLSLShader::sIndexedGLTFChannels = 0;
    }

    if (success)
    {
        gPBRGlowProgram.mName = " PBR Glow Shader";
        gPBRGlowProgram.mFeatures.hasSrgb = true;
        gPBRGlowProgram.mShaderFiles.clear();
        gPBRGlowProgram.mShaderFiles.push_back(make_pair("deferred/pbrglowV.glsl", GL_VERTEX_SHADER));
        gPBRGlowProgram.mShaderFiles.push_back(make_pair("deferred/pbrglowF.glsl", GL_FRAGMENT_SHADER));
        gPBRGlowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gPBRGlowProgram);

        success = gPBRGlowProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success && LLGLSLShader::sIndexedGLTFChannels >= 2)
    {
        // Indexed (multi-material) PBR glow, parallel to gPBRGlowProgram. Shares the
        // GBuffer indexed sampler-unit layout (base color s, emissive 3N+s) so
        // pushGLTFBatchIndexed drives it directly. Optional: failure leaves the
        // program incomplete and the pool falls back to scalar glow. Kept out of the
        // `success` chain.
        gPBRGlowIndexedProgram.mName = "PBR Glow Indexed Shader";
        gPBRGlowIndexedProgram.mFeatures.hasSrgb = true;
        gPBRGlowIndexedProgram.mShaderFiles.clear();
        gPBRGlowIndexedProgram.mShaderFiles.push_back(make_pair("deferred/pbrglowIndexedV.glsl", GL_VERTEX_SHADER));
        gPBRGlowIndexedProgram.mShaderFiles.push_back(make_pair("deferred/pbrglowIndexedF.glsl", GL_FRAGMENT_SHADER));
        gPBRGlowIndexedProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gPBRGlowIndexedProgram.clearPermutations();
        gPBRGlowIndexedProgram.addPermutation("GLTF_INDEXED_CHANNELS", llformat("%d", LLGLSLShader::sIndexedGLTFChannels));
        add_common_permutations(&gPBRGlowIndexedProgram);

        bool glow_indexed_ok = gPBRGlowIndexedProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        if (glow_indexed_ok)
        {
            S32 n = LLGLSLShader::sIndexedGLTFChannels;
            gPBRGlowIndexedProgram.forEachVariant([n](LLGLSLShader& s) { setup_gltf_indexed_samplers(s, n, true); });
        }
        else
        {
            LL_WARNS("ShaderLoading") << "Indexed PBR glow shader failed to load; multi-material glow falls back to scalar." << LL_ENDL;
            gPBRGlowIndexedProgram.unload();
        }
    }

    if (success)
    {
        gHUDPBROpaqueProgram.mName = "HUD PBR Opaque Shader";
        gHUDPBROpaqueProgram.mFeatures.hasSrgb = true;
        gHUDPBROpaqueProgram.mShaderFiles.clear();
        gHUDPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueV.glsl", GL_VERTEX_SHADER));
        gHUDPBROpaqueProgram.mShaderFiles.push_back(make_pair("deferred/pbropaqueF.glsl", GL_FRAGMENT_SHADER));
        gHUDPBROpaqueProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gHUDPBROpaqueProgram.clearPermutations();
        gHUDPBROpaqueProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDPBROpaqueProgram);

        success = gHUDPBROpaqueProgram.createShader();

        llassert(success);
    }

    if (success)
    {
        LLGLSLShader* shader = &gDeferredPBRAlphaProgram;
        shader->mName = "Deferred PBR Alpha Shader";

        shader->mFeatures.calculatesLighting = false;
        shader->mFeatures.hasLighting = false;
        shader->mFeatures.isAlphaLighting = true;
        shader->mFeatures.hasSrgb = true;
        shader->mFeatures.calculatesAtmospherics = true;
        shader->mFeatures.hasAtmospherics = true;
        shader->mFeatures.hasGamma = true;
        shader->mFeatures.hasShadows = use_sun_shadow;
        shader->mFeatures.isDeferred = true; // include deferredUtils
        shader->mFeatures.hasReflectionProbes = mShaderLevel[SHADER_DEFERRED];

        shader->mShaderFiles.clear();
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaV.glsl", GL_VERTEX_SHADER));
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaF.glsl", GL_FRAGMENT_SHADER));

        shader->clearPermutations();

        U32 alpha_mode = LLMaterial::DIFFUSE_ALPHA_MODE_BLEND;
        shader->addPermutation("DIFFUSE_ALPHA_MODE", llformat("%d", alpha_mode));
        shader->addPermutation("HAS_NORMAL_MAP", "1");
        shader->addPermutation("HAS_SPECULAR_MAP", "1"); // PBR: Packed: Occlusion, Metal, Roughness
        shader->addPermutation("HAS_EMISSIVE_MAP", "1");
        shader->addPermutation("USE_VERTEX_COLOR", "1");

        add_common_permutations(shader);

        if (use_sun_shadow)
        {
            shader->addPermutation("HAS_SUN_SHADOW", "1");
        }

        shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = shader->createShader(LLGLSLShader::VARIANT_RIGGED | LLGLSLShader::VARIANT_CLASSIC | mirror_variant());
        llassert(success);

        // Alpha Shader Hack
        // See: LLRender::syncMatrices()
        shader->forEachVariant([](LLGLSLShader& s)
        {
            s.mFeatures.calculatesLighting = true;
            s.mFeatures.hasLighting = true;
        });
    }

    if (success)
    {
        // The impostor bake's variant: flat base colour, no lighting. A bake captures ALBEDO
        // for a G-buffer that is lit once at composite time, so the fully lit program above
        // would light blended PBR twice. gDeferredAlphaImpostorProgram is the same idea for
        // the legacy path; this closes the PBR half.
        //
        // Same feature set as the program above, deliberately, even though the FOR_IMPOSTOR
        // branch never reaches the lighting. Features select which shared objects get
        // ATTACHED, and pbralphaF's prologue calls mirrorClip() and waterClip() before the
        // branch -- waterClip lives in deferredUtil.glsl, which only isDeferred or
        // hasReflectionProbes attaches. Trimming the set to what the branch appears to need
        // links against an undefined waterClip. It is the same fragment source, so it wants
        // the same attachments; the permutations are what make this program flat.
        LLGLSLShader* shader = &gDeferredPBRAlphaImpostorProgram;
        shader->mName = "Deferred PBR Alpha Impostor Shader";

        shader->mFeatures.calculatesLighting = false;
        shader->mFeatures.hasLighting = false;
        shader->mFeatures.isAlphaLighting = true;
        shader->mFeatures.hasSrgb = true;
        shader->mFeatures.calculatesAtmospherics = true;
        shader->mFeatures.hasAtmospherics = true;
        shader->mFeatures.hasGamma = true;
        shader->mFeatures.isDeferred = true; // include deferredUtils
        shader->mFeatures.hasReflectionProbes = mShaderLevel[SHADER_DEFERRED];

        shader->mShaderFiles.clear();
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaV.glsl", GL_VERTEX_SHADER));
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaF.glsl", GL_FRAGMENT_SHADER));

        shader->clearPermutations();

        U32 alpha_mode = LLMaterial::DIFFUSE_ALPHA_MODE_BLEND;
        shader->addPermutation("DIFFUSE_ALPHA_MODE", llformat("%d", alpha_mode));
        shader->addPermutation("FOR_IMPOSTOR", "1");
        shader->addPermutation("USE_VERTEX_COLOR", "1");

        add_common_permutations(shader);

        shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = shader->createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success)
    {
        LLGLSLShader* shader = &gHUDPBRAlphaProgram;
        shader->mName = "HUD PBR Alpha Shader";

        shader->mFeatures.hasSrgb = true;

        shader->mShaderFiles.clear();
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaV.glsl", GL_VERTEX_SHADER));
        shader->mShaderFiles.push_back(make_pair("deferred/pbralphaF.glsl", GL_FRAGMENT_SHADER));

        shader->clearPermutations();

        shader->addPermutation("IS_HUD", "1");

        add_common_permutations(shader);

        shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = shader->createShader(LLGLSLShader::VARIANT_CLASSIC);
        llassert(success);
    }

    if (success)
    {
        const S32 detail = clamp_terrain_detail(gSavedSettings.getS32("RenderTerrainPBRDetail"));
        const S32 mapping = clamp_terrain_mapping(gSavedSettings.getS32("RenderTerrainPBRPlanarSampleCount"));
        for (U32 paint_type = 0; paint_type < TERRAIN_PAINT_TYPE_COUNT; ++paint_type)
        {
            LLGLSLShader* shader = &gDeferredPBRTerrainProgram[paint_type];
            shader->mName = llformat("Deferred PBR Terrain Shader %d %s %s",
                    detail,
                    (paint_type == TERRAIN_PAINT_TYPE_PBR_PAINTMAP ? "paintmap" : "heightmap-with-noise"),
                    (mapping == 1 ? "flat" : "triplanar"));
            shader->mFeatures.hasSrgb = true;
            shader->mFeatures.isAlphaLighting = true;
            shader->mFeatures.calculatesAtmospherics = true;
            shader->mFeatures.hasAtmospherics = true;
            shader->mFeatures.hasGamma = true;
            shader->mFeatures.hasTransport = true;
            shader->mFeatures.isPBRTerrain = true;

            shader->mShaderFiles.clear();
            shader->mShaderFiles.push_back(make_pair("deferred/pbrterrainV.glsl", GL_VERTEX_SHADER));
            shader->mShaderFiles.push_back(make_pair("deferred/pbrterrainF.glsl", GL_FRAGMENT_SHADER));
            shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
            shader->addPermutation("TERRAIN_PBR_DETAIL", llformat("%d", detail));
            shader->addPermutation("TERRAIN_PAINT_TYPE", llformat("%d", paint_type));
            shader->addPermutation("TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT", llformat("%d", mapping));

            add_common_permutations(shader);

            success = success && shader->createShader(mirror_variant());
            llassert(success);
        }
    }

    if (success)
    {
        gDeferredTreeProgram.mName = "Deferred Tree Shader";
        gDeferredTreeProgram.mShaderFiles.clear();
        gDeferredTreeProgram.mShaderFiles.push_back(make_pair("deferred/treeV.glsl", GL_VERTEX_SHADER));
        gDeferredTreeProgram.mShaderFiles.push_back(make_pair("deferred/treeF.glsl", GL_FRAGMENT_SHADER));
        gDeferredTreeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredTreeProgram);

        gDeferredTreeProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredTreeProgram.createShader(mirror_variant());
    }

    if (success)
    {
        gDeferredTreeShadowProgram.mName = "Deferred Tree Shadow Shader";
        gDeferredTreeShadowProgram.mShaderFiles.clear();
        gDeferredTreeShadowProgram.mShaderFiles.push_back(make_pair("deferred/treeShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredTreeShadowProgram.mShaderFiles.push_back(make_pair("deferred/treeShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredTreeShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredTreeShadowProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success)
    {
        gDeferredImpostorProgram.mName = "Deferred Impostor Shader";
        gDeferredImpostorProgram.mFeatures.hasSrgb = true;
        gDeferredImpostorProgram.mShaderFiles.clear();
        gDeferredImpostorProgram.mShaderFiles.push_back(make_pair("deferred/impostorV.glsl", GL_VERTEX_SHADER));
        gDeferredImpostorProgram.mShaderFiles.push_back(make_pair("deferred/impostorF.glsl", GL_FRAGMENT_SHADER));
        gDeferredImpostorProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredImpostorProgram);

        success = gDeferredImpostorProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredLightProgram.mName = "Deferred Light Shader";
        gDeferredLightProgram.mFeatures.isDeferred = true;
        gDeferredLightProgram.mFeatures.hasFullGBuffer = true;
        gDeferredLightProgram.mFeatures.hasShadows = true;
        gDeferredLightProgram.mFeatures.hasSrgb = true;

        gDeferredLightProgram.mShaderFiles.clear();
        gDeferredLightProgram.mShaderFiles.push_back(make_pair("deferred/pointLightV.glsl", GL_VERTEX_SHADER));
        gDeferredLightProgram.mShaderFiles.push_back(make_pair("deferred/pointLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        gDeferredLightProgram.clearPermutations();

        add_common_permutations(&gDeferredLightProgram);

        success = gDeferredLightProgram.createShader(LLGLSLShader::VARIANT_CLASSIC);
        llassert(success);
    }

    for (U32 i = 0; i < LL_DEFERRED_MULTI_LIGHT_COUNT; i++)
    {
        if (success)
        {
            gDeferredMultiLightProgram[i].mName = llformat("Deferred MultiLight Shader %d", i);
            gDeferredMultiLightProgram[i].mFeatures.isDeferred = true;
            gDeferredMultiLightProgram[i].mFeatures.hasFullGBuffer = true;
            gDeferredMultiLightProgram[i].mFeatures.hasShadows = true;
            gDeferredMultiLightProgram[i].mFeatures.hasSrgb = true;

            gDeferredMultiLightProgram[i].clearPermutations();
            gDeferredMultiLightProgram[i].mShaderFiles.clear();
            gDeferredMultiLightProgram[i].mShaderFiles.push_back(make_pair("deferred/multiPointLightV.glsl", GL_VERTEX_SHADER));
            gDeferredMultiLightProgram[i].mShaderFiles.push_back(make_pair("deferred/multiPointLightF.glsl", GL_FRAGMENT_SHADER));
            gDeferredMultiLightProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
            gDeferredMultiLightProgram[i].addPermutation("LIGHT_COUNT", llformat("%d", i+1));

            add_common_permutations(&gDeferredMultiLightProgram[i]);

            success = gDeferredMultiLightProgram[i].createShader(LLGLSLShader::VARIANT_CLASSIC);
            llassert(success);
        }
    }

    if (success)
    {
        gDeferredSpotLightProgram.mName = "Deferred SpotLight Shader";
        gDeferredSpotLightProgram.mShaderFiles.clear();
        gDeferredSpotLightProgram.mFeatures.hasSrgb = true;
        gDeferredSpotLightProgram.mFeatures.isDeferred = true;
        gDeferredSpotLightProgram.mFeatures.hasFullGBuffer = true;
        gDeferredSpotLightProgram.mFeatures.hasShadows = true;

        gDeferredSpotLightProgram.clearPermutations();
        gDeferredSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/pointLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/spotLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredSpotLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredSpotLightProgram);

        success = gDeferredSpotLightProgram.createShader(LLGLSLShader::VARIANT_CLASSIC);
        llassert(success);
    }

    if (success)
    {
        gDeferredMultiSpotLightProgram.mName = "Deferred MultiSpotLight Shader";
        gDeferredMultiSpotLightProgram.mFeatures.hasSrgb = true;
        gDeferredMultiSpotLightProgram.mFeatures.isDeferred = true;
        gDeferredMultiSpotLightProgram.mFeatures.hasFullGBuffer = true;
        gDeferredMultiSpotLightProgram.mFeatures.hasShadows = true;

        gDeferredMultiSpotLightProgram.clearPermutations();
        gDeferredMultiSpotLightProgram.addPermutation("MULTI_SPOTLIGHT", "1");
        gDeferredMultiSpotLightProgram.mShaderFiles.clear();
        gDeferredMultiSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/multiPointLightV.glsl", GL_VERTEX_SHADER));
        gDeferredMultiSpotLightProgram.mShaderFiles.push_back(make_pair("deferred/spotLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredMultiSpotLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredMultiSpotLightProgram);

        success = gDeferredMultiSpotLightProgram.createShader(LLGLSLShader::VARIANT_CLASSIC);
        llassert(success);
    }

    if (success)
    {
        std::string fragment;
        bool use_ao = gSavedSettings.getBOOL("RenderDeferredSSAO");
        if (use_ao)
        {
            fragment = "deferred/sunLightSSAOF.glsl";
        }
        else
        {
            fragment = "deferred/sunLightF.glsl";
        }

        gDeferredSunProgram.mName = "Deferred Sun Shader";
        gDeferredSunProgram.mFeatures.isDeferred    = true;
        gDeferredSunProgram.mFeatures.hasShadows    = true;
        gDeferredSunProgram.mFeatures.hasAmbientOcclusion = use_ao;

        gDeferredSunProgram.mShaderFiles.clear();
        gDeferredSunProgram.mShaderFiles.push_back(make_pair("deferred/sunLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSunProgram.mShaderFiles.push_back(make_pair(fragment, GL_FRAGMENT_SHADER));
        gDeferredSunProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredSunProgram);

        success = gDeferredSunProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredSunProbeProgram.mName = "Deferred Sun Probe Shader";
        gDeferredSunProbeProgram.mFeatures.isDeferred = true;
        gDeferredSunProbeProgram.mFeatures.hasShadows = true;

        gDeferredSunProbeProgram.mShaderFiles.clear();
        gDeferredSunProbeProgram.mShaderFiles.push_back(make_pair("deferred/sunLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSunProbeProgram.mShaderFiles.push_back(make_pair("deferred/sunLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredSunProbeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredSunProbeProgram);

        success = gDeferredSunProbeProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredBlurLightProgram.mName = "Deferred Blur Light Shader";
        gDeferredBlurLightProgram.mFeatures.isDeferred = true;

        gDeferredBlurLightProgram.mShaderFiles.clear();
        gDeferredBlurLightProgram.mShaderFiles.push_back(make_pair("deferred/blurLightV.glsl", GL_VERTEX_SHADER));
        gDeferredBlurLightProgram.mShaderFiles.push_back(make_pair("deferred/blurLightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredBlurLightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredBlurLightProgram);

        success = gDeferredBlurLightProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        for (int i = 0; i < 2 && success; ++i)
        {
            bool hud = (i == 1);
            LLGLSLShader* shader = hud ? &gHUDAlphaProgram : &gDeferredAlphaProgram;
            shader->mName = hud ? "HUD Alpha Shader" : "Deferred Alpha Shader";

            shader->mFeatures.calculatesLighting = false;
            shader->mFeatures.hasLighting = false;
            shader->mFeatures.isAlphaLighting = true;
            shader->mFeatures.hasSrgb = true;
            shader->mFeatures.calculatesAtmospherics = true;
            shader->mFeatures.hasAtmospherics = true;
            shader->mFeatures.hasGamma = true;
            shader->mFeatures.hasShadows = use_sun_shadow;
            shader->mFeatures.hasReflectionProbes = true;
            shader->mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
            shader->mShaderFiles.clear();
            shader->mShaderFiles.push_back(make_pair("deferred/alphaV.glsl", GL_VERTEX_SHADER));
            shader->mShaderFiles.push_back(make_pair("deferred/alphaF.glsl", GL_FRAGMENT_SHADER));

            shader->clearPermutations();

            // Forward (non-HUD) decodes its diffuse on the sampler and filters in linear;
            // the HUD path outputs sRGB and keeps the encoded texel. See mLinearDiffuse.
            // The FOR_IMPOSTOR alpha program below takes it too -- the bake target is sRGB
            // and generateImpostor encodes on store, so it shades in linear like the rest.
            if (!hud) shader->addPermutation("LINEAR_DIFFUSE", "1");

            shader->addPermutation("USE_VERTEX_COLOR", "1");
            shader->addPermutation("HAS_ALPHA_MASK", "1");
            shader->addPermutation("USE_INDEXED_TEX", "1");
            if (use_sun_shadow)
            {
                shader->addPermutation("HAS_SUN_SHADOW", "1");
            }

            add_common_permutations(shader);

            if (hud)
            {
                shader->addPermutation("IS_HUD", "1");
            }

            shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];

            // the deferred alpha pass draws rigged geometry and can appear in a mirror; the
            // HUD pass never does either (HUDs are not drawn into the probe)
            U32 variants = LLGLSLShader::VARIANT_CLASSIC;
            if (!hud)
            {
                variants |= LLGLSLShader::VARIANT_RIGGED | mirror_variant();
            }
            success = shader->createShader(variants);
            llassert(success);

            // Hack
            shader->forEachVariant([](LLGLSLShader& s)
            {
                s.mFeatures.calculatesLighting = true;
                s.mFeatures.hasLighting = true;
            });
        }
    }

    if (success)
    {
        LLGLSLShader* shader = &gDeferredAlphaImpostorProgram;

        shader->mName = "Deferred Alpha Impostor Shader";

        // Begin Hack
        shader->mFeatures.calculatesLighting = false;
        shader->mFeatures.hasLighting = false;

        shader->mFeatures.hasSrgb = true;
        shader->mFeatures.isAlphaLighting = true;
        shader->mFeatures.hasShadows = use_sun_shadow;
        shader->mFeatures.hasReflectionProbes = true;
        shader->mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;

        shader->mShaderFiles.clear();
        shader->mShaderFiles.push_back(make_pair("deferred/alphaV.glsl", GL_VERTEX_SHADER));
        shader->mShaderFiles.push_back(make_pair("deferred/alphaF.glsl", GL_FRAGMENT_SHADER));

        shader->clearPermutations();
        shader->addPermutation("USE_INDEXED_TEX", "1");
        shader->addPermutation("FOR_IMPOSTOR", "1");
        shader->addPermutation("HAS_ALPHA_MASK", "1");
        shader->addPermutation("USE_VERTEX_COLOR", "1");
        // Shades in linear like every other forward writer: the bind paths decode the
        // diffuse on the sampler off the derived mLinearDiffuse, and generateImpostor
        // enables GL_FRAMEBUFFER_SRGB so the store re-encodes into the sRGB bake target.
        // This program was the one hole left in that conversion -- it sampled encoded,
        // tinted in gamma space, and wrote raw, which only worked because the pass it
        // ran in did not encode either.
        shader->addPermutation("LINEAR_DIFFUSE", "1");

        if (use_sun_shadow)
        {
            shader->addPermutation("HAS_SUN_SHADOW", "1");
        }

        add_common_permutations(shader);

        shader->mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = shader->createShader(LLGLSLShader::VARIANT_RIGGED | LLGLSLShader::VARIANT_CLASSIC | mirror_variant());
        llassert(success);

        // End Hack
        shader->forEachVariant([](LLGLSLShader& s)
        {
            s.mFeatures.calculatesLighting = true;
            s.mFeatures.hasLighting = true;
        });
    }

    if (success)
    {
        gDeferredFullbrightProgram.mName = "Deferred Fullbright Shader";
        gDeferredFullbrightProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightProgram.mShaderFiles.clear();
        gDeferredFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredFullbrightProgram);

        gDeferredFullbrightProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredFullbrightProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightProgram.mName = "HUD Fullbright Shader";
        gHUDFullbrightProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightProgram.mFeatures.hasGamma = true;
        gHUDFullbrightProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gHUDFullbrightProgram.mShaderFiles.clear();
        gHUDFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gHUDFullbrightProgram.clearPermutations();
        gHUDFullbrightProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightProgram);

        success = gHUDFullbrightProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredFullbrightAlphaMaskProgram.mName = "Deferred Fullbright Alpha Masking Shader";
        gDeferredFullbrightAlphaMaskProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightAlphaMaskProgram.mShaderFiles.clear();
        gDeferredFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightAlphaMaskProgram.clearPermutations();
        gDeferredFullbrightAlphaMaskProgram.addPermutation("HAS_ALPHA_MASK","1");
        gDeferredFullbrightAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredFullbrightAlphaMaskProgram);

        gDeferredFullbrightAlphaMaskProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredFullbrightAlphaMaskProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightAlphaMaskProgram.mName = "HUD Fullbright Alpha Masking Shader";
        gHUDFullbrightAlphaMaskProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.hasGamma = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gHUDFullbrightAlphaMaskProgram.mShaderFiles.clear();
        gHUDFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightAlphaMaskProgram.clearPermutations();
        gHUDFullbrightAlphaMaskProgram.addPermutation("HAS_ALPHA_MASK", "1");
        gHUDFullbrightAlphaMaskProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightAlphaMaskProgram);

        gHUDFullbrightAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gHUDFullbrightAlphaMaskProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredFullbrightAlphaMaskAlphaProgram.mName = "Deferred Fullbright Alpha Masking Alpha Shader";
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.isDeferred = true;
        gDeferredFullbrightAlphaMaskAlphaProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderFiles.clear();
        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightAlphaMaskAlphaProgram.clearPermutations();
        gDeferredFullbrightAlphaMaskAlphaProgram.addPermutation("HAS_ALPHA_MASK", "1");
        gDeferredFullbrightAlphaMaskAlphaProgram.addPermutation("IS_ALPHA", "1");

        add_common_permutations(&gDeferredFullbrightAlphaMaskAlphaProgram);

        gDeferredFullbrightAlphaMaskAlphaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredFullbrightAlphaMaskAlphaProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredFullbrightAlphaMaskAlphaProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightAlphaMaskAlphaProgram.mName = "HUD Fullbright Alpha Masking Alpha Shader";
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.hasGamma = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.isDeferred = true;
        gHUDFullbrightAlphaMaskAlphaProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gHUDFullbrightAlphaMaskAlphaProgram.mShaderFiles.clear();
        gHUDFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightAlphaMaskAlphaProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightAlphaMaskAlphaProgram.clearPermutations();
        gHUDFullbrightAlphaMaskAlphaProgram.addPermutation("HAS_ALPHA_MASK", "1");
        gHUDFullbrightAlphaMaskAlphaProgram.addPermutation("IS_ALPHA", "1");
        gHUDFullbrightAlphaMaskAlphaProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightAlphaMaskAlphaProgram);

        gHUDFullbrightAlphaMaskAlphaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = success && gHUDFullbrightAlphaMaskAlphaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredFullbrightShinyProgram.mName = "Deferred FullbrightShiny Shader";
        gDeferredFullbrightShinyProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredFullbrightShinyProgram.mFeatures.hasAtmospherics = true;
        gDeferredFullbrightShinyProgram.mFeatures.hasGamma = true;
        gDeferredFullbrightShinyProgram.mFeatures.hasSrgb = true;
        gDeferredFullbrightShinyProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gDeferredFullbrightShinyProgram.mShaderFiles.clear();
        gDeferredFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyV.glsl", GL_VERTEX_SHADER));
        gDeferredFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyF.glsl", GL_FRAGMENT_SHADER));
        gDeferredFullbrightShinyProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredFullbrightShinyProgram.mFeatures.hasReflectionProbes = true;

        add_common_permutations(&gDeferredFullbrightShinyProgram);

        gDeferredFullbrightShinyProgram.addPermutation("LINEAR_DIFFUSE", "1");
        success = gDeferredFullbrightShinyProgram.createShader(LLGLSLShader::VARIANT_RIGGED | LLGLSLShader::VARIANT_CLASSIC | mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gHUDFullbrightShinyProgram.mName = "HUD FullbrightShiny Shader";
        gHUDFullbrightShinyProgram.mFeatures.calculatesAtmospherics = true;
        gHUDFullbrightShinyProgram.mFeatures.hasAtmospherics = true;
        gHUDFullbrightShinyProgram.mFeatures.hasGamma = true;
        gHUDFullbrightShinyProgram.mFeatures.hasSrgb = true;
        gHUDFullbrightShinyProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gHUDFullbrightShinyProgram.mShaderFiles.clear();
        gHUDFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyV.glsl", GL_VERTEX_SHADER));
        gHUDFullbrightShinyProgram.mShaderFiles.push_back(make_pair("deferred/fullbrightShinyF.glsl", GL_FRAGMENT_SHADER));
        gHUDFullbrightShinyProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gHUDFullbrightShinyProgram.mFeatures.hasReflectionProbes = true;
        gHUDFullbrightShinyProgram.clearPermutations();
        gHUDFullbrightShinyProgram.addPermutation("IS_HUD", "1");

        add_common_permutations(&gHUDFullbrightShinyProgram);

        success = gHUDFullbrightShinyProgram.createShader(LLGLSLShader::VARIANT_CLASSIC);
        llassert(success);
    }

    if (success)
    {
        gDeferredEmissiveProgram.mName = "Deferred Emissive Shader";
        gDeferredEmissiveProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredEmissiveProgram.mFeatures.hasGamma = true;
        gDeferredEmissiveProgram.mFeatures.hasAtmospherics = true;
        gDeferredEmissiveProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;
        gDeferredEmissiveProgram.mShaderFiles.clear();
        gDeferredEmissiveProgram.mShaderFiles.push_back(make_pair("deferred/emissiveV.glsl", GL_VERTEX_SHADER));
        gDeferredEmissiveProgram.mShaderFiles.push_back(make_pair("deferred/emissiveF.glsl", GL_FRAGMENT_SHADER));
        gDeferredEmissiveProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredEmissiveProgram);

        success = gDeferredEmissiveProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success && LLGLSLShader::sIndexedLegacyMaterials)
    {
        // Indexed (multi-material) legacy glow, parallel to gDeferredEmissiveProgram.
        // Selects each slot's diffuse map (bound to unit s) for the glow alpha mask.
        // Only enabled when legacy material batching is active; failure leaves the
        // program incomplete and the pool falls back to scalar glow. Kept out of the
        // `success` chain.
        gDeferredEmissiveIndexedProgram.mName = "Deferred Emissive Indexed Shader";
        gDeferredEmissiveIndexedProgram.mShaderFiles.clear();
        gDeferredEmissiveIndexedProgram.mShaderFiles.push_back(make_pair("deferred/emissiveIndexedV.glsl", GL_VERTEX_SHADER));
        gDeferredEmissiveIndexedProgram.mShaderFiles.push_back(make_pair("deferred/emissiveIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredEmissiveIndexedProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredEmissiveIndexedProgram.clearPermutations();
        gDeferredEmissiveIndexedProgram.addPermutation("GLTF_INDEXED_CHANNELS", llformat("%d", LLGLSLShader::sIndexedGLTFChannels));
        add_common_permutations(&gDeferredEmissiveIndexedProgram);

        bool emissive_indexed_ok = gDeferredEmissiveIndexedProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        if (emissive_indexed_ok)
        {
            S32 n = LLGLSLShader::sIndexedGLTFChannels;
            gDeferredEmissiveIndexedProgram.forEachVariant([n](LLGLSLShader& s) { setup_material_indexed_samplers(s, n, false, false); });
        }
        else
        {
            LL_WARNS("ShaderLoading") << "Indexed legacy glow shader failed to load; multi-material glow falls back to scalar." << LL_ENDL;
            gDeferredEmissiveIndexedProgram.unload();
        }
    }

    if (success)
    {
        gDeferredSoftenProgram.mName = "Deferred Soften Shader";
        gDeferredSoftenProgram.mShaderFiles.clear();
        gDeferredSoftenProgram.mFeatures.hasSrgb = true;
        gDeferredSoftenProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredSoftenProgram.mFeatures.hasAtmospherics = true;
        gDeferredSoftenProgram.mFeatures.hasGamma = true;
        gDeferredSoftenProgram.mFeatures.isDeferred = true;
        gDeferredSoftenProgram.mFeatures.hasFullGBuffer = true;
        gDeferredSoftenProgram.mFeatures.hasShadows = use_sun_shadow;
        gDeferredSoftenProgram.mFeatures.hasReflectionProbes = mShaderLevel[SHADER_DEFERRED] > 2;

        gDeferredSoftenProgram.clearPermutations();
        add_common_permutations(&gDeferredSoftenProgram);
        gDeferredSoftenProgram.mShaderFiles.push_back(make_pair("deferred/softenLightV.glsl", GL_VERTEX_SHADER));
        gDeferredSoftenProgram.mShaderFiles.push_back(make_pair("deferred/softenLightF.glsl", GL_FRAGMENT_SHADER));

        gDeferredSoftenProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        if (use_sun_shadow)
        {
            gDeferredSoftenProgram.addPermutation("HAS_SUN_SHADOW", "1");
        }

        if (gSavedSettings.getBOOL("RenderDeferredSSAO"))
        { //if using SSAO, take screen space light map into account as if shadows are enabled
            gDeferredSoftenProgram.mShaderLevel = llmax(gDeferredSoftenProgram.mShaderLevel, 2);
            gDeferredSoftenProgram.addPermutation("HAS_SSAO", "1");
        }

        success = gDeferredSoftenProgram.createShader(LLGLSLShader::VARIANT_CLASSIC);
        llassert(success);
    }

    if (success)
    {
        gHazeProgram.mName = "Haze Shader";
        gHazeProgram.mShaderFiles.clear();
        gHazeProgram.mFeatures.hasSrgb                = true;
        gHazeProgram.mFeatures.calculatesAtmospherics = true;
        gHazeProgram.mFeatures.hasAtmospherics        = true;
        gHazeProgram.mFeatures.hasGamma               = true;
        gHazeProgram.mFeatures.isDeferred             = true;
        gHazeProgram.mFeatures.hasShadows             = use_sun_shadow;
        gHazeProgram.mFeatures.hasReflectionProbes    = mShaderLevel[SHADER_DEFERRED] > 2;

        gHazeProgram.clearPermutations();
        gHazeProgram.mShaderFiles.push_back(make_pair("deferred/softenLightV.glsl", GL_VERTEX_SHADER));
        gHazeProgram.mShaderFiles.push_back(make_pair("deferred/hazeF.glsl", GL_FRAGMENT_SHADER));

        add_common_permutations(&gHazeProgram);

        gHazeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        success = gHazeProgram.createShader(LLGLSLShader::VARIANT_CLASSIC);
        llassert(success);
    }


    if (success)
    {
        gHazeWaterProgram.mName = "Water Haze Shader";
        gHazeWaterProgram.mShaderFiles.clear();
        gHazeWaterProgram.mShaderGroup           = LLGLSLShader::SG_WATER;
        gHazeWaterProgram.mFeatures.hasSrgb                = true;
        gHazeWaterProgram.mFeatures.calculatesAtmospherics = true;
        gHazeWaterProgram.mFeatures.hasAtmospherics        = true;
        gHazeWaterProgram.mFeatures.hasGamma               = true;
        gHazeWaterProgram.mFeatures.isDeferred             = true;
        gHazeWaterProgram.mFeatures.hasShadows             = use_sun_shadow;
        gHazeWaterProgram.mFeatures.hasReflectionProbes    = mShaderLevel[SHADER_DEFERRED] > 2;

        gHazeWaterProgram.clearPermutations();
        gHazeWaterProgram.mShaderFiles.push_back(make_pair("deferred/waterHazeV.glsl", GL_VERTEX_SHADER));
        gHazeWaterProgram.mShaderFiles.push_back(make_pair("deferred/waterHazeF.glsl", GL_FRAGMENT_SHADER));

        add_common_permutations(&gHazeWaterProgram);

        gHazeWaterProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        success = gHazeWaterProgram.createShader();
        llassert(success);
    }


    if (success)
    {
        gDeferredShadowProgram.mName = "Deferred Shadow Shader";
        gDeferredShadowProgram.mShaderFiles.clear();
        gDeferredShadowProgram.mShaderFiles.push_back(make_pair("deferred/shadowV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowProgram.mShaderFiles.push_back(make_pair("deferred/shadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredShadowProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success)
    {
        gDeferredShadowCubeProgram.mName = "Deferred Shadow Cube Shader";
        gDeferredShadowCubeProgram.mFeatures.isDeferred = true;
        gDeferredShadowCubeProgram.mFeatures.hasShadows = true;
        gDeferredShadowCubeProgram.mShaderFiles.clear();
        gDeferredShadowCubeProgram.mShaderFiles.push_back(make_pair("deferred/shadowCubeV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowCubeProgram.mShaderFiles.push_back(make_pair("deferred/shadowF.glsl", GL_FRAGMENT_SHADER));
        // gDeferredShadowCubeProgram.addPermutation("DEPTH_CLAMP", "1");
        gDeferredShadowCubeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredShadowCubeProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredShadowFullbrightAlphaMaskProgram.mName = "Deferred Shadow Fullbright Alpha Mask Shader";
        gDeferredShadowFullbrightAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;

        gDeferredShadowFullbrightAlphaMaskProgram.mShaderFiles.clear();
        gDeferredShadowFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowFullbrightAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskF.glsl", GL_FRAGMENT_SHADER));

        gDeferredShadowFullbrightAlphaMaskProgram.clearPermutations();
        gDeferredShadowFullbrightAlphaMaskProgram.addPermutation("DEPTH_CLAMP", "1");
        gDeferredShadowFullbrightAlphaMaskProgram.addPermutation("IS_FULLBRIGHT", "1");

        add_common_permutations(&gDeferredShadowFullbrightAlphaMaskProgram);

        gDeferredShadowFullbrightAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredShadowFullbrightAlphaMaskProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success)
    {
        gDeferredShadowAlphaMaskProgram.mName = "Deferred Shadow Alpha Mask Shader";
        gDeferredShadowAlphaMaskProgram.mFeatures.mIndexedTextureChannels = LLGLSLShader::sIndexedTextureChannels;

        gDeferredShadowAlphaMaskProgram.mShaderFiles.clear();
        gDeferredShadowAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/shadowAlphaMaskF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredShadowAlphaMaskProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }


    if (success)
    {
        gDeferredShadowGLTFAlphaMaskProgram.mName = "Deferred GLTF Shadow Alpha Mask Shader";
        gDeferredShadowGLTFAlphaMaskProgram.mShaderFiles.clear();
        gDeferredShadowGLTFAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowGLTFAlphaMaskProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowGLTFAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredShadowGLTFAlphaMaskProgram.clearPermutations();

        add_common_permutations(&gDeferredShadowGLTFAlphaMaskProgram);

        success = gDeferredShadowGLTFAlphaMaskProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success && LLGLSLShader::sIndexedGLTFChannels >= 2)
    {
        // Indexed (multi-material) shadow alpha mask, so batched mask faces alpha-test
        // per-slot in the shadow map. Optional: if it fails to load the shadow pass
        // falls back to the scalar program (slightly wrong per-face cutouts, no crash),
        // so this is kept out of the `success` chain.
        gDeferredShadowGLTFAlphaMaskIndexedProgram.mName = "Deferred GLTF Shadow Alpha Mask Indexed Shader";
        gDeferredShadowGLTFAlphaMaskIndexedProgram.mShaderFiles.clear();
        gDeferredShadowGLTFAlphaMaskIndexedProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskIndexedV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowGLTFAlphaMaskIndexedProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowGLTFAlphaMaskIndexedProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredShadowGLTFAlphaMaskIndexedProgram.clearPermutations();
        gDeferredShadowGLTFAlphaMaskIndexedProgram.addPermutation("GLTF_INDEXED_CHANNELS", llformat("%d", LLGLSLShader::sIndexedGLTFChannels));
        add_common_permutations(&gDeferredShadowGLTFAlphaMaskIndexedProgram);

        bool shadow_indexed_ok = gDeferredShadowGLTFAlphaMaskIndexedProgram.createShader(LLGLSLShader::VARIANT_RIGGED);

        if (shadow_indexed_ok)
        { // only base color is sampled for the shadow alpha test
            const S32 n = LLGLSLShader::sIndexedGLTFChannels;
            gDeferredShadowGLTFAlphaMaskIndexedProgram.forEachVariant([n](LLGLSLShader& s) { setup_gltf_indexed_samplers(s, n, false); });
        }
        else
        {
            LL_WARNS("ShaderLoading") << "Indexed PBR shadow alpha mask shader failed to load." << LL_ENDL;
            gDeferredShadowGLTFAlphaMaskIndexedProgram.unload();
        }
    }

    if (success && LLGLSLShader::sIndexedLegacyMaterials)
    {
        // Indexed (multi-material) legacy material shadow alpha mask, so batched
        // masked legacy faces alpha-test per-slot in the shadow map. Required when
        // legacy batching is on: a failure here would leave indexed mask batches
        // casting no shadow (skipped by the scalar pass, no indexed sweep), so on
        // failure we disable legacy batching entirely rather than degrade silently.
        gDeferredShadowMaterialIndexedProgram.mName = "Deferred Material Shadow Indexed Shader";
        gDeferredShadowMaterialIndexedProgram.mShaderFiles.clear();
        gDeferredShadowMaterialIndexedProgram.mShaderFiles.push_back(make_pair("deferred/materialShadowIndexedV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowMaterialIndexedProgram.mShaderFiles.push_back(make_pair("deferred/materialShadowIndexedF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowMaterialIndexedProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredShadowMaterialIndexedProgram.clearPermutations();
        gDeferredShadowMaterialIndexedProgram.addPermutation("GLTF_INDEXED_CHANNELS", llformat("%d", LLGLSLShader::sIndexedGLTFChannels));

        bool mat_shadow_ok = gDeferredShadowMaterialIndexedProgram.createShader(LLGLSLShader::VARIANT_RIGGED);

        if (mat_shadow_ok)
        { // only diffuse is sampled for the shadow alpha test
            const S32 n = LLGLSLShader::sIndexedGLTFChannels;
            gDeferredShadowMaterialIndexedProgram.forEachVariant([n](LLGLSLShader& s) { setup_material_indexed_samplers(s, n, false, false); });
        }
        else
        {
            LL_WARNS("ShaderLoading") << "Indexed legacy material shadow shader failed to load; legacy batching disabled." << LL_ENDL;
            gDeferredShadowMaterialIndexedProgram.unload();
            LLGLSLShader::sIndexedLegacyMaterials = false; // can't shadow indexed batches -- don't form them
        }
    }

    if (success)
    {
        gDeferredShadowGLTFAlphaBlendProgram.mName = "Deferred GLTF Shadow Alpha Blend Shader";
        gDeferredShadowGLTFAlphaBlendProgram.mShaderFiles.clear();
        gDeferredShadowGLTFAlphaBlendProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaMaskV.glsl", GL_VERTEX_SHADER));
        gDeferredShadowGLTFAlphaBlendProgram.mShaderFiles.push_back(make_pair("deferred/pbrShadowAlphaBlendF.glsl", GL_FRAGMENT_SHADER));
        gDeferredShadowGLTFAlphaBlendProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredShadowGLTFAlphaBlendProgram.clearPermutations();

        add_common_permutations(&gDeferredShadowGLTFAlphaBlendProgram);

        success = gDeferredShadowGLTFAlphaBlendProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarShadowProgram.mName = "Deferred Avatar Shadow Shader";
        gDeferredAvatarShadowProgram.mFeatures.hasSkinning = true;

        gDeferredAvatarShadowProgram.mShaderFiles.clear();
        gDeferredAvatarShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredAvatarShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarAlphaShadowProgram.mName = "Deferred Avatar Alpha Shadow Shader";
        gDeferredAvatarAlphaShadowProgram.mFeatures.hasSkinning = true;
        gDeferredAvatarAlphaShadowProgram.mShaderFiles.clear();
        gDeferredAvatarAlphaShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarAlphaShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarAlphaShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredAvatarAlphaShadowProgram.createShader();
        llassert(success);
    }
    if (success)
    {
        gDeferredAvatarAlphaMaskShadowProgram.mName = "Deferred Avatar Alpha Mask Shadow Shader";
        gDeferredAvatarAlphaMaskShadowProgram.mFeatures.hasSkinning  = true;
        gDeferredAvatarAlphaMaskShadowProgram.mShaderFiles.clear();
        gDeferredAvatarAlphaMaskShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaShadowV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarAlphaMaskShadowProgram.mShaderFiles.push_back(make_pair("deferred/avatarAlphaMaskShadowF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarAlphaMaskShadowProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredAvatarAlphaMaskShadowProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredTerrainProgram.mName = "Deferred Terrain Shader";
        gDeferredTerrainProgram.mFeatures.hasSrgb = true;
        gDeferredTerrainProgram.mFeatures.isAlphaLighting = true;
        gDeferredTerrainProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredTerrainProgram.mFeatures.hasAtmospherics = true;
        gDeferredTerrainProgram.mFeatures.hasGamma = true;

        gDeferredTerrainProgram.mShaderFiles.clear();
        gDeferredTerrainProgram.mShaderFiles.push_back(make_pair("deferred/terrainV.glsl", GL_VERTEX_SHADER));
        gDeferredTerrainProgram.mShaderFiles.push_back(make_pair("deferred/terrainF.glsl", GL_FRAGMENT_SHADER));

        add_common_permutations(&gDeferredTerrainProgram);

        gDeferredTerrainProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredTerrainProgram.createShader(mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarProgram.mName = "Deferred Avatar Shader";
        gDeferredAvatarProgram.mFeatures.hasSkinning = true;
        gDeferredAvatarProgram.mShaderFiles.clear();
        gDeferredAvatarProgram.mShaderFiles.push_back(make_pair("deferred/avatarV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarProgram.mShaderFiles.push_back(make_pair("deferred/avatarF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAvatarProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredAvatarProgram.clearPermutations();

        // Skin composite is decoded on the sampler and the gbuffer write re-encodes; see the
        // avatar diffuse binds in llviewerjointmesh and the hoisted FRAMEBUFFER_SRGB.
        gDeferredAvatarProgram.addPermutation("LINEAR_DIFFUSE", "1");

        add_common_permutations(&gDeferredAvatarProgram);
        if (gSavedSettings.getBOOL("RenderAvatarCloth"))
        {
            gDeferredAvatarProgram.addPermutation("AVATAR_CLOTH", "1");
        }

        success = gDeferredAvatarProgram.createShader(mirror_variant());
        llassert(success);
    }

    if (success)
    {
        gDeferredAvatarAlphaProgram.mName = "Deferred Avatar Alpha Shader";
        gDeferredAvatarAlphaProgram.mFeatures.hasSkinning = true;
        gDeferredAvatarAlphaProgram.mFeatures.calculatesLighting = false;
        gDeferredAvatarAlphaProgram.mFeatures.hasLighting = false;
        gDeferredAvatarAlphaProgram.mFeatures.isAlphaLighting = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasSrgb = true;
        gDeferredAvatarAlphaProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasAtmospherics = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasGamma = true;
        gDeferredAvatarAlphaProgram.mFeatures.isDeferred = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasShadows = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasReflectionProbes = true;

        gDeferredAvatarAlphaProgram.mShaderFiles.clear();
        gDeferredAvatarAlphaProgram.mShaderFiles.push_back(make_pair("deferred/alphaV.glsl", GL_VERTEX_SHADER));
        gDeferredAvatarAlphaProgram.mShaderFiles.push_back(make_pair("deferred/alphaF.glsl", GL_FRAGMENT_SHADER));

        gDeferredAvatarAlphaProgram.clearPermutations();
        gDeferredAvatarAlphaProgram.addPermutation("LINEAR_DIFFUSE", "1");
        gDeferredAvatarAlphaProgram.addPermutation("USE_DIFFUSE_TEX", "1");
        gDeferredAvatarAlphaProgram.addPermutation("IS_AVATAR_SKIN", "1");
        if (use_sun_shadow)
        {
            gDeferredAvatarAlphaProgram.addPermutation("HAS_SUN_SHADOW", "1");
        }

        gDeferredAvatarAlphaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredAvatarAlphaProgram);

        success = gDeferredAvatarAlphaProgram.createShader(LLGLSLShader::VARIANT_CLASSIC | mirror_variant());
        llassert(success);

        gDeferredAvatarAlphaProgram.mFeatures.calculatesLighting = true;
        gDeferredAvatarAlphaProgram.mFeatures.hasLighting = true;
    }

    if (success)
    {
        gExposureProgram.mName = "Exposure";
        gExposureProgram.mFeatures.hasSrgb = true;
        gExposureProgram.mFeatures.isDeferred = true;
        gExposureProgram.mShaderFiles.clear();
        gExposureProgram.clearPermutations();
        gExposureProgram.addPermutation("USE_LAST_EXPOSURE", "1");
        gExposureProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gExposureProgram.mShaderFiles.push_back(make_pair("deferred/exposureF.glsl", GL_FRAGMENT_SHADER));
        gExposureProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gExposureProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gExposureProgramNoFade.mName = "Exposure (no fade)";
        gExposureProgramNoFade.mFeatures.hasSrgb = true;
        gExposureProgramNoFade.mFeatures.isDeferred = true;
        gExposureProgramNoFade.mShaderFiles.clear();
        gExposureProgramNoFade.clearPermutations();
        gExposureProgramNoFade.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gExposureProgramNoFade.mShaderFiles.push_back(make_pair("deferred/exposureF.glsl", GL_FRAGMENT_SHADER));
        gExposureProgramNoFade.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gExposureProgramNoFade.createShader();
        llassert(success);
    }

    if (success)
    {
        gLuminanceProgram.mName = "Luminance";
        gLuminanceProgram.mShaderFiles.clear();
        gLuminanceProgram.clearPermutations();
        gLuminanceProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gLuminanceProgram.mShaderFiles.push_back(make_pair("deferred/luminanceF.glsl", GL_FRAGMENT_SHADER));
        gLuminanceProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gLuminanceProgram.createShader();
        llassert(success);
    }

    if (success && gGLManager.mGLVersion > 3.9f)
    {
        std::vector<std::pair<std::string, std::string>> quality_levels = { {"12", "Low"},
                                                                             {"23", "Medium"},
                                                                             {"28", "High"},
                                                                             {"39", "Ultra"} };
        int i = 0;
        bool failed = false;
        for (const auto& quality_pair : quality_levels)
        {
            if (success)
            {
                gFXAAProgram[i].mName = llformat("FXAA Shader (%s)", quality_pair.second.c_str());
                gFXAAProgram[i].mFeatures.isDeferred = true;
                gFXAAProgram[i].mShaderFiles.clear();
                gFXAAProgram[i].mShaderFiles.push_back(make_pair("deferred/postDeferredV.glsl", GL_VERTEX_SHADER));
                gFXAAProgram[i].mShaderFiles.push_back(make_pair("deferred/fxaaF.glsl", GL_FRAGMENT_SHADER));

                gFXAAProgram[i].clearPermutations();
                gFXAAProgram[i].addPermutation("FXAA_QUALITY__PRESET", quality_pair.first);
                if (gGLManager.mGLVersion > 3.9)
                {
                    gFXAAProgram[i].addPermutation("FXAA_GLSL_400", "1");
                }
                else
                {
                    gFXAAProgram[i].addPermutation("FXAA_GLSL_130", "1");
                }

                gFXAAProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gFXAAProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gFXAAProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }
            ++i;
        }

        if (failed)
        {
            for (auto i = 0; i < 4; ++i)
            {
                gFXAAProgram[i].unload();
            }
        }
    }

    if (gGLManager.mGLVersion > 3.15f && success)
    {
        std::vector<std::pair<std::string, std::string>> quality_levels = { {"SMAA_PRESET_LOW", "Low"},
                                                                             {"SMAA_PRESET_MEDIUM", "Medium"},
                                                                             {"SMAA_PRESET_HIGH", "High"},
                                                                          {"SMAA_PRESET_ULTRA", "Ultra"} };
        const bool smaa_predication = gSavedSettings.getBOOL("RenderSMAAPredication");
        const F32 smaa_pred_threshold = gSavedSettings.getF32("RenderSMAAPredicationThreshold");
        const F32 smaa_pred_scale = llclamp(gSavedSettings.getF32("RenderSMAAPredicationScale"), 1.f, 5.f);
        const F32 smaa_pred_strength = llclamp(gSavedSettings.getF32("RenderSMAAPredicationStrength"), 0.f, 1.f);
        int i = 0;
        bool failed = false;
        for (const auto& smaa_pair : quality_levels)
        {
            std::map<std::string, std::string> defines;
            if (gGLManager.mGLVersion >= 4.f)
                defines.emplace("SMAA_GLSL_4", "1");
            else if (gGLManager.mGLVersion >= 3.1f)
                defines.emplace("SMAA_GLSL_3", "1");
            else
                defines.emplace("SMAA_GLSL_2", "1");
            defines.emplace("SMAA_PREDICATION", smaa_predication ? "1" : "0");
            if (smaa_predication)
            {
                defines.emplace("SMAA_PREDICATION_THRESHOLD", llformat("%.6f", smaa_pred_threshold));
                defines.emplace("SMAA_PREDICATION_SCALE", llformat("%.3f", smaa_pred_scale));
                defines.emplace("SMAA_PREDICATION_STRENGTH", llformat("%.3f", smaa_pred_strength));
            }
            defines.emplace("SMAA_REPROJECTION", "0");
            defines.emplace(smaa_pair.first, "1");

            if (success)
            {
                gSMAAEdgeDetectProgram[i].mName = llformat("SMAA Edge Detection (%s)", smaa_pair.second.c_str());
                gSMAAEdgeDetectProgram[i].mFeatures.isDeferred = true;

                gSMAAEdgeDetectProgram[i].clearPermutations();
                gSMAAEdgeDetectProgram[i].addPermutations(defines);

                gSMAAEdgeDetectProgram[i].mShaderFiles.clear();
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAAEdgeDetectF.glsl", GL_FRAGMENT_SHADER));
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAAEdgeDetectV.glsl", GL_VERTEX_SHADER));
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_FRAGMENT_SHADER));
                gSMAAEdgeDetectProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_VERTEX_SHADER));
                gSMAAEdgeDetectProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gSMAAEdgeDetectProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gSMAAEdgeDetectProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }

            if (success)
            {
                gSMAABlendWeightsProgram[i].mName = llformat("SMAA Blending Weights (%s)", smaa_pair.second.c_str());
                gSMAABlendWeightsProgram[i].mFeatures.isDeferred = true;

                gSMAABlendWeightsProgram[i].clearPermutations();
                gSMAABlendWeightsProgram[i].addPermutations(defines);

                gSMAABlendWeightsProgram[i].mShaderFiles.clear();
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAABlendWeightsF.glsl", GL_FRAGMENT_SHADER));
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAABlendWeightsV.glsl", GL_VERTEX_SHADER));
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_FRAGMENT_SHADER));
                gSMAABlendWeightsProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_VERTEX_SHADER));
                gSMAABlendWeightsProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gSMAABlendWeightsProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gSMAABlendWeightsProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }

            if (success)
            {
                gSMAANeighborhoodBlendProgram[i].mName = llformat("SMAA Neighborhood Blending (%s)", smaa_pair.second.c_str());
                gSMAANeighborhoodBlendProgram[i].mFeatures.isDeferred = true;

                gSMAANeighborhoodBlendProgram[i].clearPermutations();
                gSMAANeighborhoodBlendProgram[i].addPermutations(defines);

                gSMAANeighborhoodBlendProgram[i].mShaderFiles.clear();
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAANeighborhoodBlendF.glsl", GL_FRAGMENT_SHADER));
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAANeighborhoodBlendV.glsl", GL_VERTEX_SHADER));
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_FRAGMENT_SHADER));
                gSMAANeighborhoodBlendProgram[i].mShaderFiles.push_back(make_pair("deferred/SMAA.glsl", GL_VERTEX_SHADER));
                gSMAANeighborhoodBlendProgram[i].mShaderLevel = mShaderLevel[SHADER_DEFERRED];
                success = gSMAANeighborhoodBlendProgram[i].createShader();
                // llassert(success);
                if (!success)
                {
                    LL_WARNS() << "Failed to create shader '" << gSMAANeighborhoodBlendProgram[i].mName << "', disabling!" << LL_ENDL;
                    // continue as if this shader never happened
                    failed = true;
                    success = true;
                    break;
                }
            }
            ++i;
        }

        if (failed)
        {
            for (auto i = 0; i < 4; ++i)
            {
                gSMAAEdgeDetectProgram[i].unload();
                gSMAABlendWeightsProgram[i].unload();
                gSMAANeighborhoodBlendProgram[i].unload();
            }
        }
    }

    if (success && gGLManager.mGLVersion > 4.05f)
    {
        gCASProgram.mName = "Contrast Adaptive Sharpening Shader";
        gCASProgram.mFeatures.hasSrgb = true;
        gCASProgram.mShaderFiles.clear();
        gCASProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCASProgram.mShaderFiles.push_back(make_pair("deferred/CASF.glsl", GL_FRAGMENT_SHADER));
        gCASProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCASProgram.createShader();
        // llassert(success);
        if (!success)
        {
            LL_WARNS() << "Failed to create shader '" << gCASProgram.mName << "', disabling!" << LL_ENDL;
            // continue as if this shader never happened
            success = true;
        }
    }

    if (success)
    {
        gDeferredPostProgram.mName = "Deferred Post Shader";
        gDeferredPostProgram.mFeatures.isDeferred = true;
        gDeferredPostProgram.mShaderFiles.clear();
        gDeferredPostProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredPostProgram.clearPermutations();
        gDeferredPostProgram.addPermutation("FRONT_BLUR", "1");

        success = gDeferredPostProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostProgramNoNear.mName = "Deferred Post Shader No Near Blur";
        gDeferredPostProgramNoNear.mFeatures.isDeferred = true;
        gDeferredPostProgramNoNear.mShaderFiles.clear();
        gDeferredPostProgramNoNear.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostProgramNoNear.mShaderFiles.push_back(make_pair("deferred/postDeferredF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostProgramNoNear.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredPostProgramNoNear.clearPermutations();
        gDeferredPostProgramNoNear.addPermutation("FRONT_BLUR", "0");

        success = gDeferredPostProgramNoNear.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredCoFProgram.mName = "Deferred CoF Shader";
        gDeferredCoFProgram.mShaderFiles.clear();
        gDeferredCoFProgram.mFeatures.isDeferred = true;
        gDeferredCoFProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredCoFProgram.mShaderFiles.push_back(make_pair("deferred/cofF.glsl", GL_FRAGMENT_SHADER));
        gDeferredCoFProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredCoFProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredDoFCombineProgram.mName = "Deferred DoFCombine Shader";
        gDeferredDoFCombineProgram.mFeatures.isDeferred = true;
        gDeferredDoFCombineProgram.mShaderFiles.clear();
        gDeferredDoFCombineProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredDoFCombineProgram.mShaderFiles.push_back(make_pair("deferred/dofCombineF.glsl", GL_FRAGMENT_SHADER));
        gDeferredDoFCombineProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredDoFCombineProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredPostNoDoFProgram.mName = "Deferred Post NoDoF Shader";
        gDeferredPostNoDoFProgram.mFeatures.isDeferred = true;
        gDeferredPostNoDoFProgram.mShaderFiles.clear();
        gDeferredPostNoDoFProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredPostNoDoFProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoDoFF.glsl", GL_FRAGMENT_SHADER));
        gDeferredPostNoDoFProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredPostNoDoFProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gEnvironmentMapProgram.mName = "Environment Map Program";
        gEnvironmentMapProgram.mShaderFiles.clear();
        gEnvironmentMapProgram.mFeatures.calculatesAtmospherics = true;
        gEnvironmentMapProgram.mFeatures.hasAtmospherics = true;
        gEnvironmentMapProgram.mFeatures.hasGamma = true;
        gEnvironmentMapProgram.mFeatures.hasSrgb = true;

        gEnvironmentMapProgram.clearPermutations();
        gEnvironmentMapProgram.addPermutation("HAS_HDRI", "1");
        add_common_permutations(&gEnvironmentMapProgram);
        gEnvironmentMapProgram.mShaderFiles.push_back(make_pair("deferred/skyV.glsl", GL_VERTEX_SHADER));
        gEnvironmentMapProgram.mShaderFiles.push_back(make_pair("deferred/skyF.glsl", GL_FRAGMENT_SHADER));
        gEnvironmentMapProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gEnvironmentMapProgram.mShaderGroup = LLGLSLShader::SG_SKY;

        success = gEnvironmentMapProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLSkyProgram.mName = "Deferred Windlight Sky Shader";
        gDeferredWLSkyProgram.mShaderFiles.clear();
        gDeferredWLSkyProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLSkyProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLSkyProgram.mFeatures.hasGamma = true;
        gDeferredWLSkyProgram.mFeatures.hasSrgb = true;

        gDeferredWLSkyProgram.mShaderFiles.push_back(make_pair("deferred/skyV.glsl", GL_VERTEX_SHADER));
        gDeferredWLSkyProgram.mShaderFiles.push_back(make_pair("deferred/skyF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLSkyProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLSkyProgram.mShaderGroup = LLGLSLShader::SG_SKY;

        add_common_permutations(&gDeferredWLSkyProgram);

        success = gDeferredWLSkyProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLCloudProgram.mName = "Deferred Windlight Cloud Program";
        gDeferredWLCloudProgram.mShaderFiles.clear();
        gDeferredWLCloudProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLCloudProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLCloudProgram.mFeatures.hasGamma = true;
        gDeferredWLCloudProgram.mFeatures.hasSrgb = true;

        gDeferredWLCloudProgram.mShaderFiles.push_back(make_pair("deferred/cloudsV.glsl", GL_VERTEX_SHADER));
        gDeferredWLCloudProgram.mShaderFiles.push_back(make_pair("deferred/cloudsF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLCloudProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLCloudProgram.mShaderGroup = LLGLSLShader::SG_SKY;
        gDeferredWLCloudProgram.addConstant( LLGLSLShader::SHADER_CONST_CLOUD_MOON_DEPTH ); // SL-14113

        add_common_permutations(&gDeferredWLCloudProgram);

        success = gDeferredWLCloudProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLSunProgram.mName = "Deferred Windlight Sun Program";
        gDeferredWLSunProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLSunProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLSunProgram.mFeatures.hasGamma = true;
        gDeferredWLSunProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLSunProgram.mFeatures.hasSrgb = true;
        gDeferredWLSunProgram.mShaderFiles.clear();
        gDeferredWLSunProgram.mShaderFiles.push_back(make_pair("deferred/sunDiscV.glsl", GL_VERTEX_SHADER));
        gDeferredWLSunProgram.mShaderFiles.push_back(make_pair("deferred/sunDiscF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLSunProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLSunProgram.mShaderGroup = LLGLSLShader::SG_SKY;

        add_common_permutations(&gDeferredWLSunProgram);

        success = gDeferredWLSunProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredWLMoonProgram.mName = "Deferred Windlight Moon Program";
        gDeferredWLMoonProgram.mFeatures.calculatesAtmospherics = true;
        gDeferredWLMoonProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLMoonProgram.mFeatures.hasGamma = true;
        gDeferredWLMoonProgram.mFeatures.hasAtmospherics = true;
        gDeferredWLMoonProgram.mFeatures.hasSrgb = true;

        gDeferredWLMoonProgram.mShaderFiles.clear();
        gDeferredWLMoonProgram.mShaderFiles.push_back(make_pair("deferred/moonV.glsl", GL_VERTEX_SHADER));
        gDeferredWLMoonProgram.mShaderFiles.push_back(make_pair("deferred/moonF.glsl", GL_FRAGMENT_SHADER));
        gDeferredWLMoonProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredWLMoonProgram.mShaderGroup = LLGLSLShader::SG_SKY;
        gDeferredWLMoonProgram.addConstant( LLGLSLShader::SHADER_CONST_CLOUD_MOON_DEPTH ); // SL-14113

        add_common_permutations(&gDeferredWLMoonProgram);

        success = gDeferredWLMoonProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredStarProgram.mName = "Deferred Star Program";
        gDeferredStarProgram.mShaderFiles.clear();
        gDeferredStarProgram.mShaderFiles.push_back(make_pair("deferred/starsV.glsl", GL_VERTEX_SHADER));
        gDeferredStarProgram.mShaderFiles.push_back(make_pair("deferred/starsF.glsl", GL_FRAGMENT_SHADER));
        gDeferredStarProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredStarProgram.mShaderGroup = LLGLSLShader::SG_SKY;
        gDeferredStarProgram.addConstant( LLGLSLShader::SHADER_CONST_STAR_DEPTH ); // SL-14113

        add_common_permutations(&gDeferredStarProgram);

        success = gDeferredStarProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredMeteorProgram.mName = "Deferred Meteor Program";
        gDeferredMeteorProgram.mShaderFiles.clear();
        gDeferredMeteorProgram.mShaderFiles.push_back(make_pair("deferred/meteorsV.glsl", GL_VERTEX_SHADER));
        gDeferredMeteorProgram.mShaderFiles.push_back(make_pair("deferred/meteorsF.glsl", GL_FRAGMENT_SHADER));
        gDeferredMeteorProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredMeteorProgram.mShaderGroup = LLGLSLShader::SG_SKY;

        add_common_permutations(&gDeferredMeteorProgram);

        success = gDeferredMeteorProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gDeferredAuroraProgram.mName = "Deferred Aurora Program";
        gDeferredAuroraProgram.mShaderFiles.clear();
        gDeferredAuroraProgram.mShaderFiles.push_back(make_pair("deferred/auroraV.glsl", GL_VERTEX_SHADER));
        gDeferredAuroraProgram.mShaderFiles.push_back(make_pair("deferred/auroraF.glsl", GL_FRAGMENT_SHADER));
        gDeferredAuroraProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gDeferredAuroraProgram.mShaderGroup = LLGLSLShader::SG_SKY;

        add_common_permutations(&gDeferredAuroraProgram);

        success = gDeferredAuroraProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gNormalMapGenProgram.mName = "Normal Map Generation Program";
        gNormalMapGenProgram.mShaderFiles.clear();
        gNormalMapGenProgram.mShaderFiles.push_back(make_pair("deferred/normgenV.glsl", GL_VERTEX_SHADER));
        gNormalMapGenProgram.mShaderFiles.push_back(make_pair("deferred/normgenF.glsl", GL_FRAGMENT_SHADER));
        gNormalMapGenProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        gNormalMapGenProgram.mShaderGroup = LLGLSLShader::SG_SKY;
        success = gNormalMapGenProgram.createShader();
    }

    if (success)
    {
        gDeferredGenBrdfLutProgram.mName = "Brdf Gen Shader";
        gDeferredGenBrdfLutProgram.mShaderFiles.clear();
        gDeferredGenBrdfLutProgram.mShaderFiles.push_back(make_pair("deferred/genbrdflutV.glsl", GL_VERTEX_SHADER));
        gDeferredGenBrdfLutProgram.mShaderFiles.push_back(make_pair("deferred/genbrdflutF.glsl", GL_FRAGMENT_SHADER));
        gDeferredGenBrdfLutProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gDeferredGenBrdfLutProgram.createShader();
    }

    if (success) {
        gDeferredBufferVisualProgram.mName = "Deferred Buffer Visualization Shader";
        gDeferredBufferVisualProgram.mShaderFiles.clear();
        gDeferredBufferVisualProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gDeferredBufferVisualProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredVisualizeBuffers.glsl", GL_FRAGMENT_SHADER));
        gDeferredBufferVisualProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];

        add_common_permutations(&gDeferredBufferVisualProgram);

        success = gDeferredBufferVisualProgram.createShader();
    }

    if (success)
    {
        gBlitWithEffectsProgram.mName = "Blit With Post Effects Shader";
        gBlitWithEffectsProgram.mFeatures.isDeferred = true;
        gBlitWithEffectsProgram.mFeatures.hasPostEffects = true;
        gBlitWithEffectsProgram.mShaderFiles.clear();
        gBlitWithEffectsProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gBlitWithEffectsProgram.mShaderFiles.push_back(make_pair("alchemy/blitWithEffectsF.glsl", GL_FRAGMENT_SHADER));
        gBlitWithEffectsProgram.clearPermutations();
        if (gSavedSettings.getBOOL("RenderHDREnabled"))
        {
            gBlitWithEffectsProgram.addPermutation("DITHER", "1");
        }
        gBlitWithEffectsProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gBlitWithEffectsProgram.createShader();
        llassert(success);
    }

    // HDR-only: the bloom pyramid is allocated in the HDR path, so the tonemap
    // shader variants fold the bloom composite inline. Halation rides in the
    // bloom alpha channel when RenderBloomHalation is on — both settings trigger
    // shader rebuilds, so reading them at compile time stays in sync with the
    // bloom pyramid format.
    const bool hdr_enabled         = gSavedSettings.getBOOL("RenderHDREnabled");
    const bool bloom_halation_perm = gSavedSettings.getBOOL("RenderBloomHalation");

    if (success)
    {
        gCGGammaProgram.mName = "CG Gamma Shader";
        gCGGammaProgram.mFeatures.isDeferred = true;
        gCGGammaProgram.mFeatures.hasPostEffects = true;
        gCGGammaProgram.mShaderFiles.clear();
        gCGGammaProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGGammaProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGGammaProgram.clearPermutations();
        gCGGammaProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGGammaProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGGammaProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGGammaProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGGammaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCGGammaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gCGLegacyGammaProgram.mName = "CG Legacy Gamma Shader";
        gCGLegacyGammaProgram.mFeatures.isDeferred = true;
        gCGLegacyGammaProgram.mFeatures.hasPostEffects = true;
        gCGLegacyGammaProgram.mShaderFiles.clear();
        gCGLegacyGammaProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGLegacyGammaProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGLegacyGammaProgram.clearPermutations();
        gCGLegacyGammaProgram.addPermutation("LEGACY_GAMMA", "1");
        gCGLegacyGammaProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGLegacyGammaProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGLegacyGammaProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGLegacyGammaProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGLegacyGammaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCGLegacyGammaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gCGColorgradeGammaProgram.mName = "CG Color Grade Gamma Shader";
        gCGColorgradeGammaProgram.mFeatures.isDeferred = true;
        gCGColorgradeGammaProgram.mFeatures.hasColorGrade = true;
        gCGColorgradeGammaProgram.mFeatures.hasPostEffects = true;
        gCGColorgradeGammaProgram.mShaderFiles.clear();
        gCGColorgradeGammaProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGColorgradeGammaProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGColorgradeGammaProgram.clearPermutations();
        gCGColorgradeGammaProgram.addPermutation("COLOR_GRADE", "1");
        gCGColorgradeGammaProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGColorgradeGammaProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGColorgradeGammaProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGColorgradeGammaProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGColorgradeGammaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success                                = gCGColorgradeGammaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gCGColorgradeLegacyGammaProgram.mName = "CG Color Grade Legacy Gamma Shader";
        gCGColorgradeLegacyGammaProgram.mFeatures.isDeferred = true;
        gCGColorgradeLegacyGammaProgram.mFeatures.hasColorGrade = true;
        gCGColorgradeLegacyGammaProgram.mFeatures.hasPostEffects = true;
        gCGColorgradeLegacyGammaProgram.mShaderFiles.clear();
        gCGColorgradeLegacyGammaProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGColorgradeLegacyGammaProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGColorgradeLegacyGammaProgram.clearPermutations();
        gCGColorgradeLegacyGammaProgram.addPermutation("COLOR_GRADE", "1");
        gCGColorgradeLegacyGammaProgram.addPermutation("LEGACY_GAMMA", "1");
        gCGColorgradeLegacyGammaProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGColorgradeLegacyGammaProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGColorgradeLegacyGammaProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGColorgradeLegacyGammaProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGColorgradeLegacyGammaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCGColorgradeLegacyGammaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gCGTonemapProgram.mName = "CG Tonemap Shader";
        gCGTonemapProgram.mFeatures.isDeferred = true;
        gCGTonemapProgram.mFeatures.hasTonemap = true;
        gCGTonemapProgram.mFeatures.hasPostEffects = true;
        gCGTonemapProgram.mShaderFiles.clear();
        gCGTonemapProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGTonemapProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGTonemapProgram.clearPermutations();
        gCGTonemapProgram.addPermutation("TONEMAP", "1");
        gCGTonemapProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGTonemapProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGTonemapProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGTonemapProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGTonemapProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCGTonemapProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gCGTonemapLegacyGammaProgram.mName = "CG Tonemap Legacy Gamma Shader";
        gCGTonemapLegacyGammaProgram.mFeatures.isDeferred = true;
        gCGTonemapLegacyGammaProgram.mFeatures.hasTonemap = true;
        gCGTonemapLegacyGammaProgram.mFeatures.hasPostEffects = true;
        gCGTonemapLegacyGammaProgram.mShaderFiles.clear();
        gCGTonemapLegacyGammaProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGTonemapLegacyGammaProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGTonemapLegacyGammaProgram.clearPermutations();
        gCGTonemapLegacyGammaProgram.addPermutation("LEGACY_GAMMA", "1");
        gCGTonemapLegacyGammaProgram.addPermutation("TONEMAP", "1");
        gCGTonemapLegacyGammaProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGTonemapLegacyGammaProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGTonemapLegacyGammaProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGTonemapLegacyGammaProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGTonemapLegacyGammaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCGTonemapLegacyGammaProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gCGTonemapColorgradeProgram.mName = "CG Tonemap Color Grade Shader";
        gCGTonemapColorgradeProgram.mFeatures.isDeferred = true;
        gCGTonemapColorgradeProgram.mFeatures.hasTonemap = true;
        gCGTonemapColorgradeProgram.mFeatures.hasColorGrade = true;
        gCGTonemapColorgradeProgram.mFeatures.hasPostEffects = true;
        gCGTonemapColorgradeProgram.mShaderFiles.clear();
        gCGTonemapColorgradeProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGTonemapColorgradeProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGTonemapColorgradeProgram.clearPermutations();
        gCGTonemapColorgradeProgram.addPermutation("COLOR_GRADE", "1");
        gCGTonemapColorgradeProgram.addPermutation("TONEMAP", "1");
        gCGTonemapColorgradeProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGTonemapColorgradeProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGTonemapColorgradeProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGTonemapColorgradeProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGTonemapColorgradeProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCGTonemapColorgradeProgram.createShader();
        llassert(success);
    }

    if (success)
    {
        gCGTonemapColorgradeLegacyGammaProgram.mName = "CG Tonemap Color Grade Legacy Gamma Shader";
        gCGTonemapColorgradeLegacyGammaProgram.mFeatures.isDeferred = true;
        gCGTonemapColorgradeLegacyGammaProgram.mFeatures.hasTonemap = true;
        gCGTonemapColorgradeLegacyGammaProgram.mFeatures.hasColorGrade = true;
        gCGTonemapColorgradeLegacyGammaProgram.mFeatures.hasPostEffects = true;
        gCGTonemapColorgradeLegacyGammaProgram.mShaderFiles.clear();
        gCGTonemapColorgradeLegacyGammaProgram.mShaderFiles.push_back(make_pair("deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER));
        gCGTonemapColorgradeLegacyGammaProgram.mShaderFiles.push_back(make_pair("alchemy/colorCorrectF.glsl", GL_FRAGMENT_SHADER));
        gCGTonemapColorgradeLegacyGammaProgram.clearPermutations();
        gCGTonemapColorgradeLegacyGammaProgram.addPermutation("COLOR_GRADE", "1");
        gCGTonemapColorgradeLegacyGammaProgram.addPermutation("LEGACY_GAMMA", "1");
        gCGTonemapColorgradeLegacyGammaProgram.addPermutation("TONEMAP", "1");
        gCGTonemapColorgradeLegacyGammaProgram.addPermutation("HAS_POST_EFFECTS", "1");
        if (!hdr_enabled)
        {
            gCGTonemapColorgradeLegacyGammaProgram.addPermutation("DITHER", "1");
        }
        else
        {
            gCGTonemapColorgradeLegacyGammaProgram.addPermutation("BLOOM_COMPOSITE", "1");
            if (bloom_halation_perm)
            {
                gCGTonemapColorgradeLegacyGammaProgram.addPermutation("BLOOM_HALATION", "1");
            }
        }
        gCGTonemapColorgradeLegacyGammaProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gCGTonemapColorgradeLegacyGammaProgram.createShader();
        llassert(success);
    }

    // [RLVa:KB] - @setsphere
    if(success)
    {
        gRlvSphereProgram.mName = "RLVa Sphere Post Processing Shader";
        gRlvSphereProgram.mFeatures.isDeferred = true;
        gRlvSphereProgram.mShaderFiles.clear();
        gRlvSphereProgram.mShaderFiles.push_back(make_pair("deferred/rlvV.glsl", GL_VERTEX_SHADER));
        gRlvSphereProgram.mShaderFiles.push_back(make_pair("deferred/rlvF.glsl", GL_FRAGMENT_SHADER));
        gRlvSphereProgram.mShaderLevel = mShaderLevel[SHADER_DEFERRED];
        success = gRlvSphereProgram.createShader();
    }
    // [/RLV:KB]
    return success;
}

bool LLViewerShaderMgr::loadShadersObject()
{
    LL_PROFILE_ZONE_SCOPED;
    bool success = true;

    if (success)
    {
        gObjectBumpProgram.mName = "Bump Shader";
        gObjectBumpProgram.mShaderFiles.clear();
        gObjectBumpProgram.mShaderFiles.push_back(make_pair("objects/bumpV.glsl", GL_VERTEX_SHADER));
        gObjectBumpProgram.mShaderFiles.push_back(make_pair("objects/bumpF.glsl", GL_FRAGMENT_SHADER));
        gObjectBumpProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gObjectBumpProgram.createShader(LLGLSLShader::VARIANT_RIGGED | mirror_variant());
        if (success)
        { //lldrawpoolbump assumes "texture0" has channel 0 and "texture1" has channel 1
            gObjectBumpProgram.forEachVariant([](LLGLSLShader& s)
            {
                s.bind();
                s.uniform1i(sTexture0, 0);
                s.uniform1i(sTexture1, 1);
                s.unbind();
            });
        }
    }

    if (success)
    {
        gObjectAlphaMaskNoColorProgram.mName = "No color alpha mask Shader";
        gObjectAlphaMaskNoColorProgram.mFeatures.calculatesLighting = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.calculatesAtmospherics = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasGamma = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasAtmospherics = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasLighting = true;
        gObjectAlphaMaskNoColorProgram.mFeatures.hasAlphaMask = true;
        gObjectAlphaMaskNoColorProgram.mShaderFiles.clear();
        gObjectAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("objects/simpleNoColorV.glsl", GL_VERTEX_SHADER));
        gObjectAlphaMaskNoColorProgram.mShaderFiles.push_back(make_pair("objects/simpleF.glsl", GL_FRAGMENT_SHADER));
        gObjectAlphaMaskNoColorProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gObjectAlphaMaskNoColorProgram.createShader();
    }

    if (success)
    {
        gImpostorProgram.mName = "Impostor Shader";
        gImpostorProgram.mFeatures.hasSrgb = true;
        gImpostorProgram.mShaderFiles.clear();
        gImpostorProgram.mShaderFiles.push_back(make_pair("objects/impostorV.glsl", GL_VERTEX_SHADER));
        gImpostorProgram.mShaderFiles.push_back(make_pair("objects/impostorF.glsl", GL_FRAGMENT_SHADER));
        gImpostorProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gImpostorProgram.createShader();
    }

    if (success)
    {
        gObjectPreviewProgram.mName = "Object Preview Shader";
        gObjectPreviewProgram.mShaderFiles.clear();
        gObjectPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewV.glsl", GL_VERTEX_SHADER));
        gObjectPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewF.glsl", GL_FRAGMENT_SHADER));
        gObjectPreviewProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gObjectPreviewProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
        gObjectPreviewProgram.forEachVariant([](LLGLSLShader& s) { s.mFeatures.hasLighting = true; });
    }

    if (success)
    {
        gPhysicsPreviewProgram.mName = "Preview Physics Shader";
        gPhysicsPreviewProgram.mFeatures.calculatesLighting = false;
        gPhysicsPreviewProgram.mFeatures.calculatesAtmospherics = false;
        gPhysicsPreviewProgram.mFeatures.hasGamma = false;
        gPhysicsPreviewProgram.mFeatures.hasAtmospherics = false;
        gPhysicsPreviewProgram.mFeatures.hasLighting = false;
        gPhysicsPreviewProgram.mShaderFiles.clear();
        gPhysicsPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewPhysicsV.glsl", GL_VERTEX_SHADER));
        gPhysicsPreviewProgram.mShaderFiles.push_back(make_pair("objects/previewPhysicsF.glsl", GL_FRAGMENT_SHADER));
        gPhysicsPreviewProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gPhysicsPreviewProgram.createShader();
        gPhysicsPreviewProgram.mFeatures.hasLighting = false;
    }

    if (!success)
    {
        mShaderLevel[SHADER_OBJECT] = 0;
        return false;
    }

    return true;
}

bool LLViewerShaderMgr::loadShadersAvatar()
{
    LL_PROFILE_ZONE_SCOPED;
#if 1 // DEPRECATED -- forward rendering is deprecated
    bool success = true;

    if (mShaderLevel[SHADER_AVATAR] == 0)
    {
        gAvatarProgram.unload();
        return true;
    }

    if (success)
    {
        gAvatarProgram.mName = "Avatar Shader";
        gAvatarProgram.mFeatures.hasSkinning = true;
        gAvatarProgram.mFeatures.calculatesAtmospherics = true;
        gAvatarProgram.mFeatures.calculatesLighting = true;
        gAvatarProgram.mFeatures.hasGamma = true;
        gAvatarProgram.mFeatures.hasAtmospherics = true;
        gAvatarProgram.mFeatures.hasLighting = true;
        gAvatarProgram.mFeatures.hasAlphaMask = true;
        gAvatarProgram.mShaderFiles.clear();
        gAvatarProgram.mShaderFiles.push_back(make_pair("avatar/avatarV.glsl", GL_VERTEX_SHADER));
        gAvatarProgram.mShaderFiles.push_back(make_pair("avatar/avatarF.glsl", GL_FRAGMENT_SHADER));
        gAvatarProgram.mShaderLevel = mShaderLevel[SHADER_AVATAR];
        success = gAvatarProgram.createShader();

        /// Keep track of avatar levels
        if (gAvatarProgram.mShaderLevel != mShaderLevel[SHADER_AVATAR])
        {
            mMaxAvatarShaderLevel = mShaderLevel[SHADER_AVATAR] = gAvatarProgram.mShaderLevel;
        }
    }

    if( !success )
    {
        mShaderLevel[SHADER_AVATAR] = 0;
        mMaxAvatarShaderLevel = 0;
        return false;
    }
#endif
    return true;
}

bool LLViewerShaderMgr::loadShadersInterface()
{
    LL_PROFILE_ZONE_SCOPED;
    bool success = true;

    if (success)
    {
        gHighlightProgram.mName = "Highlight Shader";
        gHighlightProgram.mShaderFiles.clear();
        gHighlightProgram.mShaderFiles.push_back(make_pair("interface/highlightV.glsl", GL_VERTEX_SHADER));
        gHighlightProgram.mShaderFiles.push_back(make_pair("interface/highlightF.glsl", GL_FRAGMENT_SHADER));
        gHighlightProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gHighlightProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
    }

    if (success)
    {
        gHighlightNormalProgram.mName = "Highlight Normals Shader";
        gHighlightNormalProgram.mShaderFiles.clear();
        gHighlightNormalProgram.mShaderFiles.push_back(make_pair("interface/highlightNormV.glsl", GL_VERTEX_SHADER));
        gHighlightNormalProgram.mShaderFiles.push_back(make_pair("interface/highlightF.glsl", GL_FRAGMENT_SHADER));
        gHighlightNormalProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gHighlightNormalProgram.createShader();
    }

    if (success)
    {
        gHighlightSpecularProgram.mName = "Highlight Spec Shader";
        gHighlightSpecularProgram.mShaderFiles.clear();
        gHighlightSpecularProgram.mShaderFiles.push_back(make_pair("interface/highlightSpecV.glsl", GL_VERTEX_SHADER));
        gHighlightSpecularProgram.mShaderFiles.push_back(make_pair("interface/highlightF.glsl", GL_FRAGMENT_SHADER));
        gHighlightSpecularProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gHighlightSpecularProgram.createShader();
    }

    if (success)
    {
        gUIProgram.mName = "UI Shader";
        gUIProgram.mShaderFiles.clear();
        gUIProgram.mShaderFiles.push_back(make_pair("interface/uiV.glsl", GL_VERTEX_SHADER));
        gUIProgram.mShaderFiles.push_back(make_pair("interface/uiF.glsl", GL_FRAGMENT_SHADER));
        gUIProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gUIProgram.createShader();
        if (success)
        {
            // Initialize the shadow-path uniform to passthrough so non-text UI
            // and NO_SHADOW text take the early-return branch in uiF.glsl. GLSL
            // already zero-initializes uniforms, but pushing an explicit default
            // documents the contract and protects against driver quirks.
            // textShadowMode is the shader's only shadow uniform — atlas texel size
            // and channel layout derive from the bound texture in uiF.glsl.
            gUIProgram.bind();
            gUIProgram.uniform1i(LLShaderMgr::TEXT_SHADOW_MODE, 0);
            gUIProgram.unbind();
        }
    }

    if (success)
    {
        gPathfindingProgram.mName = "Pathfinding Shader";
        gPathfindingProgram.mShaderFiles.clear();
        gPathfindingProgram.mShaderFiles.push_back(make_pair("interface/pathfindingV.glsl", GL_VERTEX_SHADER));
        gPathfindingProgram.mShaderFiles.push_back(make_pair("interface/pathfindingF.glsl", GL_FRAGMENT_SHADER));
        gPathfindingProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gPathfindingProgram.createShader();
    }

    if (success)
    {
        gPathfindingNoNormalsProgram.mName = "PathfindingNoNormals Shader";
        gPathfindingNoNormalsProgram.mShaderFiles.clear();
        gPathfindingNoNormalsProgram.mShaderFiles.push_back(make_pair("interface/pathfindingNoNormalV.glsl", GL_VERTEX_SHADER));
        gPathfindingNoNormalsProgram.mShaderFiles.push_back(make_pair("interface/pathfindingF.glsl", GL_FRAGMENT_SHADER));
        gPathfindingNoNormalsProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gPathfindingNoNormalsProgram.createShader();
    }

    if (success)
    {
        gGlowCombineProgram.mName = "Glow Combine Shader";
        gGlowCombineProgram.mShaderFiles.clear();
        gGlowCombineProgram.mShaderFiles.push_back(make_pair("interface/glowcombineV.glsl", GL_VERTEX_SHADER));
        gGlowCombineProgram.mShaderFiles.push_back(make_pair("interface/glowcombineF.glsl", GL_FRAGMENT_SHADER));
        gGlowCombineProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gGlowCombineProgram.createShader();
    }

    if (success)
    {
        gGlowCombineFXAAProgram.mName = "Glow CombineFXAA Shader";
        gGlowCombineFXAAProgram.mShaderFiles.clear();
        gGlowCombineFXAAProgram.mShaderFiles.push_back(make_pair("interface/glowcombineFXAAV.glsl", GL_VERTEX_SHADER));
        gGlowCombineFXAAProgram.mShaderFiles.push_back(make_pair("interface/glowcombineFXAAF.glsl", GL_FRAGMENT_SHADER));
        gGlowCombineFXAAProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gGlowCombineFXAAProgram.createShader();
    }

#ifdef LL_WINDOWS
    if (success)
    {
        gTwoTextureCompareProgram.mName = "Two Texture Compare Shader";
        gTwoTextureCompareProgram.mShaderFiles.clear();
        gTwoTextureCompareProgram.mShaderFiles.push_back(make_pair("interface/twotexturecompareV.glsl", GL_VERTEX_SHADER));
        gTwoTextureCompareProgram.mShaderFiles.push_back(make_pair("interface/twotexturecompareF.glsl", GL_FRAGMENT_SHADER));
        gTwoTextureCompareProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gTwoTextureCompareProgram.createShader();
        if (success)
        {
            gTwoTextureCompareProgram.bind();
            gTwoTextureCompareProgram.uniform1i(sTex0, 0);
            gTwoTextureCompareProgram.uniform1i(sTex1, 1);
            gTwoTextureCompareProgram.uniform1i(sDitherTex, 2);
        }
    }

    if (success)
    {
        gOneTextureFilterProgram.mName = "One Texture Filter Shader";
        gOneTextureFilterProgram.mShaderFiles.clear();
        gOneTextureFilterProgram.mShaderFiles.push_back(make_pair("interface/onetexturefilterV.glsl", GL_VERTEX_SHADER));
        gOneTextureFilterProgram.mShaderFiles.push_back(make_pair("interface/onetexturefilterF.glsl", GL_FRAGMENT_SHADER));
        gOneTextureFilterProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gOneTextureFilterProgram.createShader();
        if (success)
        {
            gOneTextureFilterProgram.bind();
            gOneTextureFilterProgram.uniform1i(sTex0, 0);
        }
    }
#endif

    if (success)
    {
        gSolidColorProgram.mName = "Solid Color Shader";
        gSolidColorProgram.mShaderFiles.clear();
        gSolidColorProgram.mShaderFiles.push_back(make_pair("interface/solidcolorV.glsl", GL_VERTEX_SHADER));
        gSolidColorProgram.mShaderFiles.push_back(make_pair("interface/solidcolorF.glsl", GL_FRAGMENT_SHADER));
        gSolidColorProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gSolidColorProgram.createShader();
        if (success)
        {
            gSolidColorProgram.bind();
            gSolidColorProgram.uniform1i(sTex0, 0);
            gSolidColorProgram.unbind();
        }
    }

    if (success)
    {
        gOcclusionProgram.mName = "Occlusion Shader";
        gOcclusionProgram.mShaderFiles.clear();
        gOcclusionProgram.mShaderFiles.push_back(make_pair("interface/occlusionV.glsl", GL_VERTEX_SHADER));
        gOcclusionProgram.mShaderFiles.push_back(make_pair("interface/occlusionF.glsl", GL_FRAGMENT_SHADER));
        gOcclusionProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        add_common_permutations(&gOcclusionProgram);
        success = gOcclusionProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
    }

    if (success)
    {
        gOcclusionCubeProgram.mName = "Occlusion Cube Shader";
        gOcclusionCubeProgram.mShaderFiles.clear();
        gOcclusionCubeProgram.mShaderFiles.push_back(make_pair("interface/occlusionCubeV.glsl", GL_VERTEX_SHADER));
        gOcclusionCubeProgram.mShaderFiles.push_back(make_pair("interface/occlusionF.glsl", GL_FRAGMENT_SHADER));
        gOcclusionCubeProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        add_common_permutations(&gOcclusionCubeProgram);
        success = gOcclusionCubeProgram.createShader();
    }

    if (success)
    {
        gDebugProgram.mName = "Debug Shader";
        gDebugProgram.mShaderFiles.clear();
        gDebugProgram.mShaderFiles.push_back(make_pair("interface/debugV.glsl", GL_VERTEX_SHADER));
        gDebugProgram.mShaderFiles.push_back(make_pair("interface/debugF.glsl", GL_FRAGMENT_SHADER));
        gDebugProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gDebugProgram.createShader(LLGLSLShader::VARIANT_RIGGED);
    }

    if (success)
    {
        for (S32 variant = 0; variant < NORMAL_DEBUG_SHADER_COUNT; ++variant)
        {
            LLGLSLShader& shader = gNormalDebugProgram[variant];
            // Distinct per variant: both entries are live at once, and each also has a
            // "Skinned <name>" pair, so a shared name collides twice over.
            shader.mName = llformat("Normal Debug Shader%s",
                                    variant == NORMAL_DEBUG_SHADER_WITH_TANGENTS ? " (Tangents)" : "");
            shader.mShaderFiles.clear();
            shader.mShaderFiles.push_back(make_pair("interface/normaldebugV.glsl", GL_VERTEX_SHADER));
            // *NOTE: Geometry shaders have a reputation for being slow.
            // Consider using compute shaders instead, which have a reputation
            // for being fast. This geometry shader in particular seems to run
            // fine on my machine, but I won't vouch for this in
            // performance-critical areas.  -Cosmic,2023-09-28
            shader.mShaderFiles.push_back(make_pair("interface/normaldebugG.glsl", GL_GEOMETRY_SHADER));
            shader.mShaderFiles.push_back(make_pair("interface/normaldebugF.glsl", GL_FRAGMENT_SHADER));
            shader.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
            if (variant == NORMAL_DEBUG_SHADER_WITH_TANGENTS)
            {
                shader.addPermutation("HAS_ATTRIBUTE_TANGENT", "1");
            }
            success = shader.createShader(LLGLSLShader::VARIANT_RIGGED);
        }
    }

    if (success)
    {
        gClipProgram.mName = "Clip Shader";
        gClipProgram.mShaderFiles.clear();
        gClipProgram.mShaderFiles.push_back(make_pair("interface/clipV.glsl", GL_VERTEX_SHADER));
        gClipProgram.mShaderFiles.push_back(make_pair("interface/clipF.glsl", GL_FRAGMENT_SHADER));
        gClipProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gClipProgram.createShader();
    }

    if (success)
    {
        gBenchmarkProgram.mName = "Benchmark Shader";
        gBenchmarkProgram.mShaderFiles.clear();
        gBenchmarkProgram.mShaderFiles.push_back(make_pair("interface/benchmarkV.glsl", GL_VERTEX_SHADER));
        gBenchmarkProgram.mShaderFiles.push_back(make_pair("interface/benchmarkF.glsl", GL_FRAGMENT_SHADER));
        gBenchmarkProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gBenchmarkProgram.createShader();
    }

    if (success)
    {
        gReflectionProbeDisplayProgram.mName = "Reflection Probe Display Shader";
        gReflectionProbeDisplayProgram.mFeatures.hasReflectionProbes = true;
        gReflectionProbeDisplayProgram.mFeatures.hasSrgb = true;
        gReflectionProbeDisplayProgram.mFeatures.calculatesAtmospherics = true;
        gReflectionProbeDisplayProgram.mFeatures.hasAtmospherics = true;
        gReflectionProbeDisplayProgram.mFeatures.hasGamma = true;
        gReflectionProbeDisplayProgram.mFeatures.isDeferred = true;
        gReflectionProbeDisplayProgram.mShaderFiles.clear();
        gReflectionProbeDisplayProgram.mShaderFiles.push_back(make_pair("interface/reflectionprobeV.glsl", GL_VERTEX_SHADER));
        gReflectionProbeDisplayProgram.mShaderFiles.push_back(make_pair("interface/reflectionprobeF.glsl", GL_FRAGMENT_SHADER));
        gReflectionProbeDisplayProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gReflectionProbeDisplayProgram.createShader();
    }

    if (success)
    {
        gCopyProgram.mName = "Copy Shader";
        gCopyProgram.mShaderFiles.clear();
        gCopyProgram.mShaderFiles.push_back(make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
        gCopyProgram.mShaderFiles.push_back(make_pair("interface/copyF.glsl", GL_FRAGMENT_SHADER));
        gCopyProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gCopyProgram.createShader();
    }

    if (success)
    {
        gDrawColorProgram.mName = "Draw Color Shader";
        gDrawColorProgram.mShaderFiles.clear();
        gDrawColorProgram.mShaderFiles.push_back(make_pair("objects/simpleNoAtmosV.glsl", GL_VERTEX_SHADER));
        gDrawColorProgram.mShaderFiles.push_back(make_pair("objects/simpleColorF.glsl", GL_FRAGMENT_SHADER));
        gDrawColorProgram.clearPermutations();
        gDrawColorProgram.mShaderLevel = mShaderLevel[SHADER_OBJECT];
        success = gDrawColorProgram.createShader();
    }

    if (gSavedSettings.getBOOL("LocalTerrainPaintEnabled"))
    {
        if (success)
        {
            LLGLSLShader* shader = &gPBRTerrainBakeProgram;
            U32 bit_depth = gSavedSettings.getU32("TerrainPaintBitDepth");
            // LLTerrainPaintMap currently uses an RGB8 texture internally
            bit_depth = llclamp(bit_depth, 1, 8);
            shader->mName = llformat("Terrain Bake Shader RGB%o", bit_depth);
            shader->mFeatures.isPBRTerrain = true;

            shader->mShaderFiles.clear();
            shader->mShaderFiles.push_back(make_pair("interface/pbrTerrainBakeV.glsl", GL_VERTEX_SHADER));
            shader->mShaderFiles.push_back(make_pair("interface/pbrTerrainBakeF.glsl", GL_FRAGMENT_SHADER));
            shader->mShaderLevel = mShaderLevel[SHADER_INTERFACE];
            const U32 value_range = (1 << bit_depth) - 1;
            shader->addPermutation("TERRAIN_PAINT_PRECISION", llformat("%d", value_range));
            success = success && shader->createShader();
            //llassert(success);
            if (!success)
            {
                LL_WARNS() << "Failed to create shader '" << shader->mName << "', disabling!" << LL_ENDL;
                gSavedSettings.setBOOL("RenderCanUseTerrainBakeShaders", false);
                // continue as if this shader never happened
                success = true;
            }
        }
    }

    if (success)
    {
        gAlphaMaskProgram.mName = "Alpha Mask Shader";
        gAlphaMaskProgram.mShaderFiles.clear();
        gAlphaMaskProgram.mShaderFiles.push_back(make_pair("interface/alphamaskV.glsl", GL_VERTEX_SHADER));
        gAlphaMaskProgram.mShaderFiles.push_back(make_pair("interface/alphamaskF.glsl", GL_FRAGMENT_SHADER));
        gAlphaMaskProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gAlphaMaskProgram.createShader();
    }

    if (success)
    {
        gReflectionMipProgram.mName = "Reflection Mip Shader";
        gReflectionMipProgram.mFeatures.isDeferred = true;
        gReflectionMipProgram.mFeatures.hasGamma = true;
        gReflectionMipProgram.mFeatures.hasAtmospherics = true;
        gReflectionMipProgram.mFeatures.calculatesAtmospherics = true;
        gReflectionMipProgram.mShaderFiles.clear();
        gReflectionMipProgram.mShaderFiles.push_back(make_pair("interface/splattexturerectV.glsl", GL_VERTEX_SHADER));
        gReflectionMipProgram.mShaderFiles.push_back(make_pair("interface/reflectionmipF.glsl", GL_FRAGMENT_SHADER));
        gReflectionMipProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gReflectionMipProgram.createShader();
    }

    if (success)
    {
        // Program names must be unique; finalizeShaderList() asserts over every live program.
        gGaussianProgram.mName = "Gaussian Blur Shader";
        gGaussianProgram.mFeatures.isDeferred = true;
        gGaussianProgram.mFeatures.hasGamma = true;
        gGaussianProgram.mFeatures.hasAtmospherics = true;
        gGaussianProgram.mFeatures.calculatesAtmospherics = true;
        gGaussianProgram.mShaderFiles.clear();
        gGaussianProgram.mShaderFiles.push_back(make_pair("interface/splattexturerectV.glsl", GL_VERTEX_SHADER));
        gGaussianProgram.mShaderFiles.push_back(make_pair("interface/gaussianF.glsl", GL_FRAGMENT_SHADER));
        gGaussianProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gGaussianProgram.createShader();
    }

    if (success && gGLManager.mHasCubeMapArray)
    {
        gRadianceGenProgram.mName = "Radiance Gen Shader";
        gRadianceGenProgram.mShaderFiles.clear();
        gRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenV.glsl", GL_VERTEX_SHADER));
        gRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenF.glsl", GL_FRAGMENT_SHADER));
        gRadianceGenProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        gRadianceGenProgram.addPermutation("PROBE_FILTER_SAMPLES", "32");
        success = gRadianceGenProgram.createShader();
    }

    if (success && gGLManager.mHasCubeMapArray)
    {
        gHeroRadianceGenProgram.mName = "Hero Radiance Gen Shader";
        gHeroRadianceGenProgram.mShaderFiles.clear();
        gHeroRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenV.glsl", GL_VERTEX_SHADER));
        gHeroRadianceGenProgram.mShaderFiles.push_back(make_pair("interface/radianceGenF.glsl", GL_FRAGMENT_SHADER));
        gHeroRadianceGenProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        gHeroRadianceGenProgram.addPermutation("HERO_PROBES", "1");
        gHeroRadianceGenProgram.addPermutation("PROBE_FILTER_SAMPLES", "4");
        success                              = gHeroRadianceGenProgram.createShader();
    }

    if (success && gGLManager.mHasCubeMapArray)
    {
        gIrradianceGenProgram.mName = "Irradiance Gen Shader";
        gIrradianceGenProgram.mShaderFiles.clear();
        gIrradianceGenProgram.mShaderFiles.push_back(make_pair("interface/irradianceGenV.glsl", GL_VERTEX_SHADER));
        gIrradianceGenProgram.mShaderFiles.push_back(make_pair("interface/irradianceGenF.glsl", GL_FRAGMENT_SHADER));
        gIrradianceGenProgram.mShaderLevel = mShaderLevel[SHADER_INTERFACE];
        success = gIrradianceGenProgram.createShader();
    }

    if( !success )
    {
        mShaderLevel[SHADER_INTERFACE] = 0;
        return false;
    }

    return true;
}


std::string LLViewerShaderMgr::getShaderDirPrefix(void)
{
    return gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "shaders", "class");
}

void LLViewerShaderMgr::updateShaderUniforms(LLGLSLShader * shader)
{
    LLEnvironment::instance().updateShaderUniforms(shader);
}

