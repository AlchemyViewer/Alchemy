/**
 * @file pipeline.cpp
 * @brief Rendering pipeline.
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

#include "pipeline.h"
#include <algorithm>
#include <sstream>

#include <fmt/format.h>

// library includes
#include "llimagebmp.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llimagetga.h"
#include "llimagewebp.h"
#include "llaudioengine.h" // For debugging.
#include "llavatarappearancedefines.h"
#include "llerror.h"
#include "llfocusmgr.h"
#include "llviewercontrol.h"
#include "llfontgl.h"
#include "llfonttextcache.h"
#include "llnamevalue.h"
#include "llpointer.h"
#include "llprimitive.h"
#include "llvolume.h"
#include "material_codes.h"
#include "v3color.h"
#include "llui.h"
#include "llglheaders.h"
#include "aldepthbounds.h"
#include "allightmapblur.h"
#include "alrenderisolation.h"
#include "alsamplerstate.h"
#include "aluniformbuffer.h"
#include "alwhitebalancesolver.h"
#include "alcurvemodel.h"
#include "alfarplane.h"
#include "alfarplaneoverlay.h"
#include "alselectionoutline.h"
#include "alwaterstencil.h"
#include "llrender.h"
#include "llstartup.h"
#include "llwindow.h"   // swapBuffers()

// newview includes
#include "llagent.h"
#include "llagentcamera.h"
#include "llappviewer.h"
#include "lltexturecache.h"
#include "lltexturefetch.h"
#include "llimageworker.h"
#include "lldrawable.h"
#include "lldrawpoolalpha.h"
#include "lldrawpoolavatar.h"
#include "lldrawpoolbump.h"
#include "lldrawpooltree.h"
#include "lldrawpoolwater.h"
#include "llface.h"
#include "llfeaturemanager.h"
#include "llfloatertelehub.h"
#include "llgltfmaterialpreviewmgr.h"
#include "llfloaterreg.h"
#include "llhudmanager.h"
#include "llhudnametag.h"
#include "llhudtext.h"
#include "lllightconstants.h"
#include "llmeshrepository.h"
#include "llpipelinelistener.h"
#include "llresmgr.h"
#include "llselectmgr.h"
#include "llsky.h"
#include "lltracker.h"
#include "lltool.h"
#include "lltoolmgr.h"
#include "llviewercamera.h"
#include "llviewermediafocus.h"
#include "llviewertexturelist.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h" // for audio debugging.
#include "llviewerwindow.h" // For getSpinAxis
#include "llvoavatarself.h"
#include "llvocache.h"
#include "llvosky.h"
#include "llvowlsky.h"
#include "llvotree.h"
#include "llvovolume.h"
#include "llvosurfacepatch.h"
#include "llvotree.h"
#include "llvopartgroup.h"
#include "llworld.h"
#include "llcubemap.h"
#include "llviewershadermgr.h"
#include "llviewerstats.h"
#include "llviewerjoystick.h"
#include "llviewerdisplay.h"
#include "llspatialpartition.h"
#include "llmutelist.h"
#include "lltoolpie.h"
#include "llnotifications.h"
#include "llpathinglib.h"
#include "llfloaterpathfindingconsole.h"
#include "llfloaterpathfindingcharacters.h"
#include "llfloatertools.h"
#include "llpanelface.h"
#include "llpathfindingpathtool.h"
#include "llscenemonitor.h"
#include "llprogressview.h"
#include "llcleanup.h"
#include "lutcube.h"
// [RLVa:KB] - Checked: RLVa-2.0.0
#include "llvisualeffect.h"
#include "rlvactions.h"
#include "rlvlocks.h"
// [/RLVa:KB]

#include "llenvironment.h"
#include "llsettingsvo.h"

#include "SMAAAreaTex.h"
#include "SMAASearchTex.h"
#include "llerror.h"

#if LL_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#elif LL_GNUC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wrestrict"
#endif
#ifndef LL_WINDOWS
#define A_GCC 1
#endif
#define A_CPU 1
#include "app_settings/shaders/class1/deferred/CASF.glsl" // This is also C++
#if LL_CLANG
#pragma clang diagnostic pop
#elif LL_GNUC
#pragma GCC diagnostic pop
#endif

extern bool gSnapshot;
bool gShiftFrame = false;

//cached settings
bool LLPipeline::WindLightUseAtmosShaders;
bool LLPipeline::RenderDeferred;
F32 LLPipeline::RenderDeferredSunWash;
S32 LLPipeline::RenderShadowSplits;
bool LLPipeline::RenderDelayCreation;
bool LLPipeline::RenderAnimateRes;
bool LLPipeline::FreezeTime;
S32 LLPipeline::DebugBeaconLineWidth;
F32 LLPipeline::RenderHighlightBrightness;
LLColor4 LLPipeline::RenderHighlightColor;
F32 LLPipeline::RenderHighlightThickness;
bool LLPipeline::RenderSpotLightsInNondeferred;
LLColor4 LLPipeline::PreviewAmbientColor;
LLColor4 LLPipeline::PreviewDiffuse0;
LLColor4 LLPipeline::PreviewSpecular0;
LLColor4 LLPipeline::PreviewDiffuse1;
LLColor4 LLPipeline::PreviewSpecular1;
LLColor4 LLPipeline::PreviewDiffuse2;
LLColor4 LLPipeline::PreviewSpecular2;
LLVector3 LLPipeline::PreviewDirection0;
LLVector3 LLPipeline::PreviewDirection1;
LLVector3 LLPipeline::PreviewDirection2;
F32 LLPipeline::RenderGlowMaxExtractAlpha;
F32 LLPipeline::RenderGlowWarmthAmount;
LLVector3 LLPipeline::RenderGlowLumWeights;
LLVector3 LLPipeline::RenderGlowWarmthWeights;
S32 LLPipeline::RenderGlowIterations;
F32 LLPipeline::RenderGlowWidth;
F32 LLPipeline::RenderGlowStrength;
bool LLPipeline::RenderDepthOfField;
F32 LLPipeline::CameraFocusTransitionTime;
F32 LLPipeline::CameraFNumber;
F32 LLPipeline::CameraFocalLength;
F32 LLPipeline::CameraFieldOfView;
F32 LLPipeline::RenderShadowNoise;
F32 LLPipeline::RenderShadowBlurSize;
F32 LLPipeline::RenderSSAOScale;
U32 LLPipeline::RenderSSAOMaxScale;
F32 LLPipeline::RenderSSAOFactor;
LLVector3 LLPipeline::RenderSSAOEffect;
F32 LLPipeline::RenderShadowOffsetError;
F32 LLPipeline::RenderShadowBiasError;
F32 LLPipeline::RenderShadowOffset;
F32 LLPipeline::RenderShadowBias;
F32 LLPipeline::RenderSpotShadowOffset;
F32 LLPipeline::RenderSpotShadowBias;
LLDrawable* LLPipeline::RenderSpotLight = nullptr;
F32 LLPipeline::RenderEdgeDepthCutoff;
F32 LLPipeline::RenderEdgeNormCutoff;
LLVector3 LLPipeline::RenderShadowGaussian;
F32 LLPipeline::RenderShadowBlurDistFactor;
bool LLPipeline::RenderDeferredAtmospheric;
F32 LLPipeline::RenderHighlightFadeTime;
F32 LLPipeline::RenderFarClip;
LLVector3 LLPipeline::RenderShadowSplitExponent;
F32 LLPipeline::RenderShadowErrorCutoff;
F32 LLPipeline::RenderShadowFOVCutoff;
bool LLPipeline::CameraOffset;
F32 LLPipeline::CameraMaxCoF;
F32 LLPipeline::CameraDoFResScale;
F32 LLPipeline::RenderAutoHideSurfaceAreaLimit;
S32 LLPipeline::RenderScreenSpaceReflectionIterations;
F32 LLPipeline::RenderScreenSpaceReflectionRayStep;
F32 LLPipeline::RenderScreenSpaceReflectionDistanceBias;
F32 LLPipeline::RenderScreenSpaceReflectionDepthRejectBias;
F32 LLPipeline::RenderScreenSpaceReflectionAdaptiveStepMultiplier;
S32 LLPipeline::RenderScreenSpaceReflectionGlossySamples;
S32 LLPipeline::RenderBufferVisualization;
S32 LLPipeline::RenderHeroProbeUpdateRate;
S32 LLPipeline::RenderHeroProbeConservativeUpdateMultiplier;

const F32 BACKLIGHT_DAY_MAGNITUDE_OBJECT = 0.1f;
const F32 BACKLIGHT_NIGHT_MAGNITUDE_OBJECT = 0.08f;
const F32 ALPHA_BLEND_CUTOFF = 0.598f;
const F32 DEFERRED_LIGHT_FALLOFF = 0.5f;
const U32 DEFERRED_VB_MASK = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_TEXCOORD1;

// The exposure meter. The grid's size is fixed so its cost does not grow with the window;
// luminanceF.glsl box-filters each texel's patch of the scene whatever the patch's size. The
// histogram's 64 bins span log2 luminance -16 to +12 (BINS in exposureHistogramV.glsl and
// exposureF.glsl), and the exposure puts the metered mean at middle grey.
constexpr U32 EXPOSURE_METER_WIDTH = ALRenderResourcePlan::EXPOSURE_METER_WIDTH;
constexpr U32 EXPOSURE_METER_HEIGHT = ALRenderResourcePlan::EXPOSURE_METER_HEIGHT;
constexpr U32 EXPOSURE_HISTOGRAM_BINS = ALRenderResourcePlan::EXPOSURE_HISTOGRAM_BINS;
constexpr F32 EXPOSURE_HISTOGRAM_MIN_EV = -16.f;
constexpr F32 EXPOSURE_HISTOGRAM_MAX_EV = 12.f;
constexpr F32 EXPOSURE_KEY = 0.18f;

extern S32 gBoxFrame;
extern bool gDisplaySwapBuffers;
extern bool gDebugGL;
extern bool gCubeSnapshot;
extern bool gSnapshotNoPost;

bool    gAvatarBacklight = false;

bool    gDebugPipeline = false;
LLPipeline gPipeline;
const LLMatrix4a* gGLLastMatrix = NULL;

//----------------------------------------

void drawBox(const LLVector4a& c, const LLVector4a& r);
void drawBoxOutline(const LLVector3& pos, const LLVector3& size);
U32 nhpo2(U32 v);
LLVertexBuffer* ll_create_cube_vb(U32 type_mask);

void display_update_camera();
//----------------------------------------

S32     LLPipeline::sCompiles = 0;

bool    LLPipeline::sPickAvatar = true;
bool    LLPipeline::sDynamicLOD = true;
bool    LLPipeline::sShowHUDAttachments = true;
bool    LLPipeline::sRenderMOAPBeacons = false;
bool    LLPipeline::sRenderPhysicalBeacons = true;
bool    LLPipeline::sRenderScriptedBeacons = false;
bool    LLPipeline::sRenderScriptedTouchBeacons = true;
bool    LLPipeline::sRenderParticleBeacons = false;
bool    LLPipeline::sRenderSoundBeacons = false;
bool    LLPipeline::sRenderBeacons = false;
bool    LLPipeline::sRenderHighlight = true;
LLRender::eTexIndex LLPipeline::sRenderHighlightTextureChannel = LLRender::DIFFUSE_MAP;
bool    LLPipeline::sForceOldBakedUpload = false;
S32     LLPipeline::sUseOcclusion = 0;
bool    LLPipeline::sAutoMaskAlphaDeferred = true;
bool    LLPipeline::sAutoMaskAlphaNonDeferred = false;
bool    LLPipeline::sRenderTransparentWater = true;
bool    LLPipeline::sBakeSunlight = false;
bool    LLPipeline::sNoAlpha = false;
bool    LLPipeline::sUseFarClip = true;
bool    LLPipeline::sShadowRender = false;
bool    LLPipeline::sRenderGlow = false;
bool    LLPipeline::sImpostorRender = false;
bool    LLPipeline::sCutoutRender = false;
bool    LLPipeline::sUnderWaterRender = false;
bool    LLPipeline::sTextureBindTest = false;
bool    LLPipeline::sRenderAttachedLights = true;
bool    LLPipeline::sRenderAttachedParticles = true;
bool    LLPipeline::sRenderDeferred = false;
bool    LLPipeline::sReflectionProbesEnabled = false;
S32     LLPipeline::sVisibleLightCount = 0;
bool    LLPipeline::sRenderingHUDs;
F32     LLPipeline::sDistortionWaterClipPlaneMargin = 1.0125f;
// [SL:KB] - Patch: Render-TextureToggle (Catznip-4.0)
bool    LLPipeline::sRenderTextures = true;
// [/SL:KB]
// [RLVa:KB] - @setsphere
bool    LLPipeline::sUseDepthTexture = false;
// [/RLVa:KB]
bool    LLPipeline::sScopeCapture = false;
bool    LLPipeline::sGradeBypass = false;
U32     LLPipeline::sGradeBypassMask = 0;

// static
bool LLPipeline::isMainWorldPass()
{
    return !sShadowRender && !sImpostorRender && !sRenderingHUDs && !gCubeSnapshot
        && LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD;
}

// static
bool LLPipeline::suppressesLensAndPrintEffects()
{
    return gSnapshotNoPost || sCutoutRender;
}

// EventHost API LLPipeline listener.
static LLPipelineListener sPipelineListener;

static LLCullResult* sCull = NULL;

void validate_framebuffer_object();

// The colour attachments after the first that the main G-buffer has, in its formats: an avatar's impostor holds the
// G-buffer's channels (generateImpostor).
bool addDeferredAttachments(LLRenderTarget& target)
{
    const std::optional<ALRenderTargetDesc>& gbuffer = gPipeline.resourcePlan().desc(ALRenderResourcePlan::Id::MainGBuffer);
    if (!gbuffer)
    {
        return false;
    }
    for (U32 attachment = 1; attachment < gbuffer->colorCount(); ++attachment)
    {
        if (!target.addColorAttachment(gbuffer->color[attachment]))
        {
            return false;
        }
    }
    return true;
}

LLPipeline::LLPipeline() :
    mBackfaceCull(false),
    mMatrixOpCount(0),
    mTextureMatrixOps(0),
    mTextureMatrixOpsShadow(0),
    mTextureMatrixOpsProbe(0),
    mTextureMatrixOpsIdentity(0),
    mNumVisibleNodes(0),
    mNumVisibleFaces(0),
    mPoissonOffset(0),

    mInitialized(false),
    mShadersLoaded(false),
    mRenderDebugFeatureMask(0),
    mRenderDebugMask(0),
    mOldRenderDebugMask(0),
    mGroupQ1Locked(false),
    mResetVertexBuffers(false),
    mLastRebuildPool(NULL),
    mLightMask(0),
    mLightMovingMask(0)
{
    mNoiseMap = 0;
    mTrueNoiseMap = 0;

    for(U32 i = 0; i < 8; i++)
    {
        mHWLightColors[i] = LLColor4::black;
    }

#if !LL_RELEASE_FOR_DOWNLOAD
    // Register the Deferred block's expected std140 layout for debug validation at shader
    // load, offsetof()-derived from the very struct packDeferredUBO writes -- llrender carries
    // no hand-copied offset table that can drift from it. shadow_matrix/ssao_effect_mat must
    // introspect COLUMN-major (see DeferredUBOData). Registered here because gPipeline is a
    // global: this runs during static init, before any shader load.
#define LL_DEFERRED_LAYOUT(name_lit, m, is_matrix) \
    { -1, name_lit, (U32)offsetof(DeferredUBOData, m), is_matrix }
    LLGLSLShader::registerEngineBlockLayout("Deferred",
    {
        LL_DEFERRED_LAYOUT("shadow_matrix[0]",   shadow_matrix,      true),
        LL_DEFERRED_LAYOUT("ssao_effect_mat",    ssao_effect_mat,    true),
        LL_DEFERRED_LAYOUT("shadow_clip",        shadow_clip,        false),
        LL_DEFERRED_LAYOUT("shadow_res",         shadow_res,         false),
        LL_DEFERRED_LAYOUT("proj_shadow_res",    proj_shadow_res,    false),
        LL_DEFERRED_LAYOUT("shadow_bias",        shadow_bias,        false),
        LL_DEFERRED_LAYOUT("shadow_offset",      shadow_offset,      false),
        LL_DEFERRED_LAYOUT("spot_shadow_bias",   spot_shadow_bias,   false),
        LL_DEFERRED_LAYOUT("spot_shadow_offset", spot_shadow_offset, false),
        LL_DEFERRED_LAYOUT("ssao_radius",        ssao_radius,        false),
        LL_DEFERRED_LAYOUT("ssao_max_radius",    ssao_max_radius,    false),
        LL_DEFERRED_LAYOUT("ssao_factor",        ssao_factor,        false),
        LL_DEFERRED_LAYOUT("ssao_factor_inv",    ssao_factor_inv,    false),
        LL_DEFERRED_LAYOUT("shadow_pcss_scale",  shadow_pcss_scale,  false),
        LL_DEFERRED_LAYOUT("specular_exponent",  specular_exponent,  false),
    });
#undef LL_DEFERRED_LAYOUT
#endif // !LL_RELEASE_FOR_DOWNLOAD
}

void LLPipeline::connectRefreshCachedSettingsSafe(const std::string name)
{
    LLPointer<LLControlVariable> cntrl_ptr = gSavedSettings.getControl(name);
    if ( cntrl_ptr.isNull() )
    {
        LL_WARNS() << "Global setting name not found:" << name << LL_ENDL;
    }
    else
    {
        cntrl_ptr->getCommitSignal()->connect(boost::bind(&LLPipeline::refreshCachedSettings));
    }
}

void LLPipeline::init()
{
    refreshCachedSettings();

    mRT = &mMainRT;

    // Every target the plan names takes its name for debuggers: main.gbuffer, probe.shadow.sun0.
    for (size_t i = 0; i < ALRenderResourcePlan::ID_COUNT; ++i)
    {
        if (LLRenderTarget* target = resourceTarget((ALRenderResourcePlan::Id)i))
        {
            target->setLabel(ALRenderResourcePlan::name((ALRenderResourcePlan::Id)i));
        }
    }
    mLensDirtMap.setLabel("lens.dirt");
    mScopeSample.setLabel("scopes.sample");
    mReferenceStill.setLabel("reference.still");

    gOctreeMaxCapacity = gSavedSettings.getU32("OctreeMaxNodeCapacity");
    gOctreeMinSize = gSavedSettings.getF32("OctreeMinimumNodeSize");
    sDynamicLOD = gSavedSettings.getBOOL("RenderDynamicLOD");
    sRenderAttachedLights = gSavedSettings.getBOOL("RenderAttachedLights");
    sRenderAttachedParticles = gSavedSettings.getBOOL("RenderAttachedParticles");

    mReflectionMapManager.refreshSettings();

    mInitialized = true;

    stop_glerror();

    //create render pass pools
    getPool(LLDrawPool::POOL_WATEREXCLUSION);
    getPool(LLDrawPool::POOL_WATER);
    getPool(LLDrawPool::POOL_ALPHA_PRE_WATER);
    getPool(LLDrawPool::POOL_ALPHA_POST_WATER);
    getPool(LLDrawPool::POOL_SIMPLE);
    getPool(LLDrawPool::POOL_ALPHA_MASK);
    getPool(LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK);
    getPool(LLDrawPool::POOL_GRASS);
    getPool(LLDrawPool::POOL_FULLBRIGHT);
    getPool(LLDrawPool::POOL_BUMP);
    getPool(LLDrawPool::POOL_MATERIALS);
    getPool(LLDrawPool::POOL_GLOW);
    getPool(LLDrawPool::POOL_GLTF_PBR);
    getPool(LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK);

    resetFrameStats();

    if (gSavedSettings.getBOOL("DisableAllRenderFeatures"))
    {
        clearAllRenderDebugFeatures();
    }
    else
    {
        setAllRenderDebugFeatures(); // By default, all debugging features on
    }
    clearAllRenderDebugDisplays(); // All debug displays off

    if (gSavedSettings.getBOOL("DisableAllRenderTypes"))
    {
        clearAllRenderTypes();
    }
    else if (gNonInteractive)
    {
        clearAllRenderTypes();
    }
    else
    {
        setAllRenderTypes(); // By default, all rendering types start enabled
    }

    // make sure RenderPerformanceTest persists (hackity hack hack)
    // disables non-object rendering (UI, sky, water, etc)
    if (gSavedSettings.getBOOL("RenderPerformanceTest"))
    {
        gSavedSettings.setBOOL("RenderPerformanceTest", false);
        gSavedSettings.setBOOL("RenderPerformanceTest", true);
    }

    mOldRenderDebugMask = mRenderDebugMask;

    mBackfaceCull = true;

    // Every program and resource, from the inputs as they are now.
    loadRenderInputs();

    for (U32 i = 0; i < 2; ++i)
    {
        mSpotLightFade[i] = 1.f;
    }

    if (mCubeVB.isNull())
    {
        mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
    }

    mDeferredVB = new LLVertexBuffer(DEFERRED_VB_MASK);
    mDeferredVB->allocateBuffer(8, 0);

    {
        mScreenTriangleVB = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
        mScreenTriangleVB->allocateBuffer(3, 0);
        LLStrider<LLVector3> vert;
        mScreenTriangleVB->getVertexStrider(vert);

        vert[0].set(-1, 1, 0);
        vert[1].set(-1, -3, 0);
        vert[2].set(3, 1, 0);

        mScreenTriangleVB->unmapBuffer();
    }

    {
        // xy: the meter texel, z: 0 for its non-sky part, 1 for its sky
        mExposureHistogramVB = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
        mExposureHistogramVB->allocateBuffer(EXPOSURE_METER_WIDTH * EXPOSURE_METER_HEIGHT * 2, 0);
        LLStrider<LLVector3> vert;
        mExposureHistogramVB->getVertexStrider(vert);
        U32 i = 0;
        for (U32 y = 0; y < EXPOSURE_METER_HEIGHT; ++y)
        {
            for (U32 x = 0; x < EXPOSURE_METER_WIDTH; ++x)
            {
                vert[i++].set((F32)x, (F32)y, 0.f);
                vert[i++].set((F32)x, (F32)y, 1.f);
            }
        }
        mExposureHistogramVB->unmapBuffer();
    }

    //
    // Update all settings to trigger a cached settings refresh
    //
    connectRefreshCachedSettingsSafe("RenderAutoMaskAlphaDeferred");
    connectRefreshCachedSettingsSafe("RenderAutoMaskAlphaNonDeferred");
    connectRefreshCachedSettingsSafe("RenderUseFarClip");
    connectRefreshCachedSettingsSafe("RenderAvatarMaxNonImpostors");
    connectRefreshCachedSettingsSafe("UseOcclusion");
    // DEPRECATED -- connectRefreshCachedSettingsSafe("WindLightUseAtmosShaders");
    // DEPRECATED -- connectRefreshCachedSettingsSafe("RenderDeferred");
    connectRefreshCachedSettingsSafe("RenderDeferredSunWash");
    connectRefreshCachedSettingsSafe("RenderShadowSplits");
    connectRefreshCachedSettingsSafe("RenderDelayCreation");
    connectRefreshCachedSettingsSafe("RenderAnimateRes");
    connectRefreshCachedSettingsSafe("FreezeTime");
    connectRefreshCachedSettingsSafe("DebugBeaconLineWidth");
    connectRefreshCachedSettingsSafe("RenderHighlightBrightness");
    connectRefreshCachedSettingsSafe("RenderHighlightColor");
    connectRefreshCachedSettingsSafe("RenderHighlightThickness");
    connectRefreshCachedSettingsSafe("RenderSpotLightsInNondeferred");
    connectRefreshCachedSettingsSafe("PreviewAmbientColor");
    connectRefreshCachedSettingsSafe("PreviewDiffuse0");
    connectRefreshCachedSettingsSafe("PreviewSpecular0");
    connectRefreshCachedSettingsSafe("PreviewDiffuse1");
    connectRefreshCachedSettingsSafe("PreviewSpecular1");
    connectRefreshCachedSettingsSafe("PreviewDiffuse2");
    connectRefreshCachedSettingsSafe("PreviewSpecular2");
    connectRefreshCachedSettingsSafe("PreviewDirection0");
    connectRefreshCachedSettingsSafe("PreviewDirection1");
    connectRefreshCachedSettingsSafe("PreviewDirection2");
    connectRefreshCachedSettingsSafe("RenderGlowMaxExtractAlpha");
    connectRefreshCachedSettingsSafe("RenderGlowWarmthAmount");
    connectRefreshCachedSettingsSafe("RenderGlowLumWeights");
    connectRefreshCachedSettingsSafe("RenderGlowWarmthWeights");
    connectRefreshCachedSettingsSafe("RenderGlowIterations");
    connectRefreshCachedSettingsSafe("RenderGlowWidth");
    connectRefreshCachedSettingsSafe("RenderGlowStrength");
    connectRefreshCachedSettingsSafe("RenderDepthOfField");
    connectRefreshCachedSettingsSafe("CameraFocusTransitionTime");
    connectRefreshCachedSettingsSafe("CameraFNumber");
    connectRefreshCachedSettingsSafe("CameraFocalLength");
    connectRefreshCachedSettingsSafe("CameraFieldOfView");
    connectRefreshCachedSettingsSafe("RenderShadowNoise");
    connectRefreshCachedSettingsSafe("RenderShadowBlurSize");
    connectRefreshCachedSettingsSafe("RenderSSAOScale");
    connectRefreshCachedSettingsSafe("RenderSSAOMaxScale");
    connectRefreshCachedSettingsSafe("RenderSSAOFactor");
    connectRefreshCachedSettingsSafe("RenderSSAOEffect");
    connectRefreshCachedSettingsSafe("RenderShadowOffsetError");
    connectRefreshCachedSettingsSafe("RenderShadowBiasError");
    connectRefreshCachedSettingsSafe("RenderShadowOffset");
    connectRefreshCachedSettingsSafe("RenderShadowBias");
    connectRefreshCachedSettingsSafe("RenderSpotShadowOffset");
    connectRefreshCachedSettingsSafe("RenderSpotShadowBias");
    connectRefreshCachedSettingsSafe("RenderEdgeDepthCutoff");
    connectRefreshCachedSettingsSafe("RenderEdgeNormCutoff");
    connectRefreshCachedSettingsSafe("RenderShadowGaussian");
    connectRefreshCachedSettingsSafe("RenderShadowBlurDistFactor");
    connectRefreshCachedSettingsSafe("RenderDeferredAtmospheric");
    connectRefreshCachedSettingsSafe("RenderHighlightFadeTime");
    connectRefreshCachedSettingsSafe("RenderFarClip");
    connectRefreshCachedSettingsSafe("RenderShadowSplitExponent");
    connectRefreshCachedSettingsSafe("RenderShadowErrorCutoff");
    connectRefreshCachedSettingsSafe("RenderShadowFOVCutoff");
    connectRefreshCachedSettingsSafe("CameraOffset");
    connectRefreshCachedSettingsSafe("CameraMaxCoF");
    connectRefreshCachedSettingsSafe("CameraDoFResScale");
    connectRefreshCachedSettingsSafe("RenderAutoHideSurfaceAreaLimit");
    connectRefreshCachedSettingsSafe("RenderScreenSpaceReflectionIterations");
    connectRefreshCachedSettingsSafe("RenderScreenSpaceReflectionRayStep");
    connectRefreshCachedSettingsSafe("RenderScreenSpaceReflectionDistanceBias");
    connectRefreshCachedSettingsSafe("RenderScreenSpaceReflectionDepthRejectBias");
    connectRefreshCachedSettingsSafe("RenderScreenSpaceReflectionAdaptiveStepMultiplier");
    connectRefreshCachedSettingsSafe("RenderScreenSpaceReflectionGlossySamples");
    connectRefreshCachedSettingsSafe("RenderBufferVisualization");
    connectRefreshCachedSettingsSafe("RenderHeroProbeUpdateRate");
    connectRefreshCachedSettingsSafe("RenderHeroProbeConservativeUpdateMultiplier");

    LLPointer<LLControlVariable> cntrl_ptr = gSavedSettings.getControl("CollectFontVertexBuffers");
    if (cntrl_ptr.notNull())
    {
        cntrl_ptr->getCommitSignal()->connect([](LLControlVariable* control, const LLSD& value, const LLSD& previous)
        {
            bool enable_buffers = control->getValue().asBoolean();
            LLFontTextCache::enableBufferCollection(enable_buffers);
        });
    }

    cntrl_ptr = gSavedSettings.getControl("CollectUIImageVertexBuffers");
    if (cntrl_ptr.notNull())
    {
        cntrl_ptr->getCommitSignal()->connect([](LLControlVariable* control, const LLSD& value, const LLSD& previous)
        {
            bool enable_buffers = control->getValue().asBoolean();
            LLUIImage::enableDisplayListsCollection(enable_buffers);
        });
    }

    gSavedSettings.getControl("RenderColorGrade")->getCommitSignal()->connect(boost::bind(&LLPipeline::setupGradingLUT, this));
    gSavedSettings.getControl("RenderColorGradeLUT")->getCommitSignal()->connect(boost::bind(&LLPipeline::setupGradingLUT, this));

    // Tone curve: a commit only marks the lookup row dirty. The bake itself
    // runs from colorCorrect -- see bakeToneCurveLut for why not here.
    for (S32 c = 0; c < ALToneCurveSet::CH_COUNT; ++c)
    {
        cntrl_ptr = gSavedSettings.getControl(ALToneCurveSet::settingName(static_cast<ALToneCurveSet::EChannel>(c)));
        if (cntrl_ptr.notNull())
        {
            cntrl_ptr->getCommitSignal()->connect(
                [this](LLControlVariable*, const LLSD&, const LLSD&) { mToneCurveLutDirty = true; });
        }
    }
}

LLPipeline::~LLPipeline()
{
}

void LLPipeline::cleanup()
{
    assertInitialized();

    mGroupQ1.clear() ;

    for(pool_set_t::iterator iter = mPools.begin();
        iter != mPools.end(); )
    {
        pool_set_t::iterator curiter = iter++;
        LLDrawPool* poolp = *curiter;
        if (poolp->isFacePool())
        {
            LLFacePool* face_pool = (LLFacePool*) poolp;
            if (face_pool->mReferences.empty())
            {
                mPools.erase(curiter);
                removeFromQuickLookup( poolp );
                delete poolp;
            }
        }
        else
        {
            mPools.erase(curiter);
            removeFromQuickLookup( poolp );
            delete poolp;
        }
    }

    if (!mTerrainPools.empty())
    {
        LL_WARNS() << "Terrain Pools not cleaned up" << LL_ENDL;
    }
    if (!mTreePools.empty())
    {
        LL_WARNS() << "Tree Pools not cleaned up" << LL_ENDL;
    }

    delete mAlphaPoolPreWater;
    mAlphaPoolPreWater = nullptr;
    delete mAlphaPoolPostWater;
    mAlphaPoolPostWater = nullptr;
    delete mSkyPool;
    mSkyPool = NULL;
    delete mTerrainPool;
    mTerrainPool = NULL;
    delete mWaterPool;
    mWaterPool = NULL;
    delete mSimplePool;
    mSimplePool = NULL;
    delete mFullbrightPool;
    mFullbrightPool = NULL;
    delete mGlowPool;
    mGlowPool = NULL;
    delete mBumpPool;
    mBumpPool = NULL;
    // don't delete wl sky pool it was handled above in the for loop
    //delete mWLSkyPool;
    mWLSkyPool = NULL;
    delete mWaterExclusionPool;
    mWaterExclusionPool = nullptr;

    releaseAllGLResources();

    mFaceSelectImagep = NULL;

    mMovedList.clear();
    mMovedBridge.clear();
    mShiftList.clear();

    mInitialized = false;

    mDeferredVB = NULL;
    mScreenTriangleVB = nullptr;
    mExposureHistogramVB = nullptr;

    mCubeVB = NULL;

    mReflectionMapManager.cleanup();
    mHeroProbeManager.cleanup();
}

//============================================================================

void LLPipeline::destroyGL()
{
    stop_glerror();
    unloadShaders();
    mHighlightFaces.clear();

    resetDrawOrders();

    releaseAllGLResources();
}

void LLPipeline::markRenderInputsDirty(ALRenderInputRequests::Change change)
{
    mRenderInputRequests.mark(change);
}

void LLPipeline::applyRenderInputs()
{
    // A frame starts here, with every transient target the last one acquired back in the pool. Debug GL poisons each
    // as it is released, and names each for whoever acquires it. Nothing is trimmed until the user is logged in: the
    // login's frames draw no world, and the world's first frame takes what the reconcile readied for it rather than
    // building it mid-pass.
    mTransientTargets.setValidation(gDebugGL);
    mTransientTargets.holdTrimming(LLStartUp::getStartupState() < STATE_STARTED);
    mTransientTargets.beginFrame();
    if (mTransientTargets.namesRecorded())
    {
        LL_INFOS("RenderResources") << "Transient targets over the frame after the reconcile, "
                                    << (mTransientTargets.lastFrame().mBytes >> 20) << " MiB:\n"
                                    << mTransientTargets.describe() << LL_ENDL;
    }

    // Nothing is applied to resources that are not there. Before the pipeline's first load, and while a context is
    // lost, the load that comes next (loadRenderInputs) applies whatever was marked until then.
    if (!mInitialized || !mGLResourcesLive)
    {
        return;
    }
    if (const std::optional<ALRenderInputRequests::Application> taken = mRenderInputRequests.take())
    {
        applyRenderInputs(*taken);
    }
}

void LLPipeline::loadRenderInputs()
{
    ALRenderInputRequests::Application taken = mRenderInputRequests.take().value_or(ALRenderInputRequests::Application{});
    taken.mRebuildPrograms = true;
    applyRenderInputs(taken);
}

void LLPipeline::applyRenderInputs(const ALRenderInputRequests::Application& taken)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    using Programs = ALRenderApplication::Programs;

    latchRenderInputs();

    LLViewerShaderMgr* shaders = LLViewerShaderMgr::instance();
    const ALRenderApplication steps = ALRenderApplication::make(
        taken.mRebuildPrograms, mShadersLoaded ? std::optional<ALShaderInputs>(shaders->inputs()) : std::nullopt,
        mLatchedShaderInputs, mGLResourcesLive ? std::optional<ALRenderResourceInputs>(mRequestedInputs) : std::nullopt,
        mLatchedInputs);
    if (steps.idle())
    {
        return;
    }

    mApplyingChanges = taken.mChanges;
    const bool reload = steps.mPrograms != Programs::Kept;
    // Programs first: the resources' plan takes the normal layout and the depth convention they compile for.
    if (reload && !shaders->setShaders(mLatchedShaderInputs, steps))
    {
        mApplyingChanges = 0;
        return;
    }
    if (steps.mReconcile)
    {
        rebuildGLBuffers(mLatchedInputs, reload             ? RebuildCause::ShaderReload
                                         : taken.mSkipFrame ? RebuildCause::Resize
                                                            : RebuildCause::Setting);
    }
    else if (reload)
    {
        programsReloaded();
    }
    mApplyingChanges = 0;
}

bool LLPipeline::beginSizeOverride(U32 width, U32 height)
{
    llassert(!mRenderInputRequests.overriding());
    mRenderInputRequests.overrideBegins();

    ALRenderResourceInputs requested = mLatchedInputs;
    requested.mWidth  = width;
    requested.mHeight = height;
    // The size the last snapshot built, which its end left in place: a 360 capture's next face.
    if (mGLResourcesLive && requested == mRequestedInputs)
    {
        return mResourcesFullSize;
    }
    return rebuildGLBuffers(requested, RebuildCause::Snapshot);
}

void LLPipeline::endSizeOverride()
{
    mRenderInputRequests.overrideEnds();
}

void LLPipeline::latchRenderInputs()
{
    mLatchedShaderInputs = shaderInputsFromSettings();
    mLatchedInputs       = resourceInputsFromSettings(mLatchedShaderInputs);

    sReflectionProbesEnabled = mLatchedInputs.mProbes;
    sRenderTransparentWater  = mLatchedInputs.mTransparentWater;
}

const ALShaderInputs& LLPipeline::shaderInputs() const
{
    return LLViewerShaderMgr::instance()->inputs();
}

// static
void LLPipeline::scaleToRenderResolution(U32& resX, U32& resY)
{
    ALRenderResourceInputs::scaleToRenderResolution(resX, resY, gPipeline.mLatchedInputs.mResolutionDivisor,
                                                    gPipeline.mLatchedInputs.mResolutionMultiplier);
}

LLRenderTarget* LLPipeline::resourceTarget(ALRenderResourcePlan::Id id)
{
    using Plan = ALRenderResourcePlan;
    using Id   = Plan::Id;
    using Slot = Plan::Slot;

    // The pool's, acquired by the passes that use them.
    if (Plan::isTransient(id))
    {
        return nullptr;
    }

    if (const std::optional<Plan::Pack> pack = Plan::packOf(id))
    {
        RenderTargetPack& rt = *pack == Plan::Pack::Main ? mMainRT : (*pack == Plan::Pack::Probe ? mAuxillaryRT : mHeroProbeRT);
        const Slot slot = Plan::slotOf(id);
        switch (slot)
        {
            case Slot::GBuffer:  return &rt.deferredScreen;
            case Slot::Screen:   return &rt.screen;
            default:
                break;
        }
        return &rt.shadow[(U32)slot - (U32)Slot::Sun0];
    }

    switch (id)
    {
        case Id::SpotShadow0:       return &mSpotShadow[0];
        case Id::SpotShadow1:       return &mSpotShadow[1];
        case Id::SSRScene:          return &mSceneMap;
        case Id::DownRes:           return &mDownResMap;
        case Id::BrdfLut:           return &mPbrBrdfLut;
        case Id::ExposureMeter:     return &mLuminanceMap;
        case Id::ExposureHistogram: return &mExposureHistogram;
        case Id::Exposure:          return &mExposureMap;
        case Id::ExposureLast:      return &mLastExposure;
        case Id::LensFlareA:        return &mLensFlareState[0];
        case Id::LensFlareB:        return &mLensFlareState[1];
        default:
            return nullptr;
    }
}

U32* LLPipeline::resourceTexture(ALRenderResourcePlan::Id id)
{
    using Id = ALRenderResourcePlan::Id;
    switch (id)
    {
        case Id::Noise:      return &mNoiseMap;
        case Id::TrueNoise:  return &mTrueNoiseMap;
        case Id::SMAAArea:   return &mSMAAAreaMap;
        case Id::SMAASearch: return &mSMAASearchMap;
        case Id::SMAASample: return &mSMAASampleMap;
        case Id::ToneCurve:  return &mToneCurveLut;
        default:
            return nullptr;
    }
}

void LLPipeline::ensureResources(const ALRenderResourcePlan& plan, const ALRenderResourcePlan::IdSet& ids,
                                 ResourceChanges& changes)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    using Plan = ALRenderResourcePlan;
    using Id   = Plan::Id;

    // Owners first: a target sharing depth attaches the depth its owner holds once the owner is what the plan says.
    for (const bool sharers : { false, true })
    {
        for (size_t i = 0; i < Plan::ID_COUNT; ++i)
        {
            const Id              id       = (Id)i;
            const Plan::Resource& resource = plan[id];
            if (!ids.test(i) || resource.mDepthFrom.has_value() != sharers || Plan::isTransient(id))
            {
                continue;
            }

            if (U32* texture = resourceTexture(id))
            {
                if (resource.mDesc && !*texture)
                {
                    // A texture whose contents have no source (the SMAA sample image, without a SamplePath) is left
                    // unmade without failing anything.
                    createResourceTexture(id);
                    if (*texture)
                    {
                        changes.mBuilt.set(i);
                    }
                }
                else if (!resource.mDesc && *texture)
                {
                    LLImageGL::deleteTextures(1, texture);
                    *texture = 0;
                    changes.mFreed.set(i);
                }
                else if (*texture)
                {
                    changes.mKept.set(i);
                }
                continue;
            }

            LLRenderTarget& target = *resourceTarget(id);
            if (!resource.mDesc)
            {
                if (target.desc() != ALRenderTargetDesc{})
                {
                    target.release();
                    changes.mFreed.set(i);
                }
                continue;
            }

            const LLRenderTarget::EnsureResult result = resource.mDepthFrom
                ? target.ensure(*resource.mDesc, *resourceTarget(*resource.mDepthFrom))
                : target.ensure(*resource.mDesc);
            switch (result)
            {
                case LLRenderTarget::EnsureResult::Kept:
                    changes.mKept.set(i);
                    break;
                case LLRenderTarget::EnsureResult::Built:
                    changes.mBuilt.set(i);
                    break;
                case LLRenderTarget::EnsureResult::Failed:
                    changes.mFailed.set(i);
                    break;
            }
        }
    }

    gGL.getTextureSlot(0)->unbind();
}

void LLPipeline::prewarmTransients(const ALRenderResourcePlan& plan, const ALRenderResourcePlan::IdSet& ids,
                                   ResourceChanges& changes)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    using Plan = ALRenderResourcePlan;
    using Id   = Plan::Id;

    // Nearly every target the pool holds is the main view's size, and a pooled target over a depth the reconcile built
    // again could be matched by nothing.
    const std::optional<ALRenderTargetDesc>& gbuffer = plan.desc(Id::MainGBuffer);
    mTransientTargets.setMainSize(gbuffer ? gbuffer->width : 0, gbuffer ? gbuffer->height : 0);
    mTransientTargets.dropStaleDepth();

    // Each is given back before the next is asked for, so a description several resources share is built once. One
    // whose depth another transient resource shares is held until that one has it.
    Plan::IdSet owners;
    for (size_t i = 0; i < Plan::ID_COUNT; ++i)
    {
        const Plan::Resource& resource = plan[(Id)i];
        if (Plan::isPrewarmed((Id)i) && resource.mDesc && resource.mDepthFrom && Plan::isTransient(*resource.mDepthFrom))
        {
            owners.set((size_t)*resource.mDepthFrom);
        }
    }

    ALTransientTargets                          scope(mTransientTargets);
    std::array<LLRenderTarget*, Plan::ID_COUNT> held{};
    for (const bool sharers : { false, true })
    {
        for (size_t i = 0; i < Plan::ID_COUNT; ++i)
        {
            const Id              id       = (Id)i;
            const Plan::Resource& resource = plan[id];
            if (!ids.test(i) || !Plan::isPrewarmed(id) || !resource.mDesc || resource.mDepthFrom.has_value() != sharers)
            {
                continue;
            }

            LLRenderTarget* target = nullptr;
            if (!resource.mDepthFrom)
            {
                target = scope.acquire(*resource.mDesc, Plan::name(id));
            }
            else if (LLRenderTarget* owner = Plan::isTransient(*resource.mDepthFrom) ? held[(size_t)*resource.mDepthFrom]
                                                                                    : resourceTarget(*resource.mDepthFrom))
            {
                target = scope.acquire(*resource.mDesc, *owner, Plan::name(id));
            }

            if (!target)
            {
                changes.mFailed.set(i);
                continue;
            }
            changes.mPooled.set(i);
            if (owners.test(i))
            {
                held[i] = target;
            }
            else
            {
                scope.release(target);
            }
        }
    }
}

LLRenderTarget* LLPipeline::acquireResource(ALTransientTargets& scope, ALRenderResourcePlan::Id id, std::string_view name,
                                            LLRenderTarget* depth_from)
{
    const ALRenderResourcePlan::Resource& resource = mResourcePlan[id];
    if (!resource.mDesc)
    {
        return nullptr;
    }
    if (name.empty())
    {
        name = ALRenderResourcePlan::name(id);
    }
    if (!resource.mDepthFrom)
    {
        return scope.acquire(*resource.mDesc, name);
    }
    // Nothing over an owner left unbuilt, as a render size that fits nothing leaves the G-buffer.
    LLRenderTarget* owner = ALRenderResourcePlan::isTransient(*resource.mDepthFrom) ? depth_from
                                                                                     : resourceTarget(*resource.mDepthFrom);
    return owner && owner->getDepth() ? scope.acquire(*resource.mDesc, *owner, name) : nullptr;
}

// static
ALShaderInputs LLPipeline::shaderInputsFromSettings()
{
    static_assert(ALShaderInputs::MAX_INDEXED_GLTF_CHANNELS == LLGLSLShader::MAX_INDEXED_GLTF_CHANNELS);

    ALShaderSettings s;
    const S32 glsl = gGLManager.mGLSLVersionMajor * 100 + gGLManager.mGLSLVersionMinor;
    s.mTextureImageUnits = gGLManager.mNumTextureImageUnits;
    s.mHasClipControl    = gGLManager.mHasClipControl;
    // Intel's legacy drivers are held back from compute (LLGLManager::isIntelLegacy).
    s.mHasCompute = gGLManager.mHasCompute && glsl >= 430 && !gGLManager.isIntelLegacy();
    s.mIsD3D12    = gGLManager.mIsD3D12;

    s.mReverseZ                 = gSavedSettings.getBOOL("AlchemyRenderReverseZ");
    s.mHDR                      = gSavedSettings.getBOOL("RenderHDREnabled");
    s.mEmissive                 = gSavedSettings.getBOOL("RenderEnableEmissiveBuffer");
    s.mLightingMaxLevel         = gSavedSettings.getS32("RenderShaderLightingMaxLevel");
    s.mReservedTextureIndices   = gSavedSettings.getS32("RenderReservedTextureIndices");
    s.mSSR                      = gSavedSettings.getBOOL("RenderScreenSpaceReflections");
    s.mMirrors                  = gSavedSettings.getBOOL("RenderMirrors");
    s.mProbes                   = LLFeatureManager::getInstance()->isFeatureAvailable("RenderReflectionsEnabled") &&
                                  gSavedSettings.getBOOL("RenderReflectionsEnabled");
    s.mProbeLevel               = gSavedSettings.getS32("RenderReflectionProbeLevel");
    s.mShadowDetail             = gSavedSettings.getS32("RenderShadowDetail");
    s.mShadowFilterQuality      = gSavedSettings.getU32("AlchemyRenderShadowFilterQuality");
    s.mTerrainPlanarSampleCount = gSavedSettings.getS32("RenderTerrainPBRPlanarSampleCount");
    s.mTerrainPBRDetail         = gSavedSettings.getS32("RenderTerrainPBRDetail");
    s.mTerrainHexTiling         = gSavedSettings.getBOOL("AlchemyRenderTerrainHexTiling");
    s.mTerrainPBRNormals        = gSavedSettings.getBOOL("RenderTerrainPBRNormalsEnabled");
    s.mDynamicLOD               = gSavedSettings.getBOOL("RenderDynamicLOD");
    s.mWaterHextile             = gSavedSettings.getU32("AlchemyRenderWaterHextile");
    s.mWaterWaves               = gSavedSettings.getBOOL("AlchemyRenderWaterWaves");
    s.mDisableCompute           = gSavedSettings.getBOOL("AlchemyRenderDisableCompute");
    s.mTransparentWater         = gSavedSettings.getBOOL("RenderTransparentWater");
    s.mSSAO                     = gSavedSettings.getBOOL("RenderDeferredSSAO");
    s.mAvatarCloth              = gSavedSettings.getBOOL("RenderAvatarCloth");
    s.mGlowNoise                = gSavedSettings.getBOOL("RenderGlowNoise");
    s.mBloomHalation            = gSavedSettings.getBOOL("RenderBloomHalation");
    s.mSMAAPredication          = gSavedSettings.getBOOL("RenderSMAAPredication");
    s.mLocalTerrainPaint        = gSavedSettings.getBOOL("LocalTerrainPaintEnabled");
    s.mTerrainPaintBitDepth     = gSavedSettings.getU32("TerrainPaintBitDepth");

    const ALShaderInputs in = ALShaderInputs::make(s);
    const S32 requested_detail =
        std::clamp(s.mTerrainPBRDetail, (S32)TERRAIN_PBR_DETAIL_MIN, (S32)TERRAIN_PBR_DETAIL_MAX);
    if (in.mGlobal.mTerrainPBRDetail < requested_detail)
    {
        LL_INFOS_ONCE("ShaderLoading") << "RenderTerrainPBRDetail " << s.mTerrainPBRDetail << " clamped to "
                                       << in.mGlobal.mTerrainPBRDetail
                                       << ": full detail needs more fragment texture units than the "
                                       << s.mTextureImageUnits << " this GL implementation provides." << LL_ENDL;
    }
    return in;
}

// static
ALRenderResourceInputs LLPipeline::resourceInputsFromSettings(const ALShaderInputs& shaders)
{
    ALRenderResourceInputs in;

    if (gViewerWindow)
    {
        in.mWidth  = (U32)gViewerWindow->getWorldViewWidthRaw();
        in.mHeight = (U32)gViewerWindow->getWorldViewHeightRaw();
    }
    in.mResolutionDivisor    = gSavedSettings.getU32("RenderResolutionDivisor");
    in.mResolutionMultiplier = gSavedSettings.getF32("RenderResolutionMultiplier");

    in.mHDR      = gSavedSettings.getBOOL("RenderHDREnabled");
    in.mEmissive = gSavedSettings.getBOOL("RenderEnableEmissiveBuffer");
    in.mGBufferNormHDR = shaders.mGlobal.mGBufferNormHDR;
    in.mReverseZ       = shaders.mGlobal.mReverseZ;
    in.mCompute        = shaders.mLocal.mCompute;

    in.mShadowDetail     = gSavedSettings.getS32("RenderShadowDetail");
    in.mShadowScale      = gSavedSettings.getF32("RenderShadowResolutionScale");
    in.mShadowDepth32F   = gSavedSettings.getBOOL("AlchemyRenderShadowDepth32F");
    in.mSSAO             = gSavedSettings.getBOOL("RenderDeferredSSAO");
    in.mSSR              = gSavedSettings.getBOOL("RenderScreenSpaceReflections");
    in.mTransparentWater = gSavedSettings.getBOOL("RenderTransparentWater");

    in.mProbes = LLFeatureManager::getInstance()->isFeatureAvailable("RenderReflectionsEnabled") &&
                 gSavedSettings.getBOOL("RenderReflectionsEnabled");
    in.mProbeResolution  = ALRenderResourceInputs::probeResolution(gSavedSettings.getU32("RenderReflectionProbeResolution"));
    in.mProbeSuperSample = ALRenderResourceInputs::probeSuperSample(gSavedSettings.getS32("ALProbeSuperSample"));
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
    in.mDeferredNoise     = gSavedSettings.getF32("RenderDeferredNoise");

    in.mBakeWidth  = (U32)LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH;
    in.mBakeHeight = (U32)LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT;

    return in;
}

LLPipeline::ResourceChanges LLPipeline::reconcileResources(const ALRenderResourceInputs& requested)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    using Plan  = ALRenderResourcePlan;
    using Id    = Plan::Id;
    using IdSet = Plan::IdSet;

    ALRenderResourceInputs inputs = requested;
    Plan                   plan   = Plan::make(inputs);

    ResourceChanges changes;
    ensureResources(plan, IdSet().set(), changes);
    prewarmTransients(plan, IdSet().set(), changes);

    // Storage kept whose contents an input makes is filled again where that input changed; storage built just now was
    // filled as it was built.
    const IdSet refill = Plan::refilled(mResourceInputs, inputs) & changes.mKept;
    for (size_t i = 0; i < Plan::ID_COUNT; ++i)
    {
        if (U32* texture = refill.test(i) ? resourceTexture((Id)i) : nullptr)
        {
            LLImageGL::deleteTextures(1, texture);
            *texture = 0;
            createResourceTexture((Id)i);
            changes.mKept.reset(i);
            changes.mBuilt.set(i, *texture != 0);
        }
    }
    if (refill.any())
    {
        gGL.getTextureSlot(0)->unbind();
    }

    // A target the render size sizes that cannot be built at it halves the size, the height first and then the width,
    // until they can be or there is no size left: any but a bloom level or a shadow map, which are left unbuilt. Only
    // the targets the size sizes are freed and built again at each size: the rest neither drive the size nor follow it.
    const IdSet required = Plan::idsWhere(Plan::drivesRenderSize);
    const IdSet retried  = Plan::idsWhere(Plan::followsRenderSize);

    static LLCachedControl<bool> simulate_failure(gSavedSettings, "SimulateFBOFailure", false);
    const auto short_of_size = [&]() { return simulate_failure() || (changes.mFailed & required).any(); };
    const auto release_render_sized = [&]()
    {
        for (size_t i = 0; i < Plan::ID_COUNT; ++i)
        {
            if (LLRenderTarget* target = retried.test(i) ? resourceTarget((Id)i) : nullptr)
            {
                target->release();
            }
        }
        mTransientTargets.clear();
    };
    const auto retry_at = [&](U32 width, U32 height)
    {
        release_render_sized();
        if (width == 0 || height == 0)
        {
            return false;
        }
        inputs.mWidth  = width;
        inputs.mHeight = height;
        plan           = Plan::make(inputs);
        changes.mBuilt &= ~retried;
        changes.mKept &= ~retried;
        changes.mFailed &= ~retried;
        changes.mPooled &= ~retried;
        ensureResources(plan, retried, changes);
        prewarmTransients(plan, retried, changes);
        return !short_of_size();
    };

    if (short_of_size())
    {
        changes.mFullSize = false;

        U32  width  = inputs.mWidth;
        U32  height = inputs.mHeight;
        bool fitted = false;
        while (!fitted && width > 0 && height > 0)
        {
            height /= 2;
            fitted = retry_at(width, height);
            if (!fitted)
            {
                width /= 2;
                fitted = retry_at(width, height);
            }
        }

        if (!fitted)
        {
            // Nothing the size sizes is kept, whatever the last attempt managed.
            release_render_sized();
            changes.mFailed |= (changes.mBuilt | changes.mKept | changes.mPooled) & retried;
            changes.mBuilt &= ~retried;
            changes.mKept &= ~retried;
            changes.mPooled &= ~retried;
            LL_WARNS() << "Unable to allocate screen buffer at any resolution!" << LL_ENDL;
        }

        // Each attempt emptied the pool, a change of the main size freeing every target in it, the capture packs'
        // with the rest: they are readied again, so that one that cannot be built is known now and not mid-capture.
        changes.mPooled &= retried;
        prewarmTransients(plan, ~retried, changes);
    }

    // What each pack's passes keep about its targets.
    const auto pack_of = [this](Plan::Pack pack) -> RenderTargetPack&
    {
        return pack == Plan::Pack::Main ? mMainRT : (pack == Plan::Pack::Probe ? mAuxillaryRT : mHeroProbeRT);
    };
    for (Plan::Pack pack : { Plan::Pack::Main, Plan::Pack::Probe, Plan::Pack::Hero })
    {
        RenderTargetPack& rt = pack_of(pack);

        // The size the pack renders for: the main view's before the resolution scale, which the snapshots read back,
        // and a capture pack's its own.
        const std::optional<ALRenderTargetDesc>& gbuffer = plan.desc(Plan::inPack(pack, Plan::Slot::GBuffer));
        if (pack == Plan::Pack::Main)
        {
            rt.width  = inputs.mWidth;
            rt.height = inputs.mHeight;
        }
        else
        {
            rt.width  = gbuffer ? gbuffer->width : 0;
            rt.height = gbuffer ? gbuffer->height : 0;
        }

        // The pyramid is as deep as the levels the pool could build, from the first.
        rt.bloomMipCount = 0;
        while (rt.bloomMipCount < BLOOM_MAX_MIPS && changes.mPooled.test((size_t)Plan::bloomMip(pack, rt.bloomMipCount)))
        {
            ++rt.bloomMipCount;
        }
    }

    // The reflections march through last frame's scene, which a new target does not hold.
    if (changes.mBuilt.test((size_t)Id::SSRScene))
    {
        clearSceneMap();
    }
    // New storage holds no history.
    if (changes.mBuilt.test((size_t)Id::Exposure) || changes.mBuilt.test((size_t)Id::LensFlareA) ||
        changes.mBuilt.test((size_t)Id::LensFlareB))
    {
        resetExposureHistory();
    }
    if (changes.mBuilt.test((size_t)Id::BrdfLut))
    {
        createBrdfLut();
    }

    // The probe managers' own resources, planned beside their packs. They are freed here once nothing renders through
    // them, and built by initReflectionMaps: the reflection probes' below, once this plan is the pipeline's, and the
    // mirrors' by the hero manager's update, which runs in the frame before the world is drawn.
    if (!plan.reflectionProbes())
    {
        changes.mReflectionProbesFreed = mReflectionMapManager.freeResources();
    }
    if (!plan.heroProbes())
    {
        changes.mHeroProbesFreed = mHeroProbeManager.holdsResources();
        mHeroProbeManager.cleanup();
    }

    // What the state derived from the resources depends on, against what they were reconciled to before.
    const std::optional<ALRenderTargetDesc>& gbuffer_before = mResourcePlan.desc(Id::MainGBuffer);
    const std::optional<ALRenderTargetDesc>& gbuffer_after  = plan.desc(Id::MainGBuffer);
    const ALRenderTargetDesc before = gbuffer_before.value_or(ALRenderTargetDesc{});
    const ALRenderTargetDesc after  = gbuffer_after.value_or(ALRenderTargetDesc{});
    const IdSet changed             = Plan::changed(mResourcePlan, plan);
    changes.mMainSize       = before.width != after.width || before.height != after.height;
    changes.mGBufferFormats = before.color != after.color;
    changes.mDepthFormat    = before.depth != after.depth;
    changes.mHDR            = mResourceInputs.mHDR != inputs.mHDR;
    changes.mProbePack      = (changed & Plan::idsOf(Plan::Pack::Probe) & ~Plan::idsWhere(Plan::forMaterialPreview)).any();
    changes.mHeroPack       = (changed & Plan::idsOf(Plan::Pack::Hero)).any();

    mRequestedInputs = requested;
    mResourceInputs  = inputs;
    mResourcePlan    = plan;

    // The reflection manager's update runs after the frame is drawn, so the frame after probes come on would draw
    // through programs that read probes, with no cube array or coefficients to bind. Its update keeps them from then on.
    if (plan.reflectionProbes())
    {
        mReflectionMapManager.initReflectionMaps();
    }

    stop_glerror();
    return changes;
}

// static
void LLPipeline::refreshCachedSettings()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    LLPipeline::sAutoMaskAlphaDeferred = gSavedSettings.getBOOL("RenderAutoMaskAlphaDeferred");
    LLPipeline::sAutoMaskAlphaNonDeferred = gSavedSettings.getBOOL("RenderAutoMaskAlphaNonDeferred");
    LLPipeline::sUseFarClip = gSavedSettings.getBOOL("RenderUseFarClip");
    LLVOAvatar::sMaxNonImpostors = gSavedSettings.getU32("RenderAvatarMaxNonImpostors");
    LLVOAvatar::updateImpostorRendering(LLVOAvatar::sMaxNonImpostors);

    LLPipeline::sUseOcclusion =
            (!gUseWireframe
            && LLFeatureManager::getInstance()->isFeatureAvailable("UseOcclusion")
            && gSavedSettings.getBOOL("UseOcclusion")) ? 2 : 0;

    WindLightUseAtmosShaders = true; // DEPRECATED -- gSavedSettings.getBOOL("WindLightUseAtmosShaders");
    RenderDeferred = true; // DEPRECATED -- gSavedSettings.getBOOL("RenderDeferred");
    RenderDeferredSunWash = gSavedSettings.getF32("RenderDeferredSunWash");
    RenderShadowSplits = gSavedSettings.getS32("RenderShadowSplits");
    RenderDelayCreation = gSavedSettings.getBOOL("RenderDelayCreation");
    RenderAnimateRes = gSavedSettings.getBOOL("RenderAnimateRes");
    FreezeTime = gSavedSettings.getBOOL("FreezeTime");
    DebugBeaconLineWidth = gSavedSettings.getS32("DebugBeaconLineWidth");
    RenderHighlightBrightness = gSavedSettings.getF32("RenderHighlightBrightness");
    RenderHighlightColor = gSavedSettings.getColor4("RenderHighlightColor");
    RenderHighlightThickness = gSavedSettings.getF32("RenderHighlightThickness");
    RenderSpotLightsInNondeferred = gSavedSettings.getBOOL("RenderSpotLightsInNondeferred");
    PreviewAmbientColor = gSavedSettings.getColor4("PreviewAmbientColor");
    PreviewDiffuse0 = gSavedSettings.getColor4("PreviewDiffuse0");
    PreviewSpecular0 = gSavedSettings.getColor4("PreviewSpecular0");
    PreviewDiffuse1 = gSavedSettings.getColor4("PreviewDiffuse1");
    PreviewSpecular1 = gSavedSettings.getColor4("PreviewSpecular1");
    PreviewDiffuse2 = gSavedSettings.getColor4("PreviewDiffuse2");
    PreviewSpecular2 = gSavedSettings.getColor4("PreviewSpecular2");
    PreviewDirection0 = gSavedSettings.getVector3("PreviewDirection0");
    PreviewDirection1 = gSavedSettings.getVector3("PreviewDirection1");
    PreviewDirection2 = gSavedSettings.getVector3("PreviewDirection2");
    RenderGlowMaxExtractAlpha = gSavedSettings.getF32("RenderGlowMaxExtractAlpha");
    RenderGlowWarmthAmount = gSavedSettings.getF32("RenderGlowWarmthAmount");
    RenderGlowLumWeights = gSavedSettings.getVector3("RenderGlowLumWeights");
    RenderGlowWarmthWeights = gSavedSettings.getVector3("RenderGlowWarmthWeights");
    RenderGlowIterations = gSavedSettings.getS32("RenderGlowIterations");
    RenderGlowWidth = gSavedSettings.getF32("RenderGlowWidth");
    RenderGlowStrength = gSavedSettings.getF32("RenderGlowStrength");
    RenderDepthOfField = gSavedSettings.getBOOL("RenderDepthOfField");
    CameraFocusTransitionTime = gSavedSettings.getF32("CameraFocusTransitionTime");
    CameraFNumber = gSavedSettings.getF32("CameraFNumber");
    CameraFocalLength = gSavedSettings.getF32("CameraFocalLength");
    CameraFieldOfView = gSavedSettings.getF32("CameraFieldOfView");
    RenderShadowNoise = gSavedSettings.getF32("RenderShadowNoise");
    RenderShadowBlurSize = gSavedSettings.getF32("RenderShadowBlurSize");
    RenderSSAOScale = gSavedSettings.getF32("RenderSSAOScale");
    RenderSSAOMaxScale = gSavedSettings.getU32("RenderSSAOMaxScale");
    RenderSSAOFactor = gSavedSettings.getF32("RenderSSAOFactor");
    RenderSSAOEffect = gSavedSettings.getVector3("RenderSSAOEffect");
    RenderShadowOffsetError = gSavedSettings.getF32("RenderShadowOffsetError");
    RenderShadowBiasError = gSavedSettings.getF32("RenderShadowBiasError");
    RenderShadowOffset = gSavedSettings.getF32("RenderShadowOffset");
    RenderShadowBias = gSavedSettings.getF32("RenderShadowBias");
    RenderSpotShadowOffset = gSavedSettings.getF32("RenderSpotShadowOffset");
    RenderSpotShadowBias = gSavedSettings.getF32("RenderSpotShadowBias");
    RenderEdgeDepthCutoff = gSavedSettings.getF32("RenderEdgeDepthCutoff");
    RenderEdgeNormCutoff = gSavedSettings.getF32("RenderEdgeNormCutoff");
    RenderShadowGaussian = gSavedSettings.getVector3("RenderShadowGaussian");
    RenderShadowBlurDistFactor = gSavedSettings.getF32("RenderShadowBlurDistFactor");
    RenderDeferredAtmospheric = gSavedSettings.getBOOL("RenderDeferredAtmospheric");
    RenderHighlightFadeTime = gSavedSettings.getF32("RenderHighlightFadeTime");
    RenderFarClip = gSavedSettings.getF32("RenderFarClip");
    RenderShadowSplitExponent = gSavedSettings.getVector3("RenderShadowSplitExponent");
    RenderShadowErrorCutoff = gSavedSettings.getF32("RenderShadowErrorCutoff");
    RenderShadowFOVCutoff = gSavedSettings.getF32("RenderShadowFOVCutoff");
    CameraOffset = gSavedSettings.getBOOL("CameraOffset");
    // The Lightbox's range: the depth of field gather takes about 11 * CoF^2 taps a pixel, so a typed value past it
    // could stall the GPU until the driver resets it.
    CameraMaxCoF = llclamp(gSavedSettings.getF32("CameraMaxCoF"), 1.f, 32.f);
    CameraDoFResScale = gSavedSettings.getF32("CameraDoFResScale");
    RenderAutoHideSurfaceAreaLimit = gSavedSettings.getF32("RenderAutoHideSurfaceAreaLimit");
    RenderScreenSpaceReflectionIterations = gSavedSettings.getS32("RenderScreenSpaceReflectionIterations");
    RenderScreenSpaceReflectionRayStep = gSavedSettings.getF32("RenderScreenSpaceReflectionRayStep");
    RenderScreenSpaceReflectionDistanceBias = gSavedSettings.getF32("RenderScreenSpaceReflectionDistanceBias");
    RenderScreenSpaceReflectionDepthRejectBias = gSavedSettings.getF32("RenderScreenSpaceReflectionDepthRejectBias");
    RenderScreenSpaceReflectionAdaptiveStepMultiplier = gSavedSettings.getF32("RenderScreenSpaceReflectionAdaptiveStepMultiplier");
    RenderScreenSpaceReflectionGlossySamples = gSavedSettings.getS32("RenderScreenSpaceReflectionGlossySamples");
    RenderBufferVisualization = gSavedSettings.getS32("RenderBufferVisualization");
    RenderHeroProbeUpdateRate = gSavedSettings.getS32("RenderHeroProbeUpdateRate");
    RenderHeroProbeConservativeUpdateMultiplier = gSavedSettings.getS32("RenderHeroProbeConservativeUpdateMultiplier");

    RenderSpotLight = nullptr;

    if (gNonInteractive)
    {
        LLVOAvatar::sMaxNonImpostors = 1;
        LLVOAvatar::updateImpostorRendering(LLVOAvatar::sMaxNonImpostors);
    }

    bool enable_buffers = gSavedSettings.getBOOL("CollectFontVertexBuffers");
    LLFontTextCache::enableBufferCollection(enable_buffers);
    enable_buffers = gSavedSettings.getBOOL("CollectUIImageVertexBuffers");
    LLUIImage::enableDisplayListsCollection(enable_buffers);
}

void LLPipeline::releaseAllGLResources()
{
    assertInitialized();

    releaseRenderTargets();
    releaseDepthDependents();

    LLVOAvatar::resetImpostors();
    gBumpImageList.destroyGL();
    // The probes themselves stay, to render again into what the reconcile on the next context builds.
    mReflectionMapManager.freeResources();
    mHeroProbeManager.cleanup();

    // Clearing the recorded parameters matters as much as the release: they are
    // what generateLensDirt compares against, so leaving them set would make it
    // decide nothing had changed and skip rebuilding the plate it no longer has.
    mLensDirtMap.release();
    mLensDirtParams = LensDirtParams();

    mCGLut = nullptr;

    releaseScopeBuffers();
    clearReferenceStill();

    mDeferredUBO.release(); // shared shadow/SSAO block (UB_DEFERRED); re-created lazily on next use
    mDeferredUBODirty = true;

    if (LLEnvironment::instanceExists())
    {   // shared environment block (UB_ENVIRONMENT); re-created lazily on next bind
        LLEnvironment::instance().releaseGLBuffers();
    }

    // Spans of gGL's query pool, whose queries go with the context.
    mTerrainPrimitives.release();
    mWaterPrimitives.release();
    mWaterPatchPrimitives.release();
    mAttachmentSpans.clear();

    mGLResourcesLive = false;
}

void LLPipeline::releaseRenderTargets()
{
    using Plan = ALRenderResourcePlan;
    releaseSkyBehind();
    mTransientTargets.clear();
    for (size_t i = 0; i < Plan::ID_COUNT; ++i)
    {
        const Plan::Id id = (Plan::Id)i;
        if (LLRenderTarget* target = resourceTarget(id))
        {
            target->release();
        }
        else if (U32* texture = resourceTexture(id); texture && *texture)
        {
            LLImageGL::deleteTextures(1, texture);
            *texture = 0;
        }
    }

    for (RenderTargetPack* rt : { &mMainRT, &mAuxillaryRT, &mHeroProbeRT })
    {
        rt->bloomMipCount = 0;
    }

    mSkyBehindQuery      = {};
    mSkyBehindQueryFrame = 0;
    mLensFlareStateValid = false;
    mToneCurveLutDirty   = true;

    mResourcePlan = {};
}

void LLPipeline::releaseDepthDependents()
{
    mWaterSurface.release();
    mDepthPyramid.release();

    ALFarPlaneOverlay::release();
    if (ALSelectionOutline::instanceExists())
    {
        ALSelectionOutline::instance().release();
    }
}

namespace
{
    // How many changes the frame's application gathered into one reload or reconcile, for its log line.
    std::string applyingChanges(U32 changes)
    {
        return changes > 0 ? fmt::format(", applying {} {}", changes, changes == 1 ? "change" : "changes") : std::string();
    }

    // The names of a set of resources, comma-separated, for the log line.
    std::string resource_names(const ALRenderResourcePlan::IdSet& ids)
    {
        std::string list;
        for (size_t i = 0; i < ALRenderResourcePlan::ID_COUNT; ++i)
        {
            if (ids.test(i))
            {
                if (!list.empty())
                {
                    list += ", ";
                }
                list += ALRenderResourcePlan::name((ALRenderResourcePlan::Id)i);
            }
        }
        return list.empty() ? std::string("nothing") : list;
    }
}

// Whether a rebuild for a resize built every target at the size it was asked for, the rebuild a window dragged by its
// edge makes every frame.
static bool resized_as_asked(LLPipeline::RebuildCause cause, const LLPipeline::ResourceChanges& changes)
{
    return cause == LLPipeline::RebuildCause::Resize && changes.mFullSize && changes.mFailed.none();
}

bool LLPipeline::rebuildGLBuffers(const ALRenderResourceInputs& requested, RebuildCause cause)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    assertInitialized();

    // Comma-separated, for the log line.
    const auto add = [](std::string& list, std::string_view what)
    {
        if (!list.empty())
        {
            list += ", ";
        }
        list += what;
    };
    std::string invalidated;

    const bool new_context = !mGLResourcesLive;
    const ResourceChanges changes = reconcileResources(requested);
    mGLResourcesLive   = true;
    mResourcesFullSize = changes.mFullSize;
    // Which resource each pooled target serves over the next whole frame, for the log; not for a resize that built what
    // it asked, which is logged at debug level alone and comes every frame of a window's drag.
    if (!resized_as_asked(cause, changes))
    {
        mTransientTargets.recordNames();
    }

    std::string changed;
    if (new_context)
    {
        // releaseAllGLResources dropped everything derived with the context's resources; what is not restored here
        // is rebuilt where it is next wanted.
        add(changed, "new context");
        gBumpImageList.restoreGL();
        // Read again otherwise only when RenderColorGrade or RenderColorGradeLUT changes (LLPipeline::init).
        setupGradingLUT();
    }
    else
    {
        if (changes.mMainSize)
        {
            add(changed, "main size");
        }
        if (changes.mGBufferFormats)
        {
            add(changed, "G-buffer formats");
        }
        if (changes.mDepthFormat)
        {
            add(changed, "depth format");
        }
        if (changes.mHDR)
        {
            add(changed, "HDR");
        }
        if (changes.mProbePack)
        {
            add(changed, "probe pack");
        }
        if (changes.mHeroPack)
        {
            add(changed, "hero pack");
        }

        // The table is what gDeferredGenBrdfLutProgram draws, so it is drawn again whenever that is relinked; a table
        // built just now was drawn as it was built.
        if (cause == RebuildCause::ShaderReload && brdfLutStale())
        {
            createBrdfLut();
            add(invalidated, "BRDF LUT");
        }

        // The exposure adapts in the HDR scene's units, and the flare's history is measured in them.
        if (changes.mHDR)
        {
            resetExposureHistory();
            add(invalidated, "exposure and flare history");
        }

        // generateLensDirt rebuilds the plate itself when the frame's size or the plate's parameters move, and
        // captureReferenceStill drops a still of another size; the scopes resize their sample without losing it.
        if (changes.mMainSize && mLensDirtMap.isComplete())
        {
            add(invalidated, "lens dirt (next frame)");
        }
        if (changes.mMainSize && mReferenceStill.isComplete())
        {
            add(invalidated, "reference still (next frame)");
        }

        if (changes.mDepthFormat)
        {
            releaseDepthDependents();
            add(invalidated, "depth pyramid, water surface maps, selection outline, far-plane overlay");
        }

        // An impostor is G-buffer channels and depth in the main G-buffer's formats (generateImpostor), so those
        // are what make one stale; its size is its own.
        if (changes.mGBufferFormats || changes.mDepthFormat)
        {
            U32 baked = 0;
            for (LLCharacter* character : LLCharacter::sInstances)
            {
                baked += ((LLVOAvatar*)character)->mImpostor.isComplete() ? 1 : 0;
            }
            LLVOAvatar::resetImpostors();
            add(invalidated, fmt::format("avatar impostors ({} baked)", baked));
        }
    }

    std::string_view why;
    switch (cause)
    {
        case RebuildCause::Setting:
            why = " for a setting";
            break;
        case RebuildCause::ShaderReload:
            why = " after a shader reload";
            break;
        case RebuildCause::Resize:
            why = " for a resize";
            break;
        case RebuildCause::Snapshot:
            why = " for a snapshot";
            break;
    }

    // A new context builds everything, which its count says better than sixty names.
    const std::string built = new_context ? fmt::format("{} resources", changes.mBuilt.count())
                                          : resource_names(changes.mBuilt);
    std::string freed = changes.mFreed.any() ? resource_names(changes.mFreed) : std::string();
    if (changes.mReflectionProbesFreed)
    {
        add(freed, "reflection probes");
    }
    if (changes.mHeroProbesFreed)
    {
        add(freed, "hero probes");
    }
    const std::string applying = applyingChanges(mApplyingChanges);
    std::ostringstream line;
    line << "Render resources reconciled at " << mMainRT.deferredScreen.getWidth() << "x"
         << mMainRT.deferredScreen.getHeight() << why << applying
         << (changes.mFullSize                         ? std::string_view()
             : mMainRT.deferredScreen.isComplete() ? std::string_view(", halved for want of memory")
                                                   : std::string_view(", built at no size"))
         << "; changed: " << (changed.empty() ? std::string_view("nothing") : changed)
         << "; built: " << built << "; freed: " << (freed.empty() ? std::string_view("nothing") : freed) << "; kept "
         << changes.mKept.count() << "; pooled " << changes.mPooled.count() << ", "
         << (mTransientTargets.stats().mBytes >> 20) << " MiB"
         << (changes.mFailed.any() ? "; failed: " + resource_names(changes.mFailed) : std::string())
         << "; invalidated: " << (invalidated.empty() ? std::string_view("nothing") : invalidated);
    // A window dragged by its edge, or a panel sliding beside the world view, resizes it every frame: a resize that
    // built everything at the size asked is logged at debug level, so the log is not written every frame of it.
    if (resized_as_asked(cause, changes))
    {
        LL_DEBUGS("RenderResources") << line.str() << LL_ENDL;
    }
    else
    {
        LL_INFOS("RenderResources") << line.str() << LL_ENDL;
    }

    // A shadow map that cannot be built leaves its shadows undrawn rather than the view smaller.
    const ALRenderResourcePlan::IdSet shadows_failed =
        changes.mFailed & ALRenderResourcePlan::idsWhere(ALRenderResourcePlan::isShadowMap);
    if (shadows_failed.any())
    {
        LL_WARNS("RenderResources") << "Shadow maps not built, their shadows not drawn: " << resource_names(shadows_failed)
                                    << LL_ENDL;
    }

    return changes.mFullSize;
}

void LLPipeline::programsReloaded()
{
    // The table is what gDeferredGenBrdfLutProgram draws, so it is drawn again whenever that is relinked.
    const bool brdf_lut = brdfLutStale();
    if (brdf_lut)
    {
        createBrdfLut();
    }
    LL_INFOS("RenderResources") << "Programs reloaded" << applyingChanges(mApplyingChanges)
                                << "; render resources kept; invalidated: "
                                << (brdf_lut ? std::string_view("BRDF LUT") : std::string_view("nothing")) << LL_ENDL;
}

bool LLPipeline::brdfLutStale() const
{
    return mPbrBrdfLut.isComplete() && gDeferredGenBrdfLutProgram.linkCount() != mBrdfLutLinks;
}

void LLPipeline::createResourceTexture(ALRenderResourcePlan::Id id)
{
    using Id = ALRenderResourcePlan::Id;
    constexpr U32 noise_res = ALRenderResourcePlan::NOISE_SIZE;

    switch (id)
    {
        case Id::Noise:
        {
            std::vector<LLVector3> noise(noise_res * noise_res);
            F32 scaler = gSavedSettings.getF32("RenderDeferredNoise") / 100.f;
            for (LLVector3& texel : noise)
            {
                texel = LLVector3(ll_frand() - 0.5f, ll_frand() - 0.5f, 0.f);
                texel.normVec();
                texel.mV[2] = ll_frand() * scaler + 1.f - scaler / 2.f;
            }

            LLImageGL::generateTextures(1, &mNoiseMap);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mNoiseMap);
            LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_RGB16F, noise_res,
                                         noise_res, GL_RGB, GL_FLOAT, noise.data());
            break;
        }

        case Id::TrueNoise:
        {
            std::vector<F32> noise(noise_res * noise_res * 3);
            for (F32& value : noise)
            {
                value = ll_frand() * 2.0f - 1.0f;
            }

            LLImageGL::generateTextures(1, &mTrueNoiseMap);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mTrueNoiseMap);
            LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_RGB16F, noise_res,
                                         noise_res, GL_RGB, GL_FLOAT, noise.data());
            break;
        }

        case Id::SMAAArea:
        {
            std::vector<U8> tempBuffer(AREATEX_SIZE);
            for (U32 y = 0; y < AREATEX_HEIGHT; y++)
            {
                U32 srcY = AREATEX_HEIGHT - 1 - y;
                // unsigned int srcY = y;
                memcpy(&tempBuffer[y * AREATEX_PITCH], areaTexBytes + srcY * AREATEX_PITCH, AREATEX_PITCH);
            }

            LLImageGL::generateTextures(1, &mSMAAAreaMap);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mSMAAAreaMap);
            LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_RG8, AREATEX_WIDTH,
                                         AREATEX_HEIGHT, GL_RG, GL_UNSIGNED_BYTE, tempBuffer.data());
            break;
        }

        case Id::SMAASearch:
        {
            std::vector<U8> tempBuffer(SEARCHTEX_SIZE);
            for (U32 y = 0; y < SEARCHTEX_HEIGHT; y++)
            {
                U32 srcY = SEARCHTEX_HEIGHT - 1 - y;
                // unsigned int srcY = y;
                memcpy(&tempBuffer[y * SEARCHTEX_PITCH], searchTexBytes + srcY * SEARCHTEX_PITCH, SEARCHTEX_PITCH);
            }

            LLImageGL::generateTextures(1, &mSMAASearchMap);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mSMAASearchMap);
            LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_R8, SEARCHTEX_WIDTH,
                                         SEARCHTEX_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, tempBuffer.data());
            break;
        }

        case Id::SMAASample:
        {
            LLPointer<LLImageRaw>               raw_image = new LLImageRaw;
            LLPointer<LLImagePNG>               png_image = new LLImagePNG;
            static LLCachedControl<std::string> sample_path(gSavedSettings, "SamplePath", "");
            if (gDirUtilp->fileExists(sample_path()) && png_image->load(sample_path()) && png_image->decode(raw_image, 0.0f))
            {
                U32 format = 0;
                switch (raw_image->getComponents())
                {
                case 1:
                    format = GL_RED;
                    break;
                case 2:
                    format = GL_RG;
                    break;
                case 3:
                    format = GL_RGB;
                    break;
                case 4:
                    format = GL_RGBA;
                    break;
                default:
                    return;
                };
                LLImageGL::generateTextures(1, &mSMAASampleMap);
                gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mSMAASampleMap);
                LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_RGB8, raw_image->getWidth(),
                    raw_image->getHeight(), format, GL_UNSIGNED_BYTE, raw_image->getData());
                stop_glerror();
            }
            break;
        }

        case Id::ToneCurve:
        {
            // Allocated empty; bakeToneCurveLut fills it on the next colorCorrect,
            // and nothing samples it before then because uToneCurveAmount stays 0
            // until a bake reports a non-identity curve. RGBA16 rather than RGBA8
            // so the filter interpolates 16-bit samples instead of banding an
            // 8-bit display, and a sized format because allocateTexture2D takes
            // nothing else. One-shot: re-bakes go through setManualSubImage.
            LLImageGL::generateTextures(1, &mToneCurveLut);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mToneCurveLut);
            LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_RGBA16,
                                         ALToneCurveSet::LUT_SIZE, 1, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
            stop_glerror();
            mToneCurveLutDirty = true;   // fresh storage holds nothing
            break;
        }

        default:
            break;
    }
}

F32 lerpf(F32 a, F32 b, F32 w)
{
    return a + w * (b - a);
}

void LLPipeline::createBrdfLut()
{
    if (!mPbrBrdfLut.isComplete())
    {
        return;
    }
    mBrdfLutLinks = gDeferredGenBrdfLutProgram.linkCount();
    mPbrBrdfLut.bindTarget();

    if (gDeferredGenBrdfLutProgram.isComplete())
    {
        gDeferredGenBrdfLutProgram.bind();
        llassert_always(LLGLSLShader::sCurBoundShaderPtr != nullptr);

        // State this draw needs, rather than inheriting whatever the caller left live. It
        // runs outside any render pass, so nothing upstream has established it, and the
        // failure is silent in both directions: culling would drop the quad and leave the
        // table as whatever the allocation happened to contain, and a blend function would
        // fold that same garbage into the integral. Every PBR surface reads this texture.
        LLGLDisable cull_face(GL_CULL_FACE);
        LLGLDisable blend(GL_BLEND);

        // Counter-clockwise, so the quad is front-facing under the default winding. The
        // guard above already covers it -- this is so the draw does not depend on the guard.
        gGL.begin(LLRender::TRIANGLE_STRIP);
        gGL.vertex2f(-1, -1);
        gGL.vertex2f(1, -1);
        gGL.vertex2f(-1, 1);
        gGL.vertex2f(1, 1);
        gGL.end();
        gGL.flush();
    }
    else
    {
        LL_WARNS("Brad") << gDeferredGenBrdfLutProgram.mName << " failed to load, cannot be used!" << LL_ENDL;
    }

    gDeferredGenBrdfLutProgram.unbind();
    mPbrBrdfLut.flush();
}

void LLPipeline::resetExposureHistory()
{
    // Scale 1 at EV 0: the first frame adapts from no exposure at all. Zero is the lens flare's history: no drive, no
    // instability, no reference luminance.
    if (mExposureMap.isComplete())
    {
        mExposureMap.bindTarget();
        gGL.setClearColor(LLColor4::red);
        mExposureMap.clear();
        gGL.setClearColor(LLColor4::transparent);
        mExposureMap.flush();
    }
    if (mLensFlareState[0].isComplete())
    {
        clearLensFlareState();
    }
}

// Lens dirt plate generator.
//
// Generated rather than loaded from a bundled image: a plate built at the
// frame's own aspect keeps a mote round on an ultrawide with no cover-fit, and
// puts the grime itself on sliders instead of shipping fixed looks and hoping
// one fits the shot.
//
// This is not a per-frame pass. It runs when a parameter moves or the window
// resizes, and that budget is what lets the shader afford four cellular layers,
// two fBm fields and up to LENS_DIRT_MAX_LINES segment distance fields per
// pixel. The per-frame cost of the effect is still the one texture fetch in
// colorCorrect.
void LLPipeline::generateLensDirt()
{
    static LLCachedControl<F32> dirt_strength(gSavedSettings, "RenderLensDirtStrength", 0.f);
    static LLCachedControl<S32> dirt_seed(gSavedSettings, "RenderLensDirtSeed", 7);
    static LLCachedControl<F32> dirt_grime(gSavedSettings, "RenderLensDirtGrime", 1.f);
    static LLCachedControl<F32> dirt_mote_scale(gSavedSettings, "RenderLensDirtMoteScale", 1.f);
    static LLCachedControl<F32> dirt_smudge(gSavedSettings, "RenderLensDirtSmudge", 1.f);
    static LLCachedControl<S32> dirt_scratches(gSavedSettings, "RenderLensDirtScratches", 0);
    static LLCachedControl<F32> dirt_toe(gSavedSettings, "RenderLensDirtToe", 1.6f);
    static LLCachedControl<F32> dirt_gain(gSavedSettings, "RenderLensDirtGain", 1.f);

    // Deliberately not gated on gSnapshotNoPost. That flag is true for the one
    // frame a no-post snapshot is taken, so releasing the plate for it would
    // buy a full regeneration on the very next frame -- a hitch every time
    // someone takes one. colorCorrect already forces the strength uniform to 0
    // under a clean plate, which is the gate that actually matters.
    const bool dirt_on = (dirt_strength() > 0.f) && gLensDirtGenProgram.isComplete();

    if (!dirt_on)
    {
        // Release rather than merely skip, the same way the cross filter does:
        // allocating lazily is only worth anything if switching the effect off
        // gives the memory back. Doing it here rather than from a commit signal
        // keeps one teardown path, inside the render loop where the target is
        // owned, and lets the strength control stay a live slider -- wiring a
        // slider to a reallocation handler would fire on every mouse-move.
        if (mLensDirtMap.isComplete() || mLensDirtParams != LensDirtParams())
        {
            mLensDirtMap.release();
            mLensDirtParams = LensDirtParams();
        }
        return;
    }

    // The frame's own resolution. There is no fitting to do when the plate is
    // made at the shape it will be read at, and at GL_R8 even a 4K plate is
    // about 8 MB -- cheap for something only allocated while the effect is on.
    //
    // Generating at full resolution is only affordable because the rebuild is
    // debounced below. Capping the plate instead would bound the cost of one
    // rebuild but not the number of them, which is the part that hurts.
    const U32 gen_w = llmax(1u, mRT->screen.getWidth());
    const U32 gen_h = llmax(1u, mRT->screen.getHeight());

    LensDirtParams want;
    want.width      = gen_w;
    want.height     = gen_h;
    want.seed       = (F32)llclamp(dirt_seed(), 0, 999);
    want.grime      = llclamp(dirt_grime(), 0.f, 2.f);
    want.mote_scale = llclamp(dirt_mote_scale(), 0.5f, 2.f);
    want.smudge     = llclamp(dirt_smudge(), 0.f, 2.f);
    want.scratches  = llclamp(dirt_scratches(), 0, LENS_DIRT_MAX_LINES);
    want.toe        = llclamp(dirt_toe(), 0.6f, 4.f);
    want.gain       = llclamp(dirt_gain(), 0.5f, 2.5f);

    // Compared before the target is inspected, so a plate that failed to
    // allocate is not retried -- and this warning not repeated -- every frame
    // while VRAM stays exhausted. The next attempt happens when something
    // actually moves, and the bind site keeps the effect off through
    // isComplete() until one succeeds.
    if (want == mLensDirtParams)
    {
        return;
    }

    // Hold off while a generation slider is being dragged. A drag changes a
    // parameter every frame, and at full resolution rebuilding on each one is a
    // stutter rather than a preview -- the slower the machine, the more of the
    // drag it stutters through, which is backwards. The rebuild instead lands
    // once, on release, which is where the result is being looked for anyway.
    //
    // Only a drag is held off, which is the whole reason this is a UI signal
    // rather than a settle timer: a typed value, a reset button, applying a
    // Look, undo, and a window resize all arrive here with no slider down and
    // rebuild on the spot, where a timer would have made every one of them wait
    // for no reason. The first plate is never held off either -- until one
    // exists the effect is simply absent, and a pause reads as a bug.
    //
    // The capture test is a failsafe rather than part of the logic. LLSlider
    // raises the flag from handleMouseDown and lowers it from handleMouseUp,
    // but it implements no onMouseCaptureLost, so a capture stolen mid-drag
    // would otherwise leave the flag stuck and the plate frozen until something
    // else moved. No captor means no drag, whatever the flag says.
    if (mLensDirtSliderHeld && gFocusMgr.getMouseCapture() != nullptr)
    {
        return;
    }

    mLensDirtParams = want;

    // Everything above is a comparison; the zone starts where the work does.
    LL_PROFILE_GPU_ZONE("lens dirt generate");

    if (mLensDirtMap.getWidth() != gen_w || mLensDirtMap.getHeight() != gen_h)
    {
        mLensDirtMap.release();
        if (!mLensDirtMap.allocate(gen_w, gen_h, GL_R8))
        {
            LL_WARNS() << "Could not allocate the lens dirt plate; effect disabled until the parameters change" << LL_ENDL;
            return;
        }
        LL_DEBUGS("Pipeline") << "Lens dirt plate at " << gen_w << "x" << gen_h << LL_ENDL;
    }

    gLensDirtGenProgram.bind();

    gLensDirtGenProgram.uniform2f(LLShaderMgr::LENS_DIRT_RESOLUTION, (F32)gen_w, (F32)gen_h);
    gLensDirtGenProgram.uniform1f(LLShaderMgr::LENS_DIRT_SEED, want.seed);
    gLensDirtGenProgram.uniform1f(LLShaderMgr::LENS_DIRT_GRIME, want.grime);
    gLensDirtGenProgram.uniform1f(LLShaderMgr::LENS_DIRT_MOTE_SCALE, want.mote_scale);
    gLensDirtGenProgram.uniform1f(LLShaderMgr::LENS_DIRT_SMUDGE, want.smudge);
    gLensDirtGenProgram.uniform1i(LLShaderMgr::LENS_DIRT_SCRATCHES, want.scratches);
    gLensDirtGenProgram.uniform1f(LLShaderMgr::LENS_DIRT_TOE, want.toe);
    gLensDirtGenProgram.uniform1f(LLShaderMgr::LENS_DIRT_GAIN, want.gain);

    // No clear: the fullscreen triangle writes every texel with blending off,
    // so clearing first would be pure redundant fill.
    {
        LLGLDisable blend(GL_BLEND);

        mLensDirtMap.bindTarget();
        mScreenTriangleVB->setBuffer();
        mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        mLensDirtMap.flush();
    }

    gLensDirtGenProgram.unbind();
    stop_glerror();
}

void LLPipeline::setupGradingLUT()
{
    mCGLut = nullptr;

    std::string lut_name = gSavedSettings.getString("RenderColorGradeLUT");
    if (gSavedSettings.getBOOL("RenderColorGrade") && !lut_name.empty())
    {
        std::string lut_path = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "colorlut", lut_name);

        if (!LLFile::isfile(lut_path))
        {
            lut_path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "colorlut", lut_name);
        }

        if (LLFile::isfile(lut_path))
        {
            std::string temp_exten = gDirUtilp->getExtension(lut_path);
            bool decode_success = false;
            LLPointer<LLImageRaw> raw_image;
            bool flip_green = true;
            bool swap_bluegreen = true;
            if (temp_exten == "cube")
            {
                LutCube lutCube(lut_path);
                if (lutCube.colorCube.empty())
                {
                    LL_WARNS() << "Failed to decode color grading LUT: " << lut_path << LL_ENDL;
                    return;
                }

                if (lutCube.size > gGLManager.mGLMaxTextureSize)
                {
                    LL_WARNS() << "Color LUT of side " << lutCube.size << " exceeds the maximum texture size at path "
                               << lut_path << LL_ENDL;
                    return;
                }

                // The .cube path does not go through LLImageRaw: that class is
                // 8 bits per channel, and flattening a cube of floats to 256
                // levels before the hardware interpolates between them is where
                // LUT banding comes from. LutCube already lays its entries out
                // with x varying fastest, which is the order glTexSubImage3D
                // wants, and a cube needs neither of the axis fixups an image
                // strip does -- hence the two zeroes below.
                mCGLutSize = LLVector4((F32)lutCube.size, 0.f, 0.f);

                mCGLut = new ALTexture3D();
                if (!mCGLut->allocate(lutCube.size, lutCube.size, lutCube.size,
                                      GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT,
                                      lutCube.colorCube.data()))
                {
                    LL_WARNS() << "Failed to allocate color grading LUT texture." << LL_ENDL;
                    mCGLut = nullptr;
                }
                return;
            }
            else
            {
                enum class ELutExt
                {
                    EXT_IMG_TGA = 0,
                    EXT_IMG_PNG,
                    EXT_IMG_JPEG,
                    EXT_IMG_BMP,
                    EXT_IMG_WEBP,
                    EXT_NONE
                };

                ELutExt extension = ELutExt::EXT_NONE;
                if (temp_exten == "tga")
                {
                    extension = ELutExt::EXT_IMG_TGA;
                }
                else if (temp_exten == "png")
                {
                    extension = ELutExt::EXT_IMG_PNG;
                }
                else if (temp_exten == "jpg" || temp_exten == "jpeg")
                {
                    extension = ELutExt::EXT_IMG_JPEG;
                }
                else if (temp_exten == "bmp")
                {
                    extension = ELutExt::EXT_IMG_BMP;
                }
                else if (temp_exten == "webp")
                {
                    extension = ELutExt::EXT_IMG_WEBP;
                }

                raw_image = new LLImageRaw;

                switch (extension)
                {
                    default:
                        break;
                    case ELutExt::EXT_IMG_TGA:
                    {
                        LLPointer<LLImageTGA> tga_image = new LLImageTGA;
                        if (tga_image->load(lut_path) && tga_image->decode(raw_image, 0.0f))
                        {
                            decode_success = true;
                        }
                        break;
                    }
                    case ELutExt::EXT_IMG_PNG:
                    {
                        LLPointer<LLImagePNG> png_image = new LLImagePNG;
                        if (png_image->load(lut_path) && png_image->decode(raw_image, 0.0f))
                        {
                            decode_success = true;
                        }
                        break;
                    }
                    case ELutExt::EXT_IMG_JPEG:
                    {
                        LLPointer<LLImageJPEG> jpg_image = new LLImageJPEG;
                        if (jpg_image->load(lut_path) && jpg_image->decode(raw_image, 0.0f))
                        {
                            decode_success = true;
                        }
                        break;
                    }
                    case ELutExt::EXT_IMG_BMP:
                    {
                        LLPointer<LLImageBMP> bmp_image = new LLImageBMP;
                        if (bmp_image->load(lut_path) && bmp_image->decode(raw_image, 0.0f))
                        {
                            decode_success = true;
                        }
                        break;
                    }
                    case ELutExt::EXT_IMG_WEBP:
                    {
                        LLPointer<LLImageWebP> webp_image = new LLImageWebP;
                        if (webp_image->load(lut_path) && webp_image->decode(raw_image, 0.0f))
                        {
                            decode_success = true;
                        }
                        break;
                    }
                }
            }

            if (decode_success && raw_image)
            {
                U32 primary_format = 0;
                U32 int_format = 0;
                switch (raw_image->getComponents())
                {
                    case 3:
                    {
                        primary_format = GL_RGB;
                        int_format = GL_RGB8;
                        break;
                    }
                    case 4:
                    {
                        primary_format = GL_RGBA;
                        int_format = GL_RGBA8;
                        break;
                    }
                    default:
                    {
                        LL_WARNS() << "Color LUT has invalid number of color components: " << raw_image->getComponents() << LL_ENDL;
                        return;
                    }
                };

                S32 image_height = raw_image->getHeight();
                S32 image_width  = raw_image->getWidth();
                if ((image_height > 0 && image_height <= gGLManager.mGLMaxTextureSize) // within dimension limit
                    && ((image_height * image_height) == image_width))                 // width is height * height
                {
                    mCGLutSize = LLVector4((F32)image_height, (F32)flip_green, (F32)swap_bluegreen);

                    // The LUT is a cube of side image_height, stored in the raw image
                    // as image_height slices laid out horizontally.
                    mCGLut = new ALTexture3D();
                    if (!mCGLut->allocate(image_height, image_height, image_height,
                                          int_format, primary_format, GL_UNSIGNED_BYTE,
                                          raw_image->getData()))
                    {
                        LL_WARNS() << "Failed to allocate color grading LUT texture." << LL_ENDL;
                        mCGLut = nullptr;
                    }
                }
                else
                {
                    LL_WARNS() << "Color LUT is invalid width or height: " << image_height << " x " << image_width << " at path "
                                << lut_path << LL_ENDL;
                }
            }
            else
            {
                LL_WARNS() << "Failed to decode color grading LUT: " << lut_path << LL_ENDL;
            }
        }
    }
}


// Runs lazily from colorCorrect rather than from the settings signal for
// three reasons. A new context's reconcile recreates the texture behind the
// settings' back, and only a bake that runs on the way to drawing can refill
// it. A Look apply or an undo writes four curve settings in a row and a drag
// on the graph commits per mouse move, so one bake per rendered frame is the
// natural rate and the dirty flag coalesces the rest. And colorCorrect is the
// one place guaranteed a current context with nothing of its own bound yet --
// the upload borrows texture slot 0.
void LLPipeline::bakeToneCurveLut()
{
    LL_PROFILE_ZONE_SCOPED;
    mToneCurveLutDirty = false;

    // Once per change, so a name lookup is fine and no cached control is
    // needed. controlExists keeps a build whose settings lag behind from
    // asserting inside LLControlGroup.
    ALToneCurveSet curves;
    for (S32 c = 0; c < ALToneCurveSet::CH_COUNT; ++c)
    {
        const ALToneCurveSet::EChannel channel = static_cast<ALToneCurveSet::EChannel>(c);
        const char* name = ALToneCurveSet::settingName(channel);
        if (gSavedSettings.controlExists(name))
        {
            curves.setCurveFromLLSD(channel, gSavedSettings.getLLSD(name));
        }
    }

    mToneCurveIdentity = curves.isIdentity();
    if (mToneCurveIdentity || !mToneCurveLut)
    {
        // Identity never reaches the shader (colorCorrect uploads amount 0), so
        // stale texels are harmless; and with no texture there is nowhere to
        // bake. The flag stays clear either way: a recreated texture sets it
        // again itself.
        return;
    }

    std::vector<U16> texels;
    curves.bake(texels, ALToneCurveSet::LUT_SIZE);

    // setManualSubImage writes whatever is bound on the active unit. Slot 0 is
    // the convention every raw-name upload in this file uses.
    gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mToneCurveLut);
    LLImageGL::setManualSubImage(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), 0,
                                 ALToneCurveSet::LUT_SIZE, 1, GL_RGBA, GL_UNSIGNED_SHORT, texels.data());
    gGL.getTextureSlot(0)->unbind();
    stop_glerror();
}

void LLPipeline::restoreGL()
{
    assertInitialized();

    // The new context's programs and resources, from the inputs as they are now.
    loadRenderInputs();

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                part->restoreGL();
        }
        }
    }
}

bool LLPipeline::shadersLoaded()
{
    return (assertInitialized() && mShadersLoaded);
}

bool LLPipeline::canUseWindLightShaders() const
{
    return true;
}

bool LLPipeline::canUseAntiAliasing() const
{
    return true;
}

void LLPipeline::unloadShaders()
{
    LLViewerShaderMgr::instance()->unloadShaders();
    mShadersLoaded = false;
}

void LLPipeline::assertInitializedDoError()
{
    LL_ERRS() << "LLPipeline used when uninitialized." << LL_ENDL;
}

//============================================================================

void LLPipeline::enableShadows(const bool enable_shadows)
{
    //should probably do something here to wrangle shadows....
}

class LLOctreeDirtyTexture : public OctreeTraveler
{
public:
    const std::set<LLViewerFetchedTexture*>& mTextures;

    LLOctreeDirtyTexture(const std::set<LLViewerFetchedTexture*>& textures) : mTextures(textures) { }

    virtual void visit(const OctreeNode* node)
    {
        LLSpatialGroup* group = (LLSpatialGroup*) node->getListener(0);

        if (!group->hasState(LLSpatialGroup::GEOM_DIRTY) && !group->isEmpty())
        {
            for (LLSpatialGroup::draw_map_t::iterator i = group->mDrawMap.begin(); i != group->mDrawMap.end(); ++i)
            {
                for (LLSpatialGroup::drawmap_elem_t::iterator j = i->second.begin(); j != i->second.end(); ++j)
                {
                    LLDrawInfo* params = *j;
                    LLViewerFetchedTexture* tex = LLViewerTextureManager::staticCastToFetchedTexture(params->mTexture);
                    if (tex && mTextures.find(tex) != mTextures.end())
                    {
                        group->setState(LLSpatialGroup::GEOM_DIRTY);
                    }
                }
            }
        }

        for (LLSpatialGroup::bridge_list_t::iterator i = group->mBridgeList.begin(); i != group->mBridgeList.end(); ++i)
        {
            LLSpatialBridge* bridge = *i;
            traverse(bridge->mOctree);
        }
    }
};

// Called when a texture changes # of channels (causes faces to move to alpha pool)
void LLPipeline::dirtyPoolObjectTextures(const std::set<LLViewerFetchedTexture*>& textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    assertInitialized();

    // *TODO: This is inefficient and causes frame spikes; need a better way to do this
    //        Most of the time is spent in dirty.traverse.

    for (pool_set_t::iterator iter = mPools.begin(); iter != mPools.end(); ++iter)
    {
        LLDrawPool *poolp = *iter;
        if (poolp->isFacePool())
        {
            ((LLFacePool*) poolp)->dirtyTextures(textures);
        }
    }

    LLOctreeDirtyTexture dirty(textures);
    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                dirty.traverse(part->mOctree);
            }
        }
    }
}

LLDrawPool *LLPipeline::findPool(const U32 type, LLViewerTexture *tex0)
{
    assertInitialized();

    LLDrawPool *poolp = NULL;
    switch( type )
    {
    case LLDrawPool::POOL_SIMPLE:
        poolp = mSimplePool;
        break;

    case LLDrawPool::POOL_GRASS:
        poolp = mGrassPool;
        break;

    case LLDrawPool::POOL_ALPHA_MASK:
        poolp = mAlphaMaskPool;
        break;

    case LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK:
        poolp = mFullbrightAlphaMaskPool;
        break;

    case LLDrawPool::POOL_FULLBRIGHT:
        poolp = mFullbrightPool;
        break;

    case LLDrawPool::POOL_GLOW:
        poolp = mGlowPool;
        break;

    case LLDrawPool::POOL_TREE:
        poolp = get_if_there(mTreePools, (uintptr_t)tex0, (LLDrawPool*)0 );
        break;

    case LLDrawPool::POOL_TERRAIN:
        poolp = get_if_there(mTerrainPools, (uintptr_t)tex0, (LLDrawPool*)0 );
        break;

    case LLDrawPool::POOL_BUMP:
        poolp = mBumpPool;
        break;
    case LLDrawPool::POOL_MATERIALS:
        poolp = mMaterialsPool;
        break;
    case LLDrawPool::POOL_ALPHA_PRE_WATER:
        poolp = mAlphaPoolPreWater;
        break;
    case LLDrawPool::POOL_ALPHA_POST_WATER:
        poolp = mAlphaPoolPostWater;
        break;

    case LLDrawPool::POOL_AVATAR:
    case LLDrawPool::POOL_CONTROL_AV:
        break; // Do nothing

    case LLDrawPool::POOL_SKY:
        poolp = mSkyPool;
        break;

    case LLDrawPool::POOL_WATER:
        poolp = mWaterPool;
        break;

    case LLDrawPool::POOL_WL_SKY:
        poolp = mWLSkyPool;
        break;

    case LLDrawPool::POOL_GLTF_PBR:
        poolp = mPBROpaquePool;
        break;
    case LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK:
        poolp = mPBRAlphaMaskPool;
        break;

    case LLDrawPool::POOL_WATEREXCLUSION:
        poolp = mWaterExclusionPool;
        break;

    default:
        llassert(0);
        LL_ERRS() << "Invalid Pool Type in  LLPipeline::findPool() type=" << type << LL_ENDL;
        break;
    }

    return poolp;
}


LLDrawPool *LLPipeline::getPool(const U32 type, LLViewerTexture *tex0)
{
    LLDrawPool *poolp = findPool(type, tex0);
    if (poolp)
    {
        return poolp;
    }

    LLDrawPool *new_poolp = LLDrawPool::createPool(type, tex0);
    addPool( new_poolp );

    return new_poolp;
}


// static
LLDrawPool* LLPipeline::getPoolFromTE(const LLTextureEntry* te, LLViewerTexture* imagep)
{
    U32 type = getPoolTypeFromTE(te, imagep);
    return gPipeline.getPool(type, imagep);
}

//static
U32 LLPipeline::getPoolTypeFromTE(const LLTextureEntry* te, LLViewerTexture* imagep)
{
    if (!te || !imagep)
    {
        return 0;
    }

    LLMaterial* mat = te->getMaterialParams().get();
    LLGLTFMaterial* gltf_mat = te->getGLTFRenderMaterial();

    bool color_alpha = te->getColor().mV[3] < 0.999f;
    bool alpha = color_alpha;
    if (imagep)
    {
        alpha = alpha || (imagep->getComponents() == 4 && imagep->getType() != LLViewerTexture::MEDIA_TEXTURE) || (imagep->getComponents() == 2);
    }

    if (alpha && mat)
    {
        switch (mat->getDiffuseAlphaMode())
        {
            case 1:
                alpha = true; // Material's alpha mode is set to blend.  Toss it into the alpha draw pool.
                break;
            case 0: //alpha mode set to none, never go to alpha pool
            case 3: //alpha mode set to emissive, never go to alpha pool
                alpha = color_alpha;
                break;
            default: //alpha mode set to "mask", go to alpha pool if fullbright
                alpha = color_alpha; // Material's alpha mode is set to none, mask, or emissive.  Toss it into the opaque material draw pool.
                break;
        }
    }

    if (alpha || (gltf_mat && gltf_mat->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_BLEND))
    {
        return LLDrawPool::POOL_ALPHA;
    }
    else if ((te->getBumpmap() || te->getShiny()) && (!mat || mat->getNormalID().isNull()))
    {
        return LLDrawPool::POOL_BUMP;
    }
    else if (gltf_mat)
    {
        return LLDrawPool::POOL_GLTF_PBR;
    }
    else if (mat && !alpha)
    {
        return LLDrawPool::POOL_MATERIALS;
    }
    else
    {
        return LLDrawPool::POOL_SIMPLE;
    }
}


void LLPipeline::addPool(LLDrawPool *new_poolp)
{
    assertInitialized();
    mPools.insert(new_poolp);
    addToQuickLookup( new_poolp );
}

void LLPipeline::allocDrawable(LLViewerObject *vobj)
{
    LLDrawable *drawable = new LLDrawable(vobj);
    vobj->mDrawable = drawable;

    //encompass completely sheared objects by taking
    //the most extreme point possible (<1,1,0.5>)
    drawable->setRadius(LLVector3(1,1,0.5f).scaleVec(vobj->getScale()).length());
    if (vobj->isOrphaned())
    {
        drawable->setState(LLDrawable::FORCE_INVISIBLE);
    }
    drawable->updateXform(true);
}


void LLPipeline::unlinkDrawable(LLDrawable *drawable)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    assertInitialized();

    LLPointer<LLDrawable> drawablep = drawable; // make sure this doesn't get deleted before we are done

    // Based on flags, remove the drawable from the queues that it's on.
    if (drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        LLDrawable::drawable_vector_t::iterator iter = std::find(mMovedList.begin(), mMovedList.end(), drawablep);
        if (iter != mMovedList.end())
        {
            mMovedList.erase(iter);
        }
    }

    if (drawablep->getSpatialGroup())
    {
        if (!drawablep->getSpatialGroup()->getSpatialPartition()->remove(drawablep, drawablep->getSpatialGroup()))
        {
#ifdef LL_RELEASE_FOR_DOWNLOAD
            LL_WARNS() << "Couldn't remove object from spatial group!" << LL_ENDL;
#else
            LL_ERRS() << "Couldn't remove object from spatial group!" << LL_ENDL;
#endif
        }
    }

    mLights.erase(drawablep);

    for (light_set_t::iterator iter = mNearbyLights.begin();
                iter != mNearbyLights.end(); iter++)
    {
        if (iter->drawable == drawablep)
        {
            mNearbyLights.erase(iter);
            break;
        }
    }

    for (U32 i = 0; i < 2; ++i)
    {
        if (mShadowSpotLight[i] == drawablep)
        {
            mShadowSpotLight[i] = NULL;
        }

        if (mTargetShadowSpotLight[i] == drawablep)
        {
            mTargetShadowSpotLight[i] = NULL;
        }
    }
}

//static
void LLPipeline::removeMutedAVsLights(LLVOAvatar* muted_avatar)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    light_set_t::iterator iter = gPipeline.mNearbyLights.begin();
    while (iter != gPipeline.mNearbyLights.end())
    {
        const LLViewerObject* vobj = iter->drawable->getVObj();
        if (vobj
            && vobj->getAvatar()
            && vobj->isAttachment()
            && vobj->getAvatar() == muted_avatar)
        {
            gPipeline.mLights.erase(iter->drawable);
            iter = gPipeline.mNearbyLights.erase(iter);
        }
        else
        {
            iter++;
        }
    }
}

U32 LLPipeline::addObject(LLViewerObject *vobj)
{
    if (RenderDelayCreation)
    {
        mCreateQ.push_back(vobj);
    }
    else
    {
        createObject(vobj);
    }

    return 1;
}

void LLPipeline::createObjects(F32 max_dtime)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    LLTimer update_timer;

    while (!mCreateQ.empty() && update_timer.getElapsedTimeF32() < max_dtime)
    {
        LLViewerObject* vobj = mCreateQ.front();
        if (!vobj->isDead())
        {
            createObject(vobj);
        }
        mCreateQ.pop_front();
    }

    //for (LLViewerObject::vobj_list_t::iterator iter = mCreateQ.begin(); iter != mCreateQ.end(); ++iter)
    //{
    //  createObject(*iter);
    //}

    //mCreateQ.clear();
}

void LLPipeline::createObject(LLViewerObject* vobj)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LLDrawable* drawablep = vobj->mDrawable;

    if (!drawablep)
    {
        drawablep = vobj->createDrawable(this);
    }
    else
    {
        LL_ERRS() << "Redundant drawable creation!" << LL_ENDL;
    }

    llassert(drawablep);

    if (vobj->getParent())
    {
        vobj->setDrawableParent(((LLViewerObject*)vobj->getParent())->mDrawable); // LLPipeline::addObject 1
    }
    else
    {
        vobj->setDrawableParent(NULL); // LLPipeline::addObject 2
    }

    markRebuild(drawablep, LLDrawable::REBUILD_ALL);

    if (drawablep->getVOVolume() && RenderAnimateRes)
    {
        // fun animated res
        drawablep->updateXform(true);
        drawablep->clearState(LLDrawable::MOVE_UNDAMPED);
        drawablep->setScale(LLVector3(0,0,0));
        drawablep->makeActive();
    }
}


void LLPipeline::resetFrameStats()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    assertInitialized();

    sCompiles        = 0;
    mNumVisibleFaces = 0;
    LLRender::sDrawCalls    = 0;
    LLRender::sDrawElements = 0;

    if (mOldRenderDebugMask != mRenderDebugMask)
    {
        gObjectList.clearDebugText();
        mOldRenderDebugMask = mRenderDebugMask;
    }
}

//external functions for asynchronous updating
void LLPipeline::updateMoveDampedAsync(LLDrawable* drawablep)
{
    LL_PROFILE_ZONE_SCOPED;
    if (FreezeTime)
    {
        return;
    }
    if (!drawablep)
    {
        LL_ERRS() << "updateMove called with NULL drawablep" << LL_ENDL;
        return;
    }
    if (drawablep->isState(LLDrawable::EARLY_MOVE))
    {
        return;
    }

    assertInitialized();

    // update drawable now
    drawablep->clearState(LLDrawable::MOVE_UNDAMPED); // force to DAMPED
    drawablep->updateMove(); // returns done
    drawablep->setState(LLDrawable::EARLY_MOVE); // flag says we already did an undamped move this frame
    // Put on move list so that EARLY_MOVE gets cleared
    if (!drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        mMovedList.push_back(drawablep);
        drawablep->setState(LLDrawable::ON_MOVE_LIST);
    }
}

void LLPipeline::updateMoveNormalAsync(LLDrawable* drawablep)
{
    LL_PROFILE_ZONE_SCOPED;
    if (FreezeTime)
    {
        return;
    }
    if (!drawablep)
    {
        LL_ERRS() << "updateMove called with NULL drawablep" << LL_ENDL;
        return;
    }
    if (drawablep->isState(LLDrawable::EARLY_MOVE))
    {
        return;
    }

    assertInitialized();

    // update drawable now
    drawablep->setState(LLDrawable::MOVE_UNDAMPED); // force to UNDAMPED
    drawablep->updateMove();
    drawablep->setState(LLDrawable::EARLY_MOVE); // flag says we already did an undamped move this frame
    // Put on move list so that EARLY_MOVE gets cleared
    if (!drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        mMovedList.push_back(drawablep);
        drawablep->setState(LLDrawable::ON_MOVE_LIST);
    }
}

void LLPipeline::updateMovedList(LLDrawable::drawable_vector_t& moved_list)
{
    LL_PROFILE_ZONE_SCOPED;
    for (LLDrawable::drawable_vector_t::iterator iter = moved_list.begin();
         iter != moved_list.end(); )
    {
        LLDrawable::drawable_vector_t::iterator curiter = iter++;
        LLDrawable *drawablep = *curiter;
        if (!drawablep)
        {
            iter = moved_list.erase(curiter);
            continue;
        }
        bool done = true;
        if (!drawablep->isDead() && (!drawablep->isState(LLDrawable::EARLY_MOVE)))
        {
            done = drawablep->updateMove();
        }
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED);
        if (done)
        {
            if (drawablep->isRoot() && !drawablep->isState(LLDrawable::ACTIVE))
            {
                drawablep->makeStatic();
            }
            drawablep->clearState(LLDrawable::ON_MOVE_LIST);
            if (drawablep->isState(LLDrawable::ANIMATED_CHILD))
            { //will likely not receive any future world matrix updates
                // -- this keeps attachments from getting stuck in space and falling off your avatar
                drawablep->clearState(LLDrawable::ANIMATED_CHILD);
                markRebuild(drawablep, LLDrawable::REBUILD_VOLUME);
                if (drawablep->getVObj())
                {
                    drawablep->getVObj()->dirtySpatialGroup();
                }
            }
            iter = moved_list.erase(curiter);
        }
    }
}

void LLPipeline::updateMove()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    if (FreezeTime)
    {
        return;
    }

    assertInitialized();

    for (LLDrawable::drawable_set_t::iterator iter = mRetexturedList.begin();
            iter != mRetexturedList.end(); ++iter)
    {
        LLDrawable* drawablep = *iter;
        if (drawablep && !drawablep->isDead())
        {
            drawablep->updateTexture();
        }
    }
    mRetexturedList.clear();

    updateMovedList(mMovedList);

    //balance octrees
    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                part->mOctree->balance();
            }
        }

        //balance the VO Cache tree
        LLVOCachePartition* vo_part = region->getVOCachePartition();
        if(vo_part)
        {
            vo_part->mOctree->balance();
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// Culling and occlusion testing
/////////////////////////////////////////////////////////////////////////////

//static
F32 LLPipeline::calcPixelArea(LLVector3 center, LLVector3 size, LLCamera &camera)
{
    llassert(!gCubeSnapshot); // shouldn't be doing ANY of this during cube snap shots
    LLVector3 lookAt = center - camera.getOrigin();
    F32 dist = lookAt.length();

    //ramp down distance for nearby objects
    //shrink dist by dist/16.
    if (dist < 16.f)
    {
        dist /= 16.f;
        dist *= dist;
        dist *= 16.f;
    }

    //get area of circle around node
    F32 app_angle = atanf(size.length()/dist);
    F32 radius = app_angle*LLDrawable::sCurPixelAngle;
    return radius*radius * F_PI;
}

//static
F32 LLPipeline::calcPixelArea(const LLVector4a& center, const LLVector4a& size, LLCamera &camera)
{
    LLVector4a origin;
    origin.load3(camera.getOrigin().mV);

    LLVector4a lookAt;
    lookAt.setSub(center, origin);
    F32 dist = lookAt.getLength3().getF32();

    //ramp down distance for nearby objects
    //shrink dist by dist/16.
    if (dist < 16.f)
    {
        dist /= 16.f;
        dist *= dist;
        dist *= 16.f;
    }

    //get area of circle around node
    F32 app_angle = atanf(size.getLength3().getF32() / dist);
    F32 radius = app_angle * LLDrawable::sCurPixelAngle;
    return radius * radius * F_PI;
}

void LLPipeline::grabReferences(LLCullResult& result)
{
    sCull = &result;
}

void LLPipeline::clearReferences()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    sCull = NULL;
    mGroupSaveQ1.clear();
}

void check_references(LLSpatialGroup* group, LLDrawable* drawable)
{
    for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
    {
        LLDrawable* drawablep = (LLDrawable*)(*i)->getDrawable();
        if (drawable == drawablep)
        {
            LL_ERRS() << "LLDrawable deleted while actively reference by LLPipeline." << LL_ENDL;
        }
    }
}

void check_references(LLDrawable* drawable, LLFace* face)
{
    for (S32 i = 0; i < drawable->getNumFaces(); ++i)
    {
        if (drawable->getFace(i) == face)
        {
            LL_ERRS() << "LLFace deleted while actively referenced by LLPipeline." << LL_ENDL;
        }
    }
}

void check_references(LLSpatialGroup* group, LLFace* face)
{
    for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
    {
        LLDrawable* drawable = (LLDrawable*)(*i)->getDrawable();
        if(drawable)
        {
        check_references(drawable, face);
    }
}
}

void LLPipeline::checkReferences(LLFace* face)
{
#if 0
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, face);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, face);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, face);
        }

        for (LLCullResult::drawable_iterator iter = sCull->beginVisibleList(); iter != sCull->endVisibleList(); ++iter)
        {
            LLDrawable* drawable = *iter;
            check_references(drawable, face);
        }
    }
#endif
}

void LLPipeline::checkReferences(LLDrawable* drawable)
{
#if 0
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, drawable);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, drawable);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, drawable);
        }

        for (LLCullResult::drawable_iterator iter = sCull->beginVisibleList(); iter != sCull->endVisibleList(); ++iter)
        {
            if (drawable == *iter)
            {
                LL_ERRS() << "LLDrawable deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }
    }
#endif
}

void check_references(LLSpatialGroup* group, LLDrawInfo* draw_info)
{
    for (LLSpatialGroup::draw_map_t::iterator i = group->mDrawMap.begin(); i != group->mDrawMap.end(); ++i)
    {
        LLSpatialGroup::drawmap_elem_t& draw_vec = i->second;
        for (LLSpatialGroup::drawmap_elem_t::iterator j = draw_vec.begin(); j != draw_vec.end(); ++j)
        {
            LLDrawInfo* params = *j;
            if (params == draw_info)
            {
                LL_ERRS() << "LLDrawInfo deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }
    }
}


void LLPipeline::checkReferences(LLDrawInfo* draw_info)
{
#if 0
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, draw_info);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, draw_info);
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            check_references(group, draw_info);
        }
    }
#endif
}

void LLPipeline::checkReferences(LLSpatialGroup* group)
{
#if CHECK_PIPELINE_REFERENCES
    if (sCull)
    {
        for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
        {
            if (group == *iter)
            {
                LL_ERRS() << "LLSpatialGroup deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }

        for (LLCullResult::sg_iterator iter = sCull->beginAlphaGroups(); iter != sCull->endAlphaGroups(); ++iter)
        {
            if (group == *iter)
            {
                LL_ERRS() << "LLSpatialGroup deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }

        for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
        {
            if (group == *iter)
            {
                LL_ERRS() << "LLSpatialGroup deleted while actively referenced by LLPipeline." << LL_ENDL;
            }
        }
    }
#endif
}


bool LLPipeline::visibleObjectsInFrustum(LLCamera& camera)
{
    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                if (hasRenderType(part->mDrawableType))
                {
                    if (part->visibleObjectsInFrustum(camera))
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool LLPipeline::getVisibleExtents(LLCamera& camera, LLVector3& min, LLVector3& max)
{
    const F32 X = 65536.f;

    min = LLVector3(X,X,X);
    max = LLVector3(-X,-X,-X);

    LLViewerCamera::eCameraID saved_camera_id = LLViewerCamera::sCurCameraID;
    LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_WORLD;

    bool res = true;

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                if (hasRenderType(part->mDrawableType))
                {
                    if (!part->getVisibleExtents(camera, min, max))
                    {
                        res = false;
                    }
                }
            }
        }
    }

    // The water has no partition: its field, whole, with its lowest and highest water, and a receiver wherever the
    // camera meets it. Left out, a view of open sea, or a shadow split holding nothing but water, finds no receivers
    // and its split is left empty. Not from where the water is not drawn at all.
    if (hasRenderType(LLPipeline::RENDER_TYPE_WATER) && mWaterSurface.inView(camera) &&
        mWaterSurface.drawnFrom(*LLViewerCamera::getInstance()))
    {
        mWaterSurface.addExtents(min, max);
        res = false;
    }

    LLViewerCamera::sCurCameraID = saved_camera_id;
    return res;
}

// static
bool LLPipeline::isWaterClip()
{
    // We always pretend that we're not clipping water when rendering mirrors.
    return (gPipeline.mHeroProbeManager.isMirrorPass()) ? false : (!sRenderTransparentWater || gCubeSnapshot) && !sRenderingHUDs;
}

// static
// static
bool LLPipeline::hdrEnabled()
{
    return gPipeline.mLatchedInputs.mHDR;
}

LLRenderTarget::eDepthFormat LLPipeline::mainDepthFormat()
{
    return LLRender::sReverseZ ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24;
}

// static
void LLPipeline::updateReverseZ(bool reverse_z)
{
    // The setting where the context has clip control (ALShaderInputs). On a GPU without it the
    // setting can stay on but is inert, and every branch below then resolves to the forward
    // constants, so the forward path is never perturbed.
    const bool effective = reverse_z;
    const bool changed   = (effective != LLRender::sReverseZ);

    LLRender::sReverseZ = effective;

    // The draw distance ceiling follows the depth convention, for every consumer, and controls already shown take it.
    if (changed && LLStartUp::getStartupState() >= STATE_STARTED)
    {
        gAgentCamera.mDrawDistance = ALFarPlane::clampDrawDistance(gSavedSettings.getF32("RenderFarClip"), effective);
        LLWorld::getInstance()->setLandFarClip(gAgentCamera.mDrawDistance);
    }
    if (changed)
    {
        recap_draw_distance_controls();
    }

    // Issued unconditionally, not just on a change. These are the only writers of clip control
    // and clear depth in the tree, so if GL is ever reset underneath us -- a context restore,
    // an external state reset -- a change-gated latch would have no path back and would leave
    // reversed projections rasterizing against a [-1,1] clip volume. Four GL calls once per
    // setShaders() is not worth gating. Safe when the cap is absent: sReverseZ is then false,
    // the forward constants are what GL already has, and glClipControl is only reached with
    // mHasClipControl true (which guarantees the entry point resolved).
    if (gGLManager.mHasClipControl)
    {
        glClipControl(GL_LOWER_LEFT, effective ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
    }
    glClearDepth(effective ? 0.0 : 1.0);

    // The ambient depth func and polygon offset are tracked in the semantic convention, so the
    // physical state has to be re-derived through the current translation.
    LLGLDepthTest::rebase();
    gGL.rebasePolygonOffset();

    if (changed)
    {
        // Rebuild the sampler objects so the shadow compare func (LEQUAL<->GEQUAL) re-derives.
        // clearSamplers() MUST be followed by warmupSamplers(): the enum overload of
        // ALSamplerCache::get() indexes a flat table with no lazy path, so a cleared cache
        // hands out name 0 (asserting in debug) until warmup refills it. Only on a real flip --
        // rebuilding all 48 objects is not free.
        gGL.clearSamplers();
        gGL.warmupSamplers();

        LL_INFOS("RenderInit") << "Reverse-Z depth " << (effective ? "ENABLED" : "disabled")
                               << " (setting=" << gSavedSettings.getBOOL("AlchemyRenderReverseZ")
                               << ", clip control=" << gGLManager.mHasClipControl << ")" << LL_ENDL;
    }
}

void LLPipeline::updateCull(LLCamera& camera, LLCullResult& result)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("updateCull"); // should always be zero GPU time, but drop a timer to flush stuff out

    bool water_clip = isWaterClip();

    if (water_clip)
    {

        LLVector3 pnorm;

        F32 water_height = LLEnvironment::instance().getWaterHeight();

        if (sUnderWaterRender)
        {
            //camera is below water, cull above water
            pnorm.setVec(0, 0, 1);
        }
        else
        {
            //camera is above water, cull below water
            pnorm = LLVector3(0, 0, -1);
        }

        LLPlane plane;
        plane.setVec(LLVector3(0, 0, water_height), pnorm);

        camera.setUserClipPlane(plane);
    }
    else
    {
        camera.disableUserClipPlane();
    }

    grabReferences(result);

    sCull->clear();

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                if (hasRenderType(part->mDrawableType))
                {
                    part->cull(camera);
                }
            }
        }

        //scan the VO Cache tree
        LLVOCachePartition* vo_part = region->getVOCachePartition();
        if(vo_part)
        {
            vo_part->cull(camera, sUseOcclusion > 0);
        }
    }

    // Neither sky is part of a cutout's subject, and both come in here rather
    // than through a partition the cutout's choke points see.
    const bool cutout = cullingToCutoutSubject();

    if (!cutout &&
        hasRenderType(LLPipeline::RENDER_TYPE_SKY) &&
        gSky.mVOSkyp.notNull() &&
        gSky.mVOSkyp->mDrawable.notNull())
    {
        gSky.mVOSkyp->mDrawable->setVisible(camera);
        sCull->pushDrawable(gSky.mVOSkyp->mDrawable);
        gSky.updateCull();
        stop_glerror();
    }

    if (!cutout &&
        hasRenderType(LLPipeline::RENDER_TYPE_WL_SKY) &&
        gPipeline.canUseWindLightShaders() &&
        gSky.mVOWLSkyp.notNull() &&
        gSky.mVOWLSkyp->mDrawable.notNull())
    {
        gSky.mVOWLSkyp->mDrawable->setVisible(camera);
        sCull->pushDrawable(gSky.mVOWLSkyp->mDrawable);
    }
}

void LLPipeline::markNotCulled(LLSpatialGroup* group, LLCamera& camera)
{
    if (group->isEmpty())
    {
        return;
    }

    group->setVisible();

    if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
    {
        group->updateDistance(camera);
    }

    assertInitialized();

    if (!group->getSpatialPartition()->mRenderByGroup)
    { //render by drawable
        sCull->pushDrawableGroup(group);
    }
    else if (!cullingToCutoutSubject() || ALRenderIsolation::includes(group))
    {   //render by group
        // A cutout frame's main pass draws only its subject's groups. The rest
        // were marked visible above all the same: what one snapshot leaves out
        // must not read as out of view to object-cache eviction, LOD or
        // texture priority.
        sCull->pushVisibleGroup(group);
    }

    if (group->needsUpdate() ||
        group->getVisible(LLViewerCamera::sCurCameraID) < LLDrawable::getCurrentFrame() - 1)
    {
        // include this group in occlusion groups, not because it is an occluder, but because we want to run
        // an occlusion query to find out if it's an occluder
        markOccluder(group);
    }
    mNumVisibleNodes++;
}

void LLPipeline::markOccluder(LLSpatialGroup* group)
{
    if (sUseOcclusion > 1 && group && !group->isOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION))
    {
        LLSpatialGroup* parent = group->getParent();

        if (!parent || !parent->isOcclusionState(LLSpatialGroup::OCCLUDED))
        { //only mark top most occluders as active occlusion
            sCull->pushOcclusionGroup(group);
            group->setOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION);

            // An empty parent's bounds are its children's, so one query can cull them all; with one child they are that
            // child's (LLViewerOctreeGroup::rebound), and the query would be the child's again. Never the root, which
            // the cull never culls (LLOctreeCull::earlyFail): its children would take its OCCLUDED, and only an
            // earlyFail re-queries an occluded group, so nothing would bring the partition back.
            if (parent &&
                parent->getParent() &&
                !parent->isOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION) &&
                parent->getElementCount() == 0 &&
                parent->getOctreeNode()->getChildCount() > 1 &&
                parent->needsUpdate())
            {
                sCull->pushOcclusionGroup(parent);
                parent->setOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION);
            }
        }
    }
}

void LLPipeline::doOcclusion(LLCamera& camera)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("doOcclusion");
    llassert(!gCubeSnapshot);

    // Not while a shader profile holds the occlusion targets, which would refuse every box drawn for a query. The
    // groups the cull marked to be asked are let go, so that the next cull marks them again: one left marked is never
    // marked, and so never asked, again.
    if (!gGL.queries().canBegin(ALQueryKind::Occlusion))
    {
        for (LLCullResult::sg_iterator iter = sCull->beginOcclusionGroups(); iter != sCull->endOcclusionGroups(); ++iter)
        {
            (*iter)->clearOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION);
        }
        // The object cache's are the world camera's, which a shadow pass leaves to the main one.
        for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
        {
            LLVOCachePartition* vo_part = region->getVOCachePartition();
            if (vo_part && LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD)
            {
                vo_part->resetOccluders();
            }
        }
        return;
    }

    if (sReflectionProbesEnabled && sUseOcclusion > 1 && !LLPipeline::sShadowRender && !gCubeSnapshot)
    {
        LLGLSColorMask color_mask(false, false);
        LLGLDepthTest depth(GL_TRUE, GL_FALSE);
        LLGLDisable cull(GL_CULL_FACE);

        gOcclusionCubeProgram.bind();

        if (mCubeVB.isNull())
        { //cube VB will be used for issuing occlusion queries
            mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
        }
        mCubeVB->setBuffer();

        mReflectionMapManager.doOcclusion();
        mHeroProbeManager.doOcclusion();
        gOcclusionCubeProgram.unbind();
    }

    if (LLPipeline::sUseOcclusion > 1 &&
        (sCull->hasOcclusionGroups() || LLVOCachePartition::sNeedsOcclusionCheck))
    {
        LLVertexBuffer::unbind();

        LLGLSColorMask color_mask(false, false);

        LLGLDisable blend(GL_BLEND);
        gGL.getTextureSlot(0)->unbind();
        LLGLDepthTest depth(GL_TRUE, GL_FALSE);

        LLGLDisable cull(GL_CULL_FACE);

        gOcclusionCubeProgram.bind();

        if (mCubeVB.isNull())
        { //cube VB will be used for issuing occlusion queries
            mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
        }
        mCubeVB->setBuffer();

        for (LLCullResult::sg_iterator iter = sCull->beginOcclusionGroups(); iter != sCull->endOcclusionGroups(); ++iter)
        {
            LLSpatialGroup* group = *iter;
            if (!group->isDead())
            {
                group->doOcclusion(&camera);
                group->clearOcclusionState(LLSpatialGroup::ACTIVE_OCCLUSION);
            }
        }

        //apply occlusion culling to object cache tree
        for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
        {
            LLVOCachePartition* vo_part = (*iter)->getVOCachePartition();
            if(vo_part)
            {
                vo_part->processOccluders(&camera);
            }
        }
    }
}

bool LLPipeline::updateDrawableGeom(LLDrawable* drawablep)
{
    bool update_complete = drawablep->updateGeometry();
    if (update_complete && assertInitialized())
    {
        drawablep->setState(LLDrawable::BUILT);
    }
    return update_complete;
}

void LLPipeline::updateGL()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    {
        while (!LLGLUpdate::sGLQ.empty())
        {
            LLGLUpdate* glu = LLGLUpdate::sGLQ.front();
            glu->updateGL();
            glu->mInQ = false;
            LLGLUpdate::sGLQ.pop_front();
        }
    }
}

void LLPipeline::clearRebuildGroups()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LLSpatialGroup::sg_vector_t hudGroups;

    mGroupQ1Locked = true;
    // Iterate through all drawables on the priority build queue,
    for (LLSpatialGroup::sg_vector_t::iterator iter = mGroupQ1.begin();
         iter != mGroupQ1.end(); ++iter)
    {
        LLSpatialGroup* group = *iter;

        if (!group || group->isDead())
        {
            continue;
        }
        // If the group contains HUD objects, save the group
        if (group->isHUDGroup())
        {
            hudGroups.push_back(group);
        }
        // Else, no HUD objects so clear the build state
        else
        {
            group->clearState(LLSpatialGroup::IN_BUILD_Q1);
        }
    }

    // Clear the group
    mGroupQ1.clear();

    // Copy the saved HUD groups back in
    mGroupQ1.assign(hudGroups.begin(), hudGroups.end());
    mGroupQ1Locked = false;
}

void LLPipeline::clearRebuildDrawables()
{
    // Clear all drawables on the priority build queue,
    for (LLDrawable::drawable_list_t::iterator iter = mBuildQ1.begin();
         iter != mBuildQ1.end(); ++iter)
    {
        LLDrawable* drawablep = *iter;
        if (drawablep && !drawablep->isDead())
        {
            drawablep->clearState(LLDrawable::IN_REBUILD_Q);
        }
    }
    mBuildQ1.clear();

    //clear all moving bridges
    for (LLDrawable::drawable_vector_t::iterator iter = mMovedBridge.begin();
         iter != mMovedBridge.end(); ++iter)
    {
        LLDrawable *drawablep = *iter;
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED | LLDrawable::ON_MOVE_LIST | LLDrawable::ANIMATED_CHILD);
    }
    mMovedBridge.clear();

    //clear all moving drawables
    for (LLDrawable::drawable_vector_t::iterator iter = mMovedList.begin();
         iter != mMovedList.end(); ++iter)
    {
        LLDrawable *drawablep = *iter;
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED | LLDrawable::ON_MOVE_LIST | LLDrawable::ANIMATED_CHILD);
    }
    mMovedList.clear();

    for (LLDrawable::drawable_vector_t::iterator iter = mShiftList.begin();
        iter != mShiftList.end(); ++iter)
    {
        LLDrawable *drawablep = *iter;
        drawablep->clearState(LLDrawable::EARLY_MOVE | LLDrawable::MOVE_UNDAMPED | LLDrawable::ON_MOVE_LIST | LLDrawable::ANIMATED_CHILD | LLDrawable::ON_SHIFT_LIST);
    }
    mShiftList.clear();
}

void LLPipeline::rebuildPriorityGroups()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("rebuildPriorityGroups");

    LLTimer update_timer;
    assertInitialized();

    gMeshRepo.notifyLoadedMeshes();

    mGroupQ1Locked = true;
    // Iterate through all drawables on the priority build queue,
    for (LLSpatialGroup::sg_vector_t::iterator iter = mGroupQ1.begin();
         iter != mGroupQ1.end(); ++iter)
    {
        LLSpatialGroup* group = *iter;
        group->rebuildGeom();
        group->clearState(LLSpatialGroup::IN_BUILD_Q1);
    }

    mGroupSaveQ1 = std::move(mGroupQ1);
    mGroupQ1.clear();
    mGroupQ1Locked = false;

}

void LLPipeline::updateGeom(F32 max_dtime)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    if (gCubeSnapshot)
    {
        return;
    }

    assertInitialized();

    // notify various object types to reset internal cost metrics, etc.
    // for now, only LLVOVolume does this to throttle LOD changes
    LLVOVolume::preUpdateGeom();

    // Iterate through all drawables on the priority build queue,
    for (LLDrawable::drawable_list_t::iterator iter = mBuildQ1.begin();
         iter != mBuildQ1.end();)
    {
        LLDrawable::drawable_list_t::iterator curiter = iter++;
        LLDrawable* drawablep = *curiter;
        if (drawablep && !drawablep->isDead())
        {
            if (drawablep->isUnload())
            {
                drawablep->unload();
                drawablep->clearState(LLDrawable::FOR_UNLOAD);
            }

            if (updateDrawableGeom(drawablep))
            {
                drawablep->clearState(LLDrawable::IN_REBUILD_Q);
                mBuildQ1.erase(curiter);
            }
        }
        else
        {
            mBuildQ1.erase(curiter);
        }
    }

    updateMovedList(mMovedBridge);
}

void LLPipeline::markVisible(LLDrawable *drawablep, LLCamera& camera)
{
    if(drawablep && !drawablep->isDead())
    {
        if (cullingToCutoutSubject() && !ALRenderIsolation::includes(drawablep))
        {
            // Left out of a cutout's frame, but seen, for the same reason as in
            // markNotCulled. A bridge culls its own octree here, and each of its
            // groups is turned away there in turn.
            drawablep->setVisible(camera);
            return;
        }

        if (drawablep->isSpatialBridge())
        {
            const LLDrawable* root = ((LLSpatialBridge*) drawablep)->mDrawable;
            llassert(root); // trying to catch a bad assumption

            if (root && //  // this test may not be needed, see above
                    root->getVObj()->isAttachment())
            {
                LLDrawable* rootparent = root->getParent();
                if (rootparent) // this IS sometimes NULL
                {
                    LLViewerObject *vobj = rootparent->getVObj();
                    llassert(vobj); // trying to catch a bad assumption
                    if (vobj) // this test may not be needed, see above
                    {
                        LLVOAvatar* av = vobj->asAvatar();
                        if (av &&
                            ((!sImpostorRender && av->isImpostor()) //ignore impostor flag during impostor pass
                             || av->isInMuteList()
                             || (LLVOAvatar::AOA_JELLYDOLL == av->getOverallAppearance() && !av->needsImpostorUpdate()) ))
                        {
                            return;
                        }
                    }
                }
            }
            sCull->pushBridge((LLSpatialBridge*) drawablep);
        }
        else
        {

            sCull->pushDrawable(drawablep);
        }

        drawablep->setVisible(camera);
    }
}

void LLPipeline::markMoved(LLDrawable *drawablep, bool damped_motion)
{
    if (!drawablep)
    {
        //LL_ERRS() << "Sending null drawable to moved list!" << LL_ENDL;
        return;
    }

    if (drawablep->isDead())
    {
        LL_WARNS() << "Marking NULL or dead drawable moved!" << LL_ENDL;
        return;
    }

    if (drawablep->getParent())
    {
        //ensure that parent drawables are moved first
        markMoved(drawablep->getParent(), damped_motion);
    }

    assertInitialized();

    if (!drawablep->isState(LLDrawable::ON_MOVE_LIST))
    {
        if (drawablep->isSpatialBridge())
        {
            mMovedBridge.push_back(drawablep);
        }
        else
        {
            mMovedList.push_back(drawablep);
        }
        drawablep->setState(LLDrawable::ON_MOVE_LIST);
    }
    // The last request this frame wins. This used to be labelled "UNDAMPED trumps DAMPED", which is
    // not what it does: a damped call clears the flag an earlier undamped one set. That is relied
    // on -- LLSelectMgr::selectionMove, the joystick edit path, moves a selected object (which marks
    // itself undamped) and then asks for a damped move so a noisy analog device eases rather than
    // snaps. A caller that needs a snap to survive must not be followed by a damped call for the
    // same drawable; LLViewerObject::updateDrawable, the common one, only ever upgrades a drawable
    // that is already queued. updateMovedList clears the flag once the move is processed.
    if (! damped_motion)
    {
        drawablep->setState(LLDrawable::MOVE_UNDAMPED);
    }
    else if (drawablep->isState(LLDrawable::MOVE_UNDAMPED))
    {
        drawablep->clearState(LLDrawable::MOVE_UNDAMPED);
    }
}

void LLPipeline::markShift(LLDrawable *drawablep)
{
    if (!drawablep || drawablep->isDead() || !drawablep->getVObj())
    {
        return;
    }

    assertInitialized();

    if (!drawablep->isState(LLDrawable::ON_SHIFT_LIST))
    {
        drawablep->getVObj()->setChanged(LLXform::SHIFTED);
        if (drawablep->getParent())
        {
            markShift(drawablep->getParent());
        }
        mShiftList.push_back(drawablep);
        drawablep->setState(LLDrawable::ON_SHIFT_LIST);
    }
}

void LLPipeline::shiftObjects(const LLVector3 &offset)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    assertInitialized();

    glClear(GL_DEPTH_BUFFER_BIT);
    gDepthDirty = true;

    LLVector4a offseta;
    offseta.load3(offset.mV);

    for (LLDrawable::drawable_vector_t::iterator iter = mShiftList.begin();
            iter != mShiftList.end(); iter++)
    {
        LLDrawable *drawablep = *iter;
        if (drawablep->isDead() || !drawablep->getVObj())
        {
            continue;
        }
        drawablep->shiftPos(offseta);
        drawablep->clearState(LLDrawable::ON_SHIFT_LIST);
    }
    mShiftList.resize(0);

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
        {
            LLSpatialPartition* part = region->getSpatialPartition(i);
            if (part)
            {
                part->shift(offseta);
            }
        }
    }

    mReflectionMapManager.shift(offseta);

    LLHUDText::shiftAll(offset);
    LLHUDNameTag::shiftAll(offset);

    display_update_camera();
}

void LLPipeline::markTextured(LLDrawable *drawablep)
{
    if (drawablep && !drawablep->isDead() && assertInitialized())
    {
        mRetexturedList.insert(drawablep);
    }
}

void LLPipeline::countTextureMatrixOp(const LLMatrix4& mat)
{
    ++mTextureMatrixOps;
    if (sShadowRender)
    {
        ++mTextureMatrixOpsShadow;
    }
    else if (gCubeSnapshot)
    {
        ++mTextureMatrixOpsProbe;
    }
    if (mat.isIdentity())
    {
        ++mTextureMatrixOpsIdentity;
    }
}

void LLPipeline::markGLRebuild(LLGLUpdate* glu)
{
    if (glu && !glu->mInQ)
    {
        LLGLUpdate::sGLQ.push_back(glu);
        glu->mInQ = true;
    }
}

void LLPipeline::markPartitionMove(LLDrawable* drawable)
{
    if (!drawable->isState(LLDrawable::PARTITION_MOVE) &&
        !drawable->getPositionGroup().equals3(LLVector4a::getZero()))
    {
        drawable->setState(LLDrawable::PARTITION_MOVE);
        mPartitionQ.push_back(drawable);
    }
}

void LLPipeline::processPartitionQ()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    for (LLDrawable::drawable_list_t::iterator iter = mPartitionQ.begin(); iter != mPartitionQ.end(); ++iter)
    {
        LLDrawable* drawable = *iter;
        if (!drawable->isDead())
        {
            drawable->updateBinRadius();
            drawable->movePartition();
        }
        drawable->clearState(LLDrawable::PARTITION_MOVE);
    }

    mPartitionQ.clear();
}

void LLPipeline::markMeshDirty(LLSpatialGroup* group)
{
    mMeshDirtyGroup.push_back(group);
}

void LLPipeline::markRebuild(LLSpatialGroup* group)
{
    if (group && !group->isDead() && group->getSpatialPartition())
    {
        if (!group->hasState(LLSpatialGroup::IN_BUILD_Q1))
        {
            llassert_always(!mGroupQ1Locked);

            mGroupQ1.push_back(group);
            group->setState(LLSpatialGroup::IN_BUILD_Q1);
        }
    }
}

void LLPipeline::markRebuild(LLDrawable *drawablep, LLDrawable::EDrawableFlags flag)
{
    if (drawablep && !drawablep->isDead() && assertInitialized())
    {
        if (!drawablep->isState(LLDrawable::IN_REBUILD_Q))
        {
            mBuildQ1.push_back(drawablep);
            drawablep->setState(LLDrawable::IN_REBUILD_Q); // mark drawable as being in priority queue
        }

        drawablep->setState(flag);
    }
}

void LLPipeline::stateSort(LLCamera& camera, LLCullResult &result)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("stateSort");

    if (hasAnyRenderType(LLPipeline::RENDER_TYPE_AVATAR,
                      LLPipeline::RENDER_TYPE_CONTROL_AV,
                      LLPipeline::RENDER_TYPE_TERRAIN,
                      LLPipeline::RENDER_TYPE_TREE,
                      LLPipeline::RENDER_TYPE_SKY,
                      LLPipeline::RENDER_TYPE_VOIDWATER,
                      LLPipeline::RENDER_TYPE_WATER,
                      LLPipeline::END_RENDER_TYPES))
    {
        //clear faces from face pools
        gPipeline.resetDrawOrders();
    }

    //LLVertexBuffer::unbind();

    grabReferences(result);
    for (LLCullResult::sg_iterator iter = sCull->beginDrawableGroups(); iter != sCull->endDrawableGroups(); ++iter)
    {
        LLSpatialGroup* group = *iter;
        if (group->isDead())
        {
            continue;
        }
        group->checkOcclusion();
        if (sUseOcclusion > 1 && group->isOcclusionState(LLSpatialGroup::OCCLUDED))
        {
            markOccluder(group);
        }
        else
        {
            group->setVisible();
            for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
            {
                LLDrawable* drawablep = (LLDrawable*)(*i)->getDrawable();
                markVisible(drawablep, camera);
            }

            { //rebuild mesh as soon as we know it's visible
                group->rebuildMesh();
            }
        }
    }

    if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
    {
        LLSpatialGroup* last_group = NULL;
        bool fov_changed = LLViewerCamera::getInstance()->isDefaultFOVChanged();
        for (LLCullResult::bridge_iterator i = sCull->beginVisibleBridge(); i != sCull->endVisibleBridge(); ++i)
        {
            LLCullResult::bridge_iterator cur_iter = i;
            LLSpatialBridge* bridge = *cur_iter;
            LLSpatialGroup* group = bridge->getSpatialGroup();

            if (last_group == NULL)
            {
                last_group = group;
            }

            if (!bridge->isDead() && group && !group->isOcclusionState(LLSpatialGroup::OCCLUDED))
            {
                stateSort(bridge, camera, fov_changed);
            }

            if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD &&
                last_group != group && last_group->changeLOD())
            {
                last_group->mLastUpdateDistance = last_group->mDistance;
            }

            last_group = group;
        }

        if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD &&
            last_group && last_group->changeLOD())
        {
            last_group->mLastUpdateDistance = last_group->mDistance;
        }
    }

    for (LLCullResult::sg_iterator iter = sCull->beginVisibleGroups(); iter != sCull->endVisibleGroups(); ++iter)
    {
        LLSpatialGroup* group = *iter;
        if (group->isDead())
        {
            continue;
        }
        group->checkOcclusion();
        if (sUseOcclusion > 1 && group->isOcclusionState(LLSpatialGroup::OCCLUDED))
        {
            markOccluder(group);
        }
        else
        {
            group->setVisible();
            stateSort(group, camera);

            { //rebuild mesh as soon as we know it's visible
                group->rebuildMesh();
            }
        }
    }

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWABLE("stateSort");
        for (LLCullResult::drawable_iterator iter = sCull->beginVisibleList();
             iter != sCull->endVisibleList(); ++iter)
        {
            LLDrawable *drawablep = *iter;
            if (!drawablep->isDead())
            {
                stateSort(drawablep, camera);
            }
        }
    }

    postSort(camera);
}

void LLPipeline::stateSort(LLSpatialGroup* group, LLCamera& camera)
{
    if (group->changeLOD())
    {
        for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
        {
            LLDrawable* drawablep = (LLDrawable*)(*i)->getDrawable();
            stateSort(drawablep, camera);
        }

        if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
        { //avoid redundant stateSort calls
            group->mLastUpdateDistance = group->mDistance;
        }
    }
}

void LLPipeline::stateSort(LLSpatialBridge* bridge, LLCamera& camera, bool fov_changed)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    if (bridge->getSpatialGroup()->changeLOD() || fov_changed)
    {
        bool force_update = false;
        bridge->updateDistance(camera, force_update);
    }
}

// The test a terrain patch's octree group passed (LLSpatialPartition::cull), put to the patch. A group holds up to
// OctreeMaxNodeCapacity patches, a quarter of its region or more, and every patch drawn is tessellated whole, so a
// camera that sees a corner of the group would draw all of it: a shadow cascade a few metres across would tessellate
// the near field at its highest levels. The main view's reach is left to the group's test.
static bool terrain_patch_in_frustum(const LLDrawable* drawablep, LLCamera& camera)
{
    const LLVector4a* extents = drawablep->getSpatialExtents();
    LLVector4a center;
    center.setAdd(extents[0], extents[1]);
    center.mul(0.5f);
    LLVector4a radius;
    radius.setSub(extents[1], extents[0]);
    radius.mul(0.5f);
    return (LLPipeline::sShadowRender ? camera.AABBInFrustum(center, radius) : camera.AABBInFrustumNoFarClip(center, radius)) != 0;
}

void LLPipeline::stateSort(LLDrawable* drawablep, LLCamera& camera)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    if (!drawablep
        || drawablep->isDead()
        || !hasRenderType(drawablep->getRenderType()))
    {
        return;
    }

    // markVisible turned everything else away; this is the backstop for
    // anything that reaches the visible list some other way.
    if (cullingToCutoutSubject() && !ALRenderIsolation::includes(drawablep))
    {
        return;
    }

    // SL-11353
    // ignore our own geo when rendering spotlight shadowmaps...
    //
    if (RenderSpotLight && drawablep == RenderSpotLight)
    {
        return;
    }

    if (LLSelectMgr::getInstance()->mHideSelectedObjects)
    {
//      if (drawablep->getVObj().notNull() &&
//          drawablep->getVObj()->isSelected())
// [RLVa:KB] - Checked: 2010-09-28 (RLVa-1.2.1f) | Modified: RLVa-1.2.1f
        const LLViewerObject* pObj = drawablep->getVObj();
        if ( (pObj) && (pObj->isSelected()) &&
             ( (!RlvActions::isRlvEnabled()) ||
               ( ((!pObj->isHUDAttachment()) || (!gRlvAttachmentLocks.isLockedAttachment(pObj->getRootEdit()))) &&
                 (RlvActions::canEdit(pObj)) ) ) )
// [/RVLa:KB]
        {
            return;
        }
    }

    if (drawablep->isAvatar())
    { //don't draw avatars beyond render distance or if we don't have a spatial group.
        if ((drawablep->getSpatialGroup() == NULL) ||
            (drawablep->getSpatialGroup()->mDistance > LLVOAvatar::sRenderDistance))
        {
            return;
        }

        LLVOAvatar* avatarp = (LLVOAvatar*) drawablep->getVObj().get();
        if (!avatarp->isVisible())
        {
            return;
        }
    }

    assertInitialized();

    if (hasRenderType(drawablep->mRenderType))
    {
        if (!drawablep->isState(LLDrawable::INVISIBLE|LLDrawable::FORCE_INVISIBLE))
        {
            drawablep->setVisible(camera, NULL, false);
        }
    }

    if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
    {
        //if (drawablep->isVisible()) isVisible() check here is redundant, if it wasn't visible, it wouldn't be here
        {
            if (!drawablep->isActive())
            {
                bool force_update = false;
                drawablep->updateDistance(camera, force_update);
            }
            else if (drawablep->isAvatar())
            {
                bool force_update = false;
                drawablep->updateDistance(camera, force_update); // calls vobj->updateLOD() which calls LLVOAvatar::updateVisibility()
            }
        }
    }

    if (drawablep->getRenderType() == RENDER_TYPE_TERRAIN && !terrain_patch_in_frustum(drawablep, camera))
    {
        return;
    }

    if (!drawablep->getVOVolume())
    {
        for (LLDrawable::face_list_t::iterator iter = drawablep->mFaces.begin();
                iter != drawablep->mFaces.end(); iter++)
        {
            LLFace* facep = *iter;

            if (facep->hasGeometry())
            {
                if (facep->getPool())
                {
                    facep->getPool()->enqueue(facep);
                }
                else
                {
                    break;
                }
            }
        }
    }

    mNumVisibleFaces += drawablep->getNumFaces();
}


void forAllDrawables(LLCullResult::sg_iterator begin,
                     LLCullResult::sg_iterator end,
                     void (*func)(LLDrawable*))
{
    for (LLCullResult::sg_iterator i = begin; i != end; ++i)
    {
        LLSpatialGroup* group = *i;
        if (group->isDead())
        {
            continue;
        }
        for (LLSpatialGroup::element_iter j = group->getDataBegin(); j != group->getDataEnd(); ++j)
        {
            if((*j)->hasDrawable())
            {
                func((LLDrawable*)(*j)->getDrawable());
            }
        }
    }
}

void LLPipeline::forAllVisibleDrawables(void (*func)(LLDrawable*))
{
    forAllDrawables(sCull->beginDrawableGroups(), sCull->endDrawableGroups(), func);
    forAllDrawables(sCull->beginVisibleGroups(), sCull->endVisibleGroups(), func);
}

//function for creating scripted beacons
void renderScriptedBeacons(LLDrawable* drawablep)
{
    LLViewerObject *vobj = drawablep->getVObj();
    if (vobj
        && !vobj->isAvatar()
        && !vobj->getParent()
        && vobj->flagScripted())
    {
        if (gPipeline.sRenderBeacons)
        {
            gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(1.f, 0.f, 0.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f), LLPipeline::DebugBeaconLineWidth);
        }

        if (gPipeline.sRenderHighlight)
        {
            S32 face_id;
            S32 count = drawablep->getNumFaces();
            for (face_id = 0; face_id < count; face_id++)
            {
                LLFace * facep = drawablep->getFace(face_id);
                if (facep)
                {
                    gPipeline.mHighlightFaces.push_back(facep);
                }
            }
        }
    }
}

void renderScriptedTouchBeacons(LLDrawable *drawablep)
{
    LLViewerObject *vobj = drawablep->getVObj();
    if (vobj && !vobj->isAvatar() && !vobj->getParent() && vobj->flagScripted() && vobj->flagHandleTouch())
    {
        if (gPipeline.sRenderBeacons)
        {
            gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(1.f, 0.f, 0.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f),
                                       LLPipeline::DebugBeaconLineWidth);
        }

        if (gPipeline.sRenderHighlight)
        {
            S32 face_id;
            S32 count = drawablep->getNumFaces();
            for (face_id = 0; face_id < count; face_id++)
            {
                LLFace *facep = drawablep->getFace(face_id);
                if (facep)
                {
                    gPipeline.mHighlightFaces.push_back(facep);
                }
            }
        }
    }
}

void renderPhysicalBeacons(LLDrawable *drawablep)
{
    LLViewerObject *vobj = drawablep->getVObj();
    if (vobj &&
        !vobj->isAvatar()
        //&& !vobj->getParent()
        && vobj->flagUsePhysics())
    {
        if (gPipeline.sRenderBeacons)
        {
            gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(0.f, 1.f, 0.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f),
                                       LLPipeline::DebugBeaconLineWidth);
        }

        if (gPipeline.sRenderHighlight)
        {
            S32 face_id;
            S32 count = drawablep->getNumFaces();
            for (face_id = 0; face_id < count; face_id++)
            {
                LLFace *facep = drawablep->getFace(face_id);
                if (facep)
                {
                    gPipeline.mHighlightFaces.push_back(facep);
                }
            }
        }
    }
}

void renderMOAPBeacons(LLDrawable *drawablep)
{
    LLViewerObject *vobj = drawablep->getVObj();

    if (!vobj || vobj->isAvatar())
        return;

    bool beacon  = false;
    U8   tecount = vobj->getNumTEs();
    for (int x = 0; x < tecount; x++)
    {
        if (vobj->getTE(x)->hasMedia())
        {
            beacon = true;
            break;
        }
    }
    if (beacon)
    {
        if (gPipeline.sRenderBeacons)
        {
            gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", LLColor4(1.f, 1.f, 1.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f),
                                       LLPipeline::DebugBeaconLineWidth);
        }

        if (gPipeline.sRenderHighlight)
        {
            S32 face_id;
            S32 count = drawablep->getNumFaces();
            for (face_id = 0; face_id < count; face_id++)
            {
                LLFace *facep = drawablep->getFace(face_id);
                if (facep)
                {
                    gPipeline.mHighlightFaces.push_back(facep);
                }
            }
        }
    }
}

void renderParticleBeacons(LLDrawable *drawablep)
{
    // Look for attachments, objects, etc.
    LLViewerObject *vobj = drawablep->getVObj();
    if (vobj && vobj->isParticleSource())
    {
        if (gPipeline.sRenderBeacons)
        {
            LLColor4 light_blue(0.5f, 0.5f, 1.f, 0.5f);
            gObjectList.addDebugBeacon(vobj->getPositionAgent(), "", light_blue, LLColor4(1.f, 1.f, 1.f, 0.5f),
                                       LLPipeline::DebugBeaconLineWidth);
        }

        if (gPipeline.sRenderHighlight)
        {
            S32 face_id;
            S32 count = drawablep->getNumFaces();
            for (face_id = 0; face_id < count; face_id++)
            {
                LLFace *facep = drawablep->getFace(face_id);
                if (facep)
                {
                    gPipeline.mHighlightFaces.push_back(facep);
                }
            }
        }
    }
}

void renderSoundHighlights(LLDrawable *drawablep)
{
    // Look for attachments, objects, etc.
    LLViewerObject *vobj = drawablep->getVObj();
    if (vobj && vobj->isAudioSource())
    {
        if (gPipeline.sRenderHighlight)
        {
            S32 face_id;
            S32 count = drawablep->getNumFaces();
            for (face_id = 0; face_id < count; face_id++)
            {
                LLFace *facep = drawablep->getFace(face_id);
                if (facep)
                {
                    gPipeline.mHighlightFaces.push_back(facep);
                }
            }
        }
    }
}

void LLPipeline::postSort(LLCamera &camera)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    assertInitialized();

    if (!gCubeSnapshot)
    {
        // rebuild drawable geometry
        for (LLCullResult::sg_iterator i = sCull->beginDrawableGroups(); i != sCull->endDrawableGroups(); ++i)
        {
            LLSpatialGroup *group = *i;
            if (group->isDead())
            {
                continue;
            }
            if (!sUseOcclusion || !group->isOcclusionState(LLSpatialGroup::OCCLUDED))
            {
                group->rebuildGeom();
            }
        }
        // rebuild groups
        sCull->assertDrawMapsEmpty();

        rebuildPriorityGroups();
    }

    // build render map
    for (LLCullResult::sg_iterator i = sCull->beginVisibleGroups(); i != sCull->endVisibleGroups(); ++i)
    {
        LLSpatialGroup *group = *i;

        if (group->isDead())
        {
            continue;
        }

        if ((sUseOcclusion && group->isOcclusionState(LLSpatialGroup::OCCLUDED)) ||
            (RenderAutoHideSurfaceAreaLimit > 0.f &&
             group->mSurfaceArea > RenderAutoHideSurfaceAreaLimit * llmax(group->mObjectBoxSize, 10.f)))
        {
            continue;
        }

        if (group->hasState(LLSpatialGroup::NEW_DRAWINFO) && group->hasState(LLSpatialGroup::GEOM_DIRTY) && !gCubeSnapshot)
        {  // no way this group is going to be drawable without a rebuild
            group->rebuildGeom();
        }

        for (LLSpatialGroup::draw_map_t::iterator j = group->mDrawMap.begin(); j != group->mDrawMap.end(); ++j)
        {
            LLSpatialGroup::drawmap_elem_t &src_vec = j->second;
            if (!hasRenderType(j->first))
            {
                continue;
            }

            for (LLSpatialGroup::drawmap_elem_t::iterator k = src_vec.begin(); k != src_vec.end(); ++k)
            {
                LLDrawInfo *info = *k;

                sCull->pushDrawInfo(j->first, info);
                if (!sShadowRender && !gCubeSnapshot)
                {
                    addTrianglesDrawn(info->mCount);
                }
            }
        }

        if (hasRenderType(LLPipeline::RENDER_TYPE_PASS_ALPHA))
        {
            LLSpatialGroup::draw_map_t::iterator alpha = group->mDrawMap.find(LLRenderPass::PASS_ALPHA);

            if (alpha != group->mDrawMap.end())
            {  // store alpha groups for sorting
                LLSpatialBridge *bridge = group->getSpatialPartition()->asBridge();
                if (LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_WORLD && !gCubeSnapshot)
                {
                    if (bridge)
                    {
                        LLCamera trans_camera = bridge->transformCamera(camera);
                        group->updateDistance(trans_camera);
                    }
                    else
                    {
                        group->updateDistance(camera);
                    }
                }

                if (hasRenderType(LLDrawPool::POOL_ALPHA))
                {
                    sCull->pushAlphaGroup(group);
                }
            }

            LLSpatialGroup::draw_map_t::iterator rigged_alpha = group->mDrawMap.find(LLRenderPass::PASS_ALPHA_RIGGED);

            if (rigged_alpha != group->mDrawMap.end())
            {  // store rigged alpha groups for LLDrawPoolAlpha prepass (skip distance update, rigged attachments use depth buffer)
                if (hasRenderType(LLDrawPool::POOL_ALPHA))
                {
                    sCull->pushRiggedAlphaGroup(group);
                }
            }
        }
    }

    // pack vertex buffers for groups that chose to delay their updates
    {
        LL_PROFILE_GPU_ZONE("rebuildMesh");
        for (LLSpatialGroup::sg_vector_t::iterator iter = mMeshDirtyGroup.begin(); iter != mMeshDirtyGroup.end(); ++iter)
        {
            (*iter)->rebuildMesh();
        }
    }

    mMeshDirtyGroup.clear();

    // An impostor bake sets sShadowRender too, but it draws blended alpha for real and
    // therefore needs it sorted -- a depth-only shadow pass is the only thing that does not.
    // Skipping the sort here left layered transparents on an impostored avatar (hair over a
    // visor, a skirt over stockings) blending in octree order instead of back-to-front.
    if (!sShadowRender || sImpostorRender)
    {
        // order alpha groups by distance
        std::sort(sCull->beginAlphaGroups(), sCull->endAlphaGroups(), LLSpatialGroup::CompareDepthGreater());

        // order rigged alpha groups by avatar attachment order
        std::sort(sCull->beginRiggedAlphaGroups(), sCull->endRiggedAlphaGroups(), LLSpatialGroup::CompareRenderOrder());
    }

    // only render if the flag is set. The flag is only set if we are in edit mode or the toggle is set in the menus
    if (LLFloaterReg::instanceVisible("beacons") && !sShadowRender && !gCubeSnapshot)
    {
        if (sRenderScriptedTouchBeacons)
        {
            // Only show the beacon on the root object.
            forAllVisibleDrawables(renderScriptedTouchBeacons);
        }
        else if (sRenderScriptedBeacons)
        {
            // Only show the beacon on the root object.
            forAllVisibleDrawables(renderScriptedBeacons);
        }

        if (sRenderPhysicalBeacons)
        {
            // Only show the beacon on the root object.
            forAllVisibleDrawables(renderPhysicalBeacons);
        }

        if (sRenderMOAPBeacons)
        {
            forAllVisibleDrawables(renderMOAPBeacons);
        }

        if (sRenderParticleBeacons)
        {
            forAllVisibleDrawables(renderParticleBeacons);
        }

        // If god mode, also show audio cues
        if (sRenderSoundBeacons && gAudiop)
        {
            // Walk all sound sources and render out beacons for them. Note, this isn't done in the ForAllVisibleDrawables function, because
            // some are not visible.
            LLAudioEngine::source_map::iterator iter;
            for (iter = gAudiop->mAllSources.begin(); iter != gAudiop->mAllSources.end(); ++iter)
            {
                LLAudioSource *sourcep = iter->second;

                LLVector3d pos_global = sourcep->getPositionGlobal();
                LLVector3  pos        = gAgent.getPosAgentFromGlobal(pos_global);
                if (gPipeline.sRenderBeacons)
                {
                    // pos += LLVector3(0.f, 0.f, 0.2f);
                    gObjectList.addDebugBeacon(pos, "", LLColor4(1.f, 1.f, 0.f, 0.5f), LLColor4(1.f, 1.f, 1.f, 0.5f), DebugBeaconLineWidth);
                }
            }
            // now deal with highlights for all those seeable sound sources
            forAllVisibleDrawables(renderSoundHighlights);
        }
    }

    // If managing your telehub, draw beacons at telehub and currently selected spawnpoint.
    if (LLFloaterTelehub::renderBeacons() && !sShadowRender && !gCubeSnapshot)
    {
        LLFloaterTelehub::addBeacons();
    }

    if (!sShadowRender && !gCubeSnapshot)
    {
        mSelectedFaces.clear();

        bool tex_index_changed = false;
        if (!gNonInteractive)
        {
            LLRender::eTexIndex tex_index = sRenderHighlightTextureChannel;
            setRenderHighlightTextureChannel(gFloaterTools->getPanelFace()->getTextureChannelToEdit());
            tex_index_changed = sRenderHighlightTextureChannel != tex_index;
        }

        // Draw face highlights for selected faces.
        if (LLSelectMgr::getInstance()->getTEMode())
        {
            struct f : public LLSelectedTEFunctor
            {
                virtual bool apply(LLViewerObject *object, S32 te)
                {
                    if (object->mDrawable)
                    {
                        LLFace *facep = object->mDrawable->getFace(te);
                        if (facep)
                        {
                            gPipeline.mSelectedFaces.push_back(facep);
                        }
                    }
                    return true;
                }
            } func;
            LLSelectMgr::getInstance()->getSelection()->applyToTEs(&func);

            if (tex_index_changed)
            {
                // Rebuild geometry for all selected faces with PBR textures
                for (const LLFace* face : gPipeline.mSelectedFaces)
                {
                    if (const LLViewerObject* vobj = face->getViewerObject())
                    {
                        if (const LLTextureEntry* tep = vobj->getTE(face->getTEOffset()))
                        {
                            if (tep->getGLTFRenderMaterial())
                            {
                                gPipeline.markRebuild(face->getDrawable(), LLDrawable::REBUILD_VOLUME);
                            }
                        }
                    }
                }
            }
        }
    }

    LLVertexBuffer::flushBuffers();
    // LLSpatialGroup::sNoDelete = false;
}


void render_hud_elements()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    gPipeline.disableLights();

    LLGLSUIDefault gls_ui;

    //LLGLEnable stencil(GL_STENCIL_TEST);
    //glStencilFunc(GL_ALWAYS, 255, 0xFFFFFFFF);
    //glStencilMask(0xFFFFFFFF);
    //glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    gUIProgram.bind();
    gGL.color4f(1, 1, 1, 1);
    LLGLDepthTest depth(GL_TRUE, GL_FALSE);

    if (gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
    {
        gViewerWindow->renderSelections(false, false, false); // For HUD version in render_ui_3d()

        // Draw the tracking overlays
        LLTracker::render3D();

        if (LLWorld::instanceExists())
        {
            // Show the property lines
            LLWorld::getInstance()->renderPropertyLines();
        }
        LLViewerParcelMgr::getInstance()->render();
        LLViewerParcelMgr::getInstance()->renderParcelCollision();
    }
    else if (gForceRenderLandFence)
    {
        // This is only set when not rendering the UI, for parcel snapshots
        LLViewerParcelMgr::getInstance()->render();
    }
    else if (gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_HUD))
    {
        LLHUDText::renderAllHUD();
    }

    gUIProgram.unbind();
}

static inline void bindHighlightProgram(LLGLSLShader& program)
{
    if ((LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_INTERFACE) > 0))
    {
        program.bind();
        gGL.diffuseColor4f(1, 1, 1, 0.5f);
    }
}

static inline void unbindHighlightProgram(LLGLSLShader& program)
{
    if (LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_INTERFACE) > 0)
    {
        program.unbind();
    }
}

void LLPipeline::renderSelectedFaces(const LLColor4& color)
{
    if (!mFaceSelectImagep)
    {
        mFaceSelectImagep = LLViewerTextureManager::getFetchedTexture(IMG_FACE_SELECT);
    }

    if (mFaceSelectImagep)
    {
        // Make sure the selection image gets downloaded and decoded
        mFaceSelectImagep->addTextureStats((F32)MAX_IMAGE_AREA);

        for (auto facep : mSelectedFaces)
        {
            if (!facep || !facep->getViewerObject())
            {
                LLSelectMgr::getInstance()->clearSelections();
                return;
            }
            if (!facep->getDrawable() || facep->getDrawable()->isDead())
            {
                LL_ERRS() << "Bad face on selection" << LL_ENDL;
                return;
            }

            facep->renderSelected(mFaceSelectImagep, color);
        }
    }
}

void LLPipeline::renderHighlights()
{
    assertInitialized();

    // Draw 3D UI elements here (before we clear the Z buffer in POOL_HUD)
    // Render highlighted faces.
    LLGLSPipelineAlpha gls_pipeline_alpha;
    disableLights();

    if (hasRenderDebugFeatureMask(RENDER_DEBUG_FEATURE_SELECTED))
    {
        bindHighlightProgram(gHighlightProgram);

        if (sRenderHighlightTextureChannel == LLRender::DIFFUSE_MAP ||
            sRenderHighlightTextureChannel == LLRender::BASECOLOR_MAP ||
            sRenderHighlightTextureChannel == LLRender::METALLIC_ROUGHNESS_MAP ||
            sRenderHighlightTextureChannel == LLRender::GLTF_NORMAL_MAP ||
            sRenderHighlightTextureChannel == LLRender::EMISSIVE_MAP ||
            sRenderHighlightTextureChannel == LLRender::NUM_TEXTURE_CHANNELS)
        {
            static const LLColor4 highlight_selected_color(1.f, 1.f, 1.f, 0.5f);
            renderSelectedFaces(highlight_selected_color);
        }

        // Paint 'em red!
        static const LLColor4 highlight_face_color(1.f, 0.f, 0.f, 0.5f);
        for (auto facep : mHighlightFaces)
        {
            facep->renderSelected(LLViewerTexture::sNullImagep, highlight_face_color);
        }

        unbindHighlightProgram(gHighlightProgram);
    }

    // Contains a list of the faces of objects that are physical or
    // have touch-handlers.
    mHighlightFaces.clear();

    if (hasRenderDebugFeatureMask(RENDER_DEBUG_FEATURE_SELECTED))
    {
        if (sRenderHighlightTextureChannel == LLRender::NORMAL_MAP)
        {
            static const LLColor4 highlight_normal_color(1.0f, 0.5f, 0.5f, 0.5f);
            bindHighlightProgram(gHighlightNormalProgram);
            renderSelectedFaces(highlight_normal_color);
            unbindHighlightProgram(gHighlightNormalProgram);
        }
        else if (sRenderHighlightTextureChannel == LLRender::SPECULAR_MAP)
        {
            static const LLColor4 highlight_specular_color(0.0f, 0.3f, 1.0f, 0.8f);
            bindHighlightProgram(gHighlightSpecularProgram);
            renderSelectedFaces(highlight_specular_color);
            unbindHighlightProgram(gHighlightSpecularProgram);
        }
    }
}

//debug use
U32 LLPipeline::sCurRenderPoolType = 0 ;

void LLPipeline::renderGeomDeferred(LLCamera& camera, bool do_occlusion)
{
    LLAppViewer::instance()->pingMainloopTimeout("Pipeline:RenderGeomDeferred");
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LL_PROFILE_GPU_ZONE("renderGeomDeferred");

    llassert(!sRenderingHUDs);

    if (gUseWireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    if (&camera == LLViewerCamera::getInstance())
    {   // the start of the main render frame: from the last frame's camera
        // space to this one's
        LLViewerCamera::getInstance()->calcDeltaModelview();

        // Probe and mirror captures move the camera singleton and come through here too; the frame starts with the
        // main view. The water's field was laid out as the frame began (display).
        if (!gCubeSnapshot)
        {
            mTerrainPrimitives.plot();
            mWaterPrimitives.plot();
            mWaterPatchPrimitives.plot();
            LL_PROFILE_PLOT("water draws", (int64_t)mWaterDraws);
            mWaterDraws = 0;
            // The waves this frame: their patches before the cull, and A in centimetres.
            LL_PROFILE_PLOT("water patches", (int64_t)mWaterSurface.patchCount());
            LL_PROFILE_PLOT("water waves cm", (int64_t)(mWaterSurface.maxDisplacement() * 100.f));
            LL_PROFILE_PLOT("render target MB", (int64_t)(LLRenderTarget::sBytesAllocated >> 20));
            // The pool's share of those, and how the last frame's acquisitions found it.
            LL_PROFILE_PLOT("transient target MB", (int64_t)(mTransientTargets.lastFrame().mBytes >> 20));
            LL_PROFILE_PLOT("transient target hits", (int64_t)mTransientTargets.lastFrame().mHits);
            LL_PROFILE_PLOT("transient target misses", (int64_t)mTransientTargets.lastFrame().mMisses);
        }
    }

    // doOcclusion() stands down itself while a shader profile holds the occlusion targets.
    bool occlude = LLPipeline::sUseOcclusion > 1 && do_occlusion;

    setupHWLights();

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("deferred pools");

        LLGLEnable cull(GL_CULL_FACE);

        // The G-buffer writers shade in linear (colour textures decode on the sampler) and
        // rely on this to re-encode into the sRGB albedo attachment; it is inert on the
        // other attachments and on depth-only targets. Enabled ONCE here rather than per
        // pool -- each real FRAMEBUFFER_SRGB toggle costs a gGL.flush() plus a GL state
        // change, and the per-pool scopes toggled it off and on again at every pool
        // boundary. The writers that still store display-encoded values raw opt OUT
        // locally: the WL sky family (LLDrawPoolWLSky::renderDeferred) and the avatar pool's
        // impostor/rigid passes (LLDrawPoolAvatar::renderDeferred).
        LLGLEnable srgb(GL_FRAMEBUFFER_SRGB);

        for (pool_set_t::iterator iter = mPools.begin(); iter != mPools.end(); ++iter)
        {
            LLDrawPool *poolp = *iter;
            if (hasRenderType(poolp->getType()))
            {
                poolp->prerender();
            }
        }

        LLVertexBuffer::unbind();

        LLGLState::checkStates();

        if (LLViewerShaderMgr::instance()->mShaderLevel[LLViewerShaderMgr::SHADER_DEFERRED] > 1)
        {
            //update reflection probe uniform
            mReflectionMapManager.updateUniforms();
            mHeroProbeManager.updateUniforms();

            // Upload (if changed) and bind the shared sky/water constant block (UB_ENVIRONMENT)
            // for this pass, same cadence as the reflection-probe UBO.
            LLEnvironment::instance().bindEnvironmentUBO();
        }

        U32 cur_type = 0;

        gGL.setColorMask(true, true);

        pool_set_t::iterator iter1 = mPools.begin();

        while ( iter1 != mPools.end() )
        {
            LLDrawPool *poolp = *iter1;

            cur_type = poolp->getType();

            if (occlude && cur_type >= LLDrawPool::POOL_GRASS)
            {
                llassert(!gCubeSnapshot); // never do occlusion culling on cube snapshots
                occlude = false;
                gGLLastMatrix = NULL;
                gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
                doOcclusion(camera);
            }

            pool_set_t::iterator iter2 = iter1;
            if (hasRenderType(poolp->getType()) && poolp->getNumDeferredPasses() > 0)
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("deferred pool render");

                gGLLastMatrix = NULL;
                gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());

                for( S32 i = 0; i < poolp->getNumDeferredPasses(); i++ )
                {
                    LLVertexBuffer::unbind();
                    poolp->beginDeferredPass(i);
                    for (iter2 = iter1; iter2 != mPools.end(); iter2++)
                    {
                        LLDrawPool *p = *iter2;
                        if (p->getType() != cur_type)
                        {
                            break;
                        }

                        if ( !p->getSkipRenderFlag() ) { p->renderDeferred(i); }
                    }
                    poolp->endDeferredPass(i);
                    LLVertexBuffer::unbind();

                    LLGLState::checkStates();
                }
            }
            else
            {
                // Skip all pools of this type
                for (iter2 = iter1; iter2 != mPools.end(); iter2++)
                {
                    LLDrawPool *p = *iter2;
                    if (p->getType() != cur_type)
                    {
                        break;
                    }
                }
            }
            iter1 = iter2;
            stop_glerror();
        }

        gGLLastMatrix = NULL;
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());

        gGL.setColorMask(true, false);

    } // Tracy ZoneScoped

    if (gUseWireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

// Render all of our geometry that's required after our deferred pass.
// This is gonna be stuff like alpha, water, etc.
void LLPipeline::renderGeomPostDeferred(LLCamera& camera)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LL_PROFILE_GPU_ZONE("renderGeomPostDeferred");

    U32 cur_type = 0;

    LLGLEnable cull(GL_CULL_FACE);

    bool done_atmospherics = LLPipeline::sRenderingHUDs; //skip atmospherics on huds

    // The water haze and the exclusion serve the water this view draws: from above, the haze fogs what lies under the
    // surface and the exclusion cuts the surface, so neither runs where none is drawn -- the Water render type off, or
    // the view missing the sea (LLDrawPoolWater::getNumPostDeferredPasses). From below, the haze fills the screen.
    const bool water_drawn = mWaterPool && hasRenderType(LLPipeline::RENDER_TYPE_WATER) && mWaterPool->getNumPostDeferredPasses() > 0;
    bool done_water_haze = done_atmospherics || (!sUnderWaterRender && !water_drawn);
    // and the water exclusion mask: the water pool and water haze, which read it, do not run
    // on huds, and every pass that runs them builds the mask first
    bool done_water_exclusion = LLPipeline::sRenderingHUDs || !water_drawn;

    // do water exclusion just before water pass.
    U32 water_exclusion_pass = LLDrawPool::POOL_WATEREXCLUSION;

    // do atmospheric haze just before post water alpha
    U32 atmospherics_pass = LLDrawPool::POOL_ALPHA_POST_WATER;

    if (LLPipeline::sUnderWaterRender)
    { // if under water, do atmospherics just before the water pass
        atmospherics_pass = LLDrawPool::POOL_WATER;
    }

    // do water haze just before pre water alpha
    U32 water_haze_pass = LLDrawPool::POOL_ALPHA_PRE_WATER;

    // A cutout's coverage is seeded once every opaque pool has laid down its
    // depth -- fullbright, shiny and alpha-masked fullbright draw here, after
    // lighting, and would otherwise be left uncovered -- and before the first
    // blended pool adds to it.
    U32 cutout_seed_pass = LLDrawPool::POOL_ALPHA_PRE_WATER;
    bool done_cutout_seed = !cullingToCutoutSubject();

    calcNearbyLights(camera);
    setupHWLights();

    gGL.setSceneBlendType(LLRender::BT_ALPHA);
    gGL.setColorMask(true, false);

    pool_set_t::iterator iter1 = mPools.begin();

    if (gDebugGL || gDebugPipeline)
    {
        LLGLState::checkStates(GL_FALSE);
    }

    // turn off atmospherics and water haze for low detail reflection probe
    bool low_detail_probe = shaderInputs().mGlobal.mProbeLevel == 0 && gCubeSnapshot;
    done_atmospherics = done_atmospherics || low_detail_probe;
    done_water_haze   = done_water_haze || low_detail_probe;

    // Underwater, the water haze is a full-screen pass, and it would fog the
    // empty background of a cutout frame -- which then bleeds into whatever
    // blended geometry is composited over it. Above water it has nothing to
    // draw there, the water being left out.
    done_water_haze = done_water_haze || cullingToCutoutSubject();


    while ( iter1 != mPools.end() )
    {
        LLDrawPool *poolp = *iter1;

        cur_type = poolp->getType();

        if (cur_type >= water_exclusion_pass && !done_water_exclusion)
        { // do water exclusion against depth buffer before rendering alpha
            doWaterExclusion();
            done_water_exclusion = true;
        }

        if (cur_type >= atmospherics_pass && !done_atmospherics)
        { // do atmospherics against depth buffer before rendering alpha
            doAtmospherics();
            done_atmospherics = true;
        }

        if (cur_type >= cutout_seed_pass && !done_cutout_seed)
        {
            seedCutoutCoverageFromDepth();
            done_cutout_seed = true;
        }

        if (cur_type >= water_haze_pass && !done_water_haze)
        { // do water haze against depth buffer before rendering alpha
            doWaterHaze();
            done_water_haze = true;
        }

        pool_set_t::iterator iter2 = iter1;
        if (hasRenderType(poolp->getType()) && poolp->getNumPostDeferredPasses() > 0)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("deferred poolrender");

            // Wireframe is for the pools' geometry alone. The passes run between pools above
            // are full-screen triangles, which in line mode shade only their edges, and the
            // water exclusion would stencil only the outlines of its prims.
            if (gUseWireframe)
            {
                gGL.flush();
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            }

            gGLLastMatrix = NULL;
            gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());

            for( S32 i = 0; i < poolp->getNumPostDeferredPasses(); i++ )
            {
                LLVertexBuffer::unbind();
                poolp->beginPostDeferredPass(i);
                for (iter2 = iter1; iter2 != mPools.end(); iter2++)
                {
                    LLDrawPool *p = *iter2;
                    if (p->getType() != cur_type)
                    {
                        break;
                    }

                    p->renderPostDeferred(i);
                }
                poolp->endPostDeferredPass(i);
                LLVertexBuffer::unbind();

                if (gDebugGL || gDebugPipeline)
                {
                    LLGLState::checkStates(GL_FALSE);
                }
            }

            if (gUseWireframe)
            {
                gGL.flush();
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
        else
        {
            // Skip all pools of this type
            for (iter2 = iter1; iter2 != mPools.end(); iter2++)
            {
                LLDrawPool *p = *iter2;
                if (p->getType() != cur_type)
                {
                    break;
                }
            }
        }
        iter1 = iter2;
        stop_glerror();
    }

    if (!done_cutout_seed)
    {
        seedCutoutCoverageFromDepth();
    }

    gGLLastMatrix = NULL;
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());

    // An impostor is a billboard: baking selection highlights or debug geometry into its
    // texture freezes them there for the life of the impostor and replays them into the scene
    // G-buffer, which is not what either is for. They belong to the frame that draws the
    // billboard, where they are already drawn. Nor are they part of a cutout's subject.
    if (!gCubeSnapshot && !sImpostorRender && !sCutoutRender)
    {
        // debug displays
        renderHighlights();
        mHighlightFaces.clear();

        renderDebug();
        if (!hasRenderType(LLPipeline::RENDER_TYPE_HUD))
        {
            ALFarPlaneOverlay::render();
        }
    }
}

void LLPipeline::renderGeomShadow(LLCamera& camera)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("renderGeomShadow");
    U32 cur_type = 0;

    LLGLEnable cull(GL_CULL_FACE);

    LLVertexBuffer::unbind();

    pool_set_t::iterator iter1 = mPools.begin();

    while ( iter1 != mPools.end() )
    {
        LLDrawPool *poolp = *iter1;

        cur_type = poolp->getType();

        pool_set_t::iterator iter2 = iter1;
        if (hasRenderType(poolp->getType()) && poolp->getNumShadowPasses() > 0)
        {
            poolp->prerender() ;

            gGLLastMatrix = NULL;
            gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());

            for( S32 i = 0; i < poolp->getNumShadowPasses(); i++ )
            {
                LLVertexBuffer::unbind();
                poolp->beginShadowPass(i);
                for (iter2 = iter1; iter2 != mPools.end(); iter2++)
                {
                    LLDrawPool *p = *iter2;
                    if (p->getType() != cur_type)
                    {
                        break;
                    }

                    p->renderShadow(i);
                }
                poolp->endShadowPass(i);
                LLVertexBuffer::unbind();
            }
        }
        else
        {
            // Skip all pools of this type
            for (iter2 = iter1; iter2 != mPools.end(); iter2++)
            {
                LLDrawPool *p = *iter2;
                if (p->getType() != cur_type)
                {
                    break;
                }
            }
        }
        iter1 = iter2;
        stop_glerror();
    }

    gGLLastMatrix = NULL;
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
}


static U32 sIndicesDrawnCount = 0;

void LLPipeline::addTrianglesDrawn(S32 index_count)
{
    sIndicesDrawnCount += index_count;
}

void LLPipeline::recordTrianglesDrawn()
{
    assertInitialized();
    U32 count = sIndicesDrawnCount / 3;
    sIndicesDrawnCount = 0;
    add(LLStatViewer::TRIANGLES_DRAWN, LLUnits::Triangles::fromValue(count));
}

void LLPipeline::renderPhysicsDisplay()
{
    if (!hasRenderDebugMask(LLPipeline::RENDER_DEBUG_PHYSICS_SHAPES))
    {
        return;
    }

    gGL.flush();
    gDebugProgram.bind();

    // The passes below set their own masks; this hands the caller's back.
    LLGLSColorMask color_mask(true, false);

    // Toward the camera: pass 0 lays the shapes' own depth down unoffset, and an outline
    // pushed behind it fails the depth test everywhere the fill covers. setPolygonOffset
    // flips the sign under reverse-Z itself.
    LLGLEnable polygon_offset_line(GL_POLYGON_OFFSET_LINE);
    gGL.setPolygonOffset(-3.f, -3.f);
    gGL.setLineWidth(3.f);
    LLGLEnable blend(GL_BLEND);
    gGL.setSceneBlendType(LLRender::BT_ALPHA);

    for (int pass = 0; pass < 3; ++pass)
    {
        // pass 0 - depth write enabled, color write disabled, fill
        // pass 1 - depth write disabled, color write enabled, fill
        // pass 2 - depth write disabled, color write enabled, wireframe
        gGL.setColorMask(pass >= 1, false);
        LLGLDepthTest depth(GL_TRUE, pass == 0);

        bool wireframe = (pass == 2);

        if (wireframe)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }

        for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
        {
            LLViewerRegion* region = *iter;
            for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
            {
                LLSpatialPartition* part = region->getSpatialPartition(i);
                if (part)
                {
                    if (hasRenderType(part->mDrawableType))
                    {
                        part->renderPhysicsShapes(wireframe);
                    }
                }
            }
        }
        gGL.flush();

        if (wireframe)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }
    gGL.setLineWidth(1.f);
    gDebugProgram.unbind();

}

extern std::set<LLSpatialGroup*> visible_selected_groups;

void LLPipeline::renderDebug()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    assertInitialized();

    bool hud_only = hasRenderType(LLPipeline::RENDER_TYPE_HUD);

    if (!hud_only )
    {
        //Render any navmesh geometry
        LLPathingLib *llPathingLibInstance = LLPathingLib::getInstance();
        if ( llPathingLibInstance != NULL )
        {
            //character floater renderables

            LLHandle<LLFloaterPathfindingCharacters> pathfindingCharacterHandle = LLFloaterPathfindingCharacters::getInstanceHandle();
            if ( !pathfindingCharacterHandle.isDead() )
            {
                LLFloaterPathfindingCharacters *pathfindingCharacter = pathfindingCharacterHandle.get();

                if ( pathfindingCharacter->getVisible() || gAgentCamera.cameraMouselook() )
                {
                    gPathfindingProgram.bind();
                    gPathfindingProgram.uniform1f(LLShaderMgr::TINT, 1.f);
                    gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, 1.f);
                    gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 1.f);

                    //Requried character physics capsule render parameters
                    LLUUID id;
                    LLVector3 pos;
                    LLQuaternion rot;

                    if ( pathfindingCharacter->isPhysicsCapsuleEnabled( id, pos, rot ) )
                    {
                        //remove blending artifacts
                        gGL.setColorMask(false, false);
                        llPathingLibInstance->renderSimpleShapeCapsuleID( gGL, id, pos, rot );
                        gGL.setColorMask(true, false);
                        LLGLEnable blend(GL_BLEND);
                        gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 0.90f);
                        llPathingLibInstance->renderSimpleShapeCapsuleID( gGL, id, pos, rot );
                        gPathfindingProgram.bind();
                    }
                }
            }


            //pathing console renderables
            LLHandle<LLFloaterPathfindingConsole> pathfindingConsoleHandle = LLFloaterPathfindingConsole::getInstanceHandle();
            if (!pathfindingConsoleHandle.isDead())
            {
                LLFloaterPathfindingConsole *pathfindingConsole = pathfindingConsoleHandle.get();

                if ( pathfindingConsole->getVisible() || gAgentCamera.cameraMouselook() )
                {
                    F32 ambiance = gSavedSettings.getF32("PathfindingAmbiance");

                    gPathfindingProgram.bind();

                    gPathfindingProgram.uniform1f(LLShaderMgr::TINT, 1.f);
                    gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, ambiance);
                    gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 1.f);

                    if ( !pathfindingConsole->isRenderWorld() )
                    {
                        const LLColor4 clearColor = gSavedSettings.getColor4("PathfindingNavMeshClear");
                        gGL.setColorMask(true, true);
                        gGL.setClearColor(LLColor4(clearColor.mV[0], clearColor.mV[1], clearColor.mV[2], 0.f));
                        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT); // no stencil -- deprecated | GL_STENCIL_BUFFER_BIT);
                        gGL.setColorMask(true, false);
                        glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                    }

                    //NavMesh
                    if ( pathfindingConsole->isRenderNavMesh() )
                    {
                        gGL.setLineWidth(2.0f);
                        LLGLEnable cull(GL_CULL_FACE);
                        LLGLDisable blend(GL_BLEND);

                        if ( pathfindingConsole->isRenderWorld() )
                        {
                            LLGLEnable blend(GL_BLEND);
                            gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 0.66f);
                            llPathingLibInstance->renderNavMesh();
                        }
                        else
                        {
                            llPathingLibInstance->renderNavMesh();
                        }

                        //render edges
                        gPathfindingNoNormalsProgram.bind();
                        gPathfindingNoNormalsProgram.uniform1f(LLShaderMgr::TINT, 1.f);
                        gPathfindingNoNormalsProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 1.f);
                        llPathingLibInstance->renderNavMeshEdges();
                        gPathfindingProgram.bind();

                        gGL.flush();
                        glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                        gGL.setLineWidth(1.0f);
                        gGL.flush();
                    }
                    //User designated path
                    if ( LLPathfindingPathTool::getInstance()->isRenderPath() )
                    {
                        //The path
                        gUIProgram.bind();
                        gGL.getTextureSlot(0)->bindSampled(LLViewerFetchedTexture::sWhiteImagep, ALSamplers::AnisoWrap);
                        llPathingLibInstance->renderPath();
                        gPathfindingProgram.bind();

                        //The bookends
                        //remove blending artifacts
                        gGL.setColorMask(false, false);
                        llPathingLibInstance->renderPathBookend( gGL, LLPathingLib::LLPL_START );
                        llPathingLibInstance->renderPathBookend( gGL, LLPathingLib::LLPL_END );

                        gGL.setColorMask(true, false);
                        //render the bookends
                        LLGLEnable blend(GL_BLEND);
                        gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 0.90f);
                        llPathingLibInstance->renderPathBookend( gGL, LLPathingLib::LLPL_START );
                        llPathingLibInstance->renderPathBookend( gGL, LLPathingLib::LLPL_END );
                        gPathfindingProgram.bind();
                    }

                    if ( pathfindingConsole->isRenderWaterPlane() )
                    {
                        LLGLEnable blend(GL_BLEND);
                        gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 0.90f);
                        llPathingLibInstance->renderSimpleShapes( gGL, gAgent.getRegion()->getWaterHeight() );
                    }
                //physics/exclusion shapes
                if ( pathfindingConsole->isRenderAnyShapes() )
                {
                        U32 render_order[] = {
                            1 << LLPathingLib::LLST_ObstacleObjects,
                            1 << LLPathingLib::LLST_WalkableObjects,
                            1 << LLPathingLib::LLST_ExclusionPhantoms,
                            1 << LLPathingLib::LLST_MaterialPhantoms,
                        };

                        U32 flags = pathfindingConsole->getRenderShapeFlags();

                        for (U32 i = 0; i < 4; i++)
                        {
                            if (!(flags & render_order[i]))
                            {
                                continue;
                            }

                            //turn off backface culling for volumes so they are visible when camera is inside volume
                            LLGLDisable cull(i >= 2 ? GL_CULL_FACE : 0);

                            gGL.flush();
                            glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

                            //get rid of some z-fighting
                            LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
                            gGL.setPolygonOffset(1.0f, 1.0f);

                            //render to depth first to avoid blending artifacts
                            gGL.setColorMask(false, false);
                            llPathingLibInstance->renderNavMeshShapesVBO( render_order[i] );
                            gGL.setColorMask(true, false);

                            //get rid of some z-fighting
                            gGL.setPolygonOffset(0.f, 0.f);

                            LLGLEnable blend(GL_BLEND);

                            {
                                gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, ambiance);

                                { //draw solid overlay
                                    LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_LEQUAL);
                                    llPathingLibInstance->renderNavMeshShapesVBO( render_order[i] );
                                    gGL.flush();
                                }

                                LLGLEnable lineOffset(GL_POLYGON_OFFSET_LINE);
                                glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

                                F32 offset = gSavedSettings.getF32("PathfindingLineOffset");

                                if (pathfindingConsole->isRenderXRay())
                                {
                                    gPathfindingProgram.uniform1f(LLShaderMgr::TINT, gSavedSettings.getF32("PathfindingXRayTint"));
                                    gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, gSavedSettings.getF32("PathfindingXRayOpacity"));
                                    LLGLEnable blend(GL_BLEND);
                                    LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_GREATER);

                                    gGL.setPolygonOffset(offset, -offset);

                                    if (gSavedSettings.getBOOL("PathfindingXRayWireframe"))
                                    { //draw hidden wireframe as darker and less opaque
                                        gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, 1.f);
                                        llPathingLibInstance->renderNavMeshShapesVBO( render_order[i] );
                                    }
                                    else
                                    {
                                        glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                                        gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, ambiance);
                                        llPathingLibInstance->renderNavMeshShapesVBO( render_order[i] );
                                        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                                    }
                                }

                                { //draw visible wireframe as brighter, thicker and more opaque
                                    gGL.setPolygonOffset(offset, offset);
                                    gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, 1.f);
                                    gPathfindingProgram.uniform1f(LLShaderMgr::TINT, 1.f);
                                    gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, 1.f);

                                    gGL.setLineWidth(gSavedSettings.getF32("PathfindingLineWidth"));
                                    LLGLDisable blendOut(GL_BLEND);
                                    llPathingLibInstance->renderNavMeshShapesVBO( render_order[i] );
                                    gGL.flush();
                                    gGL.setLineWidth(1.f);
                                }

                                glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                            }
                        }
                    }

                    gGL.setPolygonOffset(0.f, 0.f);

                    if ( pathfindingConsole->isRenderNavMesh() && pathfindingConsole->isRenderXRay() )
                    {   //render navmesh xray
                        F32 ambiance = gSavedSettings.getF32("PathfindingAmbiance");

                        LLGLEnable lineOffset(GL_POLYGON_OFFSET_LINE);
                        LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);

                        F32 offset = gSavedSettings.getF32("PathfindingLineOffset");
                        gGL.setPolygonOffset(offset, -offset);

                        LLGLEnable blend(GL_BLEND);
                        LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_GREATER);
                        gGL.flush();
                        gGL.setLineWidth(2.0f);
                        LLGLEnable cull(GL_CULL_FACE);

                        gPathfindingProgram.uniform1f(LLShaderMgr::TINT, gSavedSettings.getF32("PathfindingXRayTint"));
                        gPathfindingProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, gSavedSettings.getF32("PathfindingXRayOpacity"));

                        if (gSavedSettings.getBOOL("PathfindingXRayWireframe"))
                        { //draw hidden wireframe as darker and less opaque
                            glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
                            gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, 1.f);
                            llPathingLibInstance->renderNavMesh();
                            glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                        }
                        else
                        {
                            gPathfindingProgram.uniform1f(LLShaderMgr::AMBIANCE, ambiance);
                            llPathingLibInstance->renderNavMesh();
                        }

                        //render edges
                        gPathfindingNoNormalsProgram.bind();
                        gPathfindingNoNormalsProgram.uniform1f(LLShaderMgr::TINT, gSavedSettings.getF32("PathfindingXRayTint"));
                        gPathfindingNoNormalsProgram.uniform1f(LLShaderMgr::ALPHA_SCALE, gSavedSettings.getF32("PathfindingXRayOpacity"));
                        llPathingLibInstance->renderNavMeshEdges();
                        gPathfindingProgram.bind();

                        gGL.flush();
                        gGL.setLineWidth(1.0f);
                    }

                    gGL.setPolygonOffset(0.f, 0.f);

                    gGL.flush();
                    gPathfindingProgram.unbind();
                }
            }
        }
    }

    gGLLastMatrix = NULL;
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGL.setColorMask(true, false);


    if (!hud_only && !mDebugBlips.empty())
    { //render debug blips
        gUIProgram.bind();
        gGL.color4f(1, 1, 1, 1);

        gGL.getTextureSlot(0)->bindSampled(LLViewerFetchedTexture::sWhiteImagep, ALSamplers::AnisoWrap, true);

        glPointSize(8.f);
        LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);

        gGL.begin(LLRender::POINTS);
        for (std::list<DebugBlip>::iterator iter = mDebugBlips.begin(); iter != mDebugBlips.end(); )
        {
            DebugBlip& blip = *iter;

            blip.mAge += gFrameIntervalSeconds.value();
            if (blip.mAge > 2.f)
            {
                mDebugBlips.erase(iter++);
            }
            else
            {
                iter++;
            }

            blip.mPosition.mV[2] += gFrameIntervalSeconds.value()*2.f;

            gGL.color4fv(blip.mColor.mV);
            gGL.vertex3fv(blip.mPosition.mV);
        }
        gGL.end();
        gGL.flush();
        glPointSize(1.f);
    }

    // Debug stuff.
    if (gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_OCTREE |
        LLPipeline::RENDER_DEBUG_OCCLUSION |
        LLPipeline::RENDER_DEBUG_LIGHTS |
        LLPipeline::RENDER_DEBUG_BATCH_SIZE |
        LLPipeline::RENDER_DEBUG_UPDATE_TYPE |
        LLPipeline::RENDER_DEBUG_BBOXES |
        LLPipeline::RENDER_DEBUG_NORMALS |
        LLPipeline::RENDER_DEBUG_POINTS |
        LLPipeline::RENDER_DEBUG_TEXTURE_AREA |
        LLPipeline::RENDER_DEBUG_TEXTURE_ANIM |
        LLPipeline::RENDER_DEBUG_RAYCAST |
        LLPipeline::RENDER_DEBUG_AVATAR_VOLUME |
        LLPipeline::RENDER_DEBUG_AVATAR_JOINTS |
        LLPipeline::RENDER_DEBUG_AGENT_TARGET |
        LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA |
        LLPipeline::RENDER_DEBUG_TEXEL_DENSITY))
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("render debug bridges");

        for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
        {
            for (U32 i = 0; i < LLViewerRegion::NUM_PARTITIONS; i++)
            {
                LLSpatialPartition* part = region->getSpatialPartition(i);
                if (part)
                {
                    if ((hud_only && (part->mDrawableType == RENDER_TYPE_HUD || part->mDrawableType == RENDER_TYPE_HUD_PARTICLES)) ||
                        (!hud_only && hasRenderType(part->mDrawableType)))
                    {
                        part->renderDebug();
                    }
                }
            }
        }

        for (LLCullResult::bridge_iterator i = sCull->beginVisibleBridge(); i != sCull->endVisibleBridge(); ++i)
        {
            LLSpatialBridge* bridge = *i;
            if (!bridge->isDead() && hasRenderType(bridge->mDrawableType))
            {
                gGL.pushMatrix();
                gGL.multMatrix(bridge->mDrawable->getRenderMatrix().getF32ptr());
                bridge->renderDebug();
                gGL.popMatrix();
            }
        }
    }

    if (gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_OCCLUSION))
    { //render visible selected group occlusion geometry
        gDebugProgram.bind();
        LLGLDepthTest depth(GL_TRUE, GL_FALSE);
        gGL.diffuseColor3f(1,0,1);
        for (std::set<LLSpatialGroup*>::iterator iter = visible_selected_groups.begin(); iter != visible_selected_groups.end(); ++iter)
        {
            LLSpatialGroup* group = *iter;

            LLVector4a fudge;
            fudge.splat(0.25f); //SG_OCCLUSION_FUDGE

            LLVector4a size;
            const LLVector4a* bounds = group->getBounds();
            size.setAdd(fudge, bounds[1]);

            drawBox(bounds[0], size);
        }
    }

    visible_selected_groups.clear();

    //draw reflection probes and links between them
    if (gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_REFLECTION_PROBES) && !hud_only)
    {
        mReflectionMapManager.renderDebug();
    }

    static LLCachedControl<bool> render_ref_probe_volumes(gSavedSettings, "RenderReflectionProbeVolumes");
    if (render_ref_probe_volumes && !hud_only)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("probe debug display");

        bindDeferredShader(gReflectionProbeDisplayProgram, NULL);
        mScreenTriangleVB->setBuffer();

        LLGLEnable blend(GL_BLEND);
        LLGLDepthTest depth(GL_FALSE);

        mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        unbindDeferredShader(gReflectionProbeDisplayProgram);
    }

    gUIProgram.bind();

    if (hasRenderDebugMask(LLPipeline::RENDER_DEBUG_RAYCAST) && !hud_only)
    { //draw crosshairs on particle intersection
        if (gDebugRaycastParticle)
        {
            gDebugProgram.bind();

            gGL.getTextureSlot(0)->unbind();

            LLVector3 center(gDebugRaycastParticleIntersection.getF32ptr());
            LLVector3 size(0.1f, 0.1f, 0.1f);

            LLVector3 p[6];

            p[0] = center + size.scaledVec(LLVector3(1,0,0));
            p[1] = center + size.scaledVec(LLVector3(-1,0,0));
            p[2] = center + size.scaledVec(LLVector3(0,1,0));
            p[3] = center + size.scaledVec(LLVector3(0,-1,0));
            p[4] = center + size.scaledVec(LLVector3(0,0,1));
            p[5] = center + size.scaledVec(LLVector3(0,0,-1));

            gGL.begin(LLRender::LINES);
            gGL.diffuseColor3f(1.f, 1.f, 0.f);
            for (U32 i = 0; i < 6; i++)
            {
                gGL.vertex3fv(p[i].mV);
            }
            gGL.end();
            gGL.flush();

            gDebugProgram.unbind();
        }
    }

    if (hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !hud_only)
    {
        LLVertexBuffer::unbind();

        LLGLEnable blend(GL_BLEND);
        LLGLDepthTest depth(true, false);
        LLGLDisable cull(GL_CULL_FACE);

        gGL.color4f(1,1,1,1);
        gGL.getTextureSlot(0)->unbind();

        F32 a = 0.1f;

        F32 col[] =
        {
            1,0,0,a,
            0,1,0,a,
            0,0,1,a,
            1,0,1,a,

            1,1,0,a,
            0,1,1,a,
            1,1,1,a,
            1,0,1,a,
        };

        for (U32 i = 0; i < 8; i++)
        {
            LLVector3* frust = mShadowCamera[i].mAgentFrustum;

            if (i > 3)
            { //render shadow frusta as volumes
                if (mShadowFrustPoints[i-4].empty())
                {
                    continue;
                }

                gGL.color4fv(col+(i-4)*4);

                gGL.begin(LLRender::TRIANGLE_STRIP);
                gGL.vertex3fv(frust[0].mV); gGL.vertex3fv(frust[4].mV);
                gGL.vertex3fv(frust[1].mV); gGL.vertex3fv(frust[5].mV);
                gGL.vertex3fv(frust[2].mV); gGL.vertex3fv(frust[6].mV);
                gGL.vertex3fv(frust[3].mV); gGL.vertex3fv(frust[7].mV);
                gGL.vertex3fv(frust[0].mV); gGL.vertex3fv(frust[4].mV);
                gGL.end();


                gGL.begin(LLRender::TRIANGLE_STRIP);
                gGL.vertex3fv(frust[0].mV);
                gGL.vertex3fv(frust[1].mV);
                gGL.vertex3fv(frust[3].mV);
                gGL.vertex3fv(frust[2].mV);
                gGL.end();

                gGL.begin(LLRender::TRIANGLE_STRIP);
                gGL.vertex3fv(frust[4].mV);
                gGL.vertex3fv(frust[5].mV);
                gGL.vertex3fv(frust[7].mV);
                gGL.vertex3fv(frust[6].mV);
                gGL.end();
            }


            if (i < 4)
            {

                //if (i == 0 || !mShadowFrustPoints[i].empty())
                {
                    //render visible point cloud
                    gGL.flush();
                    glPointSize(8.f);
                    gGL.begin(LLRender::POINTS);

                    F32* c = col+i*4;
                    gGL.color3fv(c);

                    for (U32 j = 0; j < mShadowFrustPoints[i].size(); ++j)
                        {
                            gGL.vertex3fv(mShadowFrustPoints[i][j].mV);

                        }
                    gGL.end();

                    gGL.flush();
                    glPointSize(1.f);

                    LLVector3* ext = mShadowExtents[i];
                    LLVector3 pos = (ext[0]+ext[1])*0.5f;
                    LLVector3 size = (ext[1]-ext[0])*0.5f;
                    drawBoxOutline(pos, size);

                    //render camera frustum splits as outlines
                    gGL.begin(LLRender::LINES);
                    gGL.vertex3fv(frust[0].mV); gGL.vertex3fv(frust[1].mV);
                    gGL.vertex3fv(frust[1].mV); gGL.vertex3fv(frust[2].mV);
                    gGL.vertex3fv(frust[2].mV); gGL.vertex3fv(frust[3].mV);
                    gGL.vertex3fv(frust[3].mV); gGL.vertex3fv(frust[0].mV);
                    gGL.vertex3fv(frust[4].mV); gGL.vertex3fv(frust[5].mV);
                    gGL.vertex3fv(frust[5].mV); gGL.vertex3fv(frust[6].mV);
                    gGL.vertex3fv(frust[6].mV); gGL.vertex3fv(frust[7].mV);
                    gGL.vertex3fv(frust[7].mV); gGL.vertex3fv(frust[4].mV);
                    gGL.vertex3fv(frust[0].mV); gGL.vertex3fv(frust[4].mV);
                    gGL.vertex3fv(frust[1].mV); gGL.vertex3fv(frust[5].mV);
                    gGL.vertex3fv(frust[2].mV); gGL.vertex3fv(frust[6].mV);
                    gGL.vertex3fv(frust[3].mV); gGL.vertex3fv(frust[7].mV);
                    gGL.end();
                }
            }

            /*gGL.flush();
             gGL.setLineWidth(16-i*2);
            for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
                    iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
            {
                LLViewerRegion* region = *iter;
                for (U32 j = 0; j < LLViewerRegion::NUM_PARTITIONS; j++)
                {
                    LLSpatialPartition* part = region->getSpatialPartition(j);
                    if (part)
                    {
                        if (hasRenderType(part->mDrawableType))
                        {
                            part->renderIntersectingBBoxes(&mShadowCamera[i]);
                        }
                    }
                }
            }
            gGL.flush();
             gGL.setLineWidth(1.f);*/
        }
    }

    if (mRenderDebugMask & RENDER_DEBUG_WIND_VECTORS)
    {
        gAgent.getRegion()->mWind.renderVectors();
    }

    if (mRenderDebugMask & RENDER_DEBUG_COMPOSITION)
    {
        // Debug composition layers
        F32 x, y;

        gGL.getTextureSlot(0)->unbind();

        if (gAgent.getRegion())
        {
            gGL.begin(LLRender::POINTS);
            // Draw the composition layer for the region that I'm in.
            for (x = 0; x <= 260; x++)
            {
                for (y = 0; y <= 260; y++)
                {
                    if ((x > 255) || (y > 255))
                    {
                        gGL.color4f(1.f, 0.f, 0.f, 1.f);
                    }
                    else
                    {
                        gGL.color4f(0.f, 0.f, 1.f, 1.f);
                    }
                    F32 z = gAgent.getRegion()->getCompositionXY((S32)x, (S32)y);
                    z *= 5.f;
                    z += 50.f;
                    gGL.vertex3f(x, y, z);
                }
            }
            gGL.end();
        }
    }

    gGL.flush();
    gUIProgram.unbind();
}

void LLPipeline::rebuildPools()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    assertInitialized();

    auto max_count = mPools.size();
    pool_set_t::iterator iter1 = mPools.upper_bound(mLastRebuildPool);
    while(max_count > 0 && mPools.size() > 0) // && num_rebuilds < MAX_REBUILDS)
    {
        if (iter1 == mPools.end())
        {
            iter1 = mPools.begin();
        }
        LLDrawPool* poolp = *iter1;

        if (poolp->isDead())
        {
            mPools.erase(iter1++);
            removeFromQuickLookup( poolp );
            if (poolp == mLastRebuildPool)
            {
                mLastRebuildPool = NULL;
            }
            delete poolp;
        }
        else
        {
            mLastRebuildPool = poolp;
            iter1++;
        }
        max_count--;
    }
}

void LLPipeline::addToQuickLookup( LLDrawPool* new_poolp )
{
    assertInitialized();

    switch( new_poolp->getType() )
    {
    case LLDrawPool::POOL_SIMPLE:
        if (mSimplePool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate simple pool." << LL_ENDL;
        }
        else
        {
            mSimplePool = (LLRenderPass*) new_poolp;
        }
        break;

    case LLDrawPool::POOL_ALPHA_MASK:
        if (mAlphaMaskPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate alpha mask pool." << LL_ENDL;
            break;
        }
        else
        {
            mAlphaMaskPool = (LLRenderPass*) new_poolp;
        }
        break;

    case LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK:
        if (mFullbrightAlphaMaskPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate alpha mask pool." << LL_ENDL;
            break;
        }
        else
        {
            mFullbrightAlphaMaskPool = (LLRenderPass*) new_poolp;
        }
        break;

    case LLDrawPool::POOL_GRASS:
        if (mGrassPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate grass pool." << LL_ENDL;
        }
        else
        {
            mGrassPool = (LLRenderPass*) new_poolp;
        }
        break;

    case LLDrawPool::POOL_FULLBRIGHT:
        if (mFullbrightPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate simple pool." << LL_ENDL;
        }
        else
        {
            mFullbrightPool = (LLRenderPass*) new_poolp;
        }
        break;

    case LLDrawPool::POOL_GLOW:
        if (mGlowPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate glow pool." << LL_ENDL;
        }
        else
        {
            mGlowPool = (LLRenderPass*) new_poolp;
        }
        break;

    case LLDrawPool::POOL_TREE:
        mTreePools[ uintptr_t(new_poolp->getTexture()) ] = new_poolp ;
        break;

    case LLDrawPool::POOL_TERRAIN:
        mTerrainPools[ uintptr_t(new_poolp->getTexture()) ] = new_poolp ;
        break;

    case LLDrawPool::POOL_BUMP:
        if (mBumpPool)
        {
            llassert(0);
            LL_WARNS() << "Ignoring duplicate bump pool." << LL_ENDL;
        }
        else
        {
            mBumpPool = new_poolp;
        }
        break;
    case LLDrawPool::POOL_MATERIALS:
        if (mMaterialsPool)
        {
            llassert(0);
            LL_WARNS() << "Ignorning duplicate materials pool." << LL_ENDL;
        }
        else
        {
            mMaterialsPool = new_poolp;
        }
        break;
    case LLDrawPool::POOL_ALPHA_PRE_WATER:
        if( mAlphaPoolPreWater )
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Alpha pre-water pool" << LL_ENDL;
        }
        else
        {
            mAlphaPoolPreWater = (LLDrawPoolAlpha*) new_poolp;
        }
        break;
    case LLDrawPool::POOL_ALPHA_POST_WATER:
        if (mAlphaPoolPostWater)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Alpha post-water pool" << LL_ENDL;
        }
        else
        {
            mAlphaPoolPostWater = (LLDrawPoolAlpha*)new_poolp;
        }
        break;

    case LLDrawPool::POOL_AVATAR:
    case LLDrawPool::POOL_CONTROL_AV:
        break; // Do nothing

    case LLDrawPool::POOL_SKY:
        if( mSkyPool )
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Sky pool" << LL_ENDL;
        }
        else
        {
            mSkyPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_WATER:
        if( mWaterPool )
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Water pool" << LL_ENDL;
        }
        else
        {
            mWaterPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_WL_SKY:
        if( mWLSkyPool )
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate WLSky Pool" << LL_ENDL;
        }
        else
        {
            mWLSkyPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_GLTF_PBR:
        if( mPBROpaquePool )
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate PBR Opaque Pool" << LL_ENDL;
        }
        else
        {
            mPBROpaquePool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK:
        if (mPBRAlphaMaskPool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate PBR Alpha Mask Pool" << LL_ENDL;
        }
        else
        {
            mPBRAlphaMaskPool = new_poolp;
        }
        break;

    case LLDrawPool::POOL_WATEREXCLUSION:
        if (mWaterExclusionPool)
        {
            llassert(0);
            LL_WARNS() << "LLPipeline::addPool(): Ignoring duplicate Water Exclusion Pool" << LL_ENDL;
        }
        else
        {
            mWaterExclusionPool = new_poolp;
        }
        break;

    default:
        llassert(0);
        LL_WARNS() << "Invalid Pool Type in  LLPipeline::addPool()" << LL_ENDL;
        break;
    }
}

void LLPipeline::removePool( LLDrawPool* poolp )
{
    assertInitialized();
    removeFromQuickLookup(poolp);
    mPools.erase(poolp);
    delete poolp;
}

void LLPipeline::removeFromQuickLookup( LLDrawPool* poolp )
{
    assertInitialized();
    switch( poolp->getType() )
    {
    case LLDrawPool::POOL_SIMPLE:
        llassert(mSimplePool == poolp);
        mSimplePool = NULL;
        break;

    case LLDrawPool::POOL_ALPHA_MASK:
        llassert(mAlphaMaskPool == poolp);
        mAlphaMaskPool = NULL;
        break;

    case LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK:
        llassert(mFullbrightAlphaMaskPool == poolp);
        mFullbrightAlphaMaskPool = NULL;
        break;

    case LLDrawPool::POOL_GRASS:
        llassert(mGrassPool == poolp);
        mGrassPool = NULL;
        break;

    case LLDrawPool::POOL_FULLBRIGHT:
        llassert(mFullbrightPool == poolp);
        mFullbrightPool = NULL;
        break;

    case LLDrawPool::POOL_WL_SKY:
        llassert(mWLSkyPool == poolp);
        mWLSkyPool = NULL;
        break;

    case LLDrawPool::POOL_GLOW:
        llassert(mGlowPool == poolp);
        mGlowPool = NULL;
        break;

    case LLDrawPool::POOL_TREE:
        mTreePools.erase( (uintptr_t)poolp->getTexture() );
        break;

    case LLDrawPool::POOL_TERRAIN:
        mTerrainPools.erase( (uintptr_t)poolp->getTexture() );
        break;

    case LLDrawPool::POOL_BUMP:
        llassert( poolp == mBumpPool );
        mBumpPool = NULL;
        break;

    case LLDrawPool::POOL_MATERIALS:
        llassert(poolp == mMaterialsPool);
        mMaterialsPool = NULL;
        break;

    case LLDrawPool::POOL_ALPHA_PRE_WATER:
        llassert( poolp == mAlphaPoolPreWater );
        mAlphaPoolPreWater = nullptr;
        break;

    case LLDrawPool::POOL_ALPHA_POST_WATER:
        llassert(poolp == mAlphaPoolPostWater);
        mAlphaPoolPostWater = nullptr;
        break;

    case LLDrawPool::POOL_AVATAR:
    case LLDrawPool::POOL_CONTROL_AV:
        break; // Do nothing

    case LLDrawPool::POOL_SKY:
        llassert( poolp == mSkyPool );
        mSkyPool = NULL;
        break;

    case LLDrawPool::POOL_WATER:
        llassert( poolp == mWaterPool );
        mWaterPool = NULL;
        break;

    case LLDrawPool::POOL_GLTF_PBR:
        llassert( poolp == mPBROpaquePool );
        mPBROpaquePool = NULL;
        break;

    case LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK:
        llassert(poolp == mPBRAlphaMaskPool);
        mPBRAlphaMaskPool = NULL;
        break;

    case LLDrawPool::POOL_WATEREXCLUSION:
        llassert(poolp == mWaterExclusionPool);
        mWaterExclusionPool = nullptr;
        break;

    default:
        llassert(0);
        LL_WARNS() << "Invalid Pool Type in  LLPipeline::removeFromQuickLookup() type=" << poolp->getType() << LL_ENDL;
        break;
    }
}

void LLPipeline::resetDrawOrders()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    assertInitialized();
    // Iterate through all of the draw pools and rebuild them.
    for (pool_set_t::iterator iter = mPools.begin(); iter != mPools.end(); ++iter)
    {
        LLDrawPool *poolp = *iter;
        poolp->resetDrawOrders();
    }
}

//============================================================================
// Once-per-frame setup of hardware lights,
// including sun/moon, avatar backlight, and up to 6 local lights

void LLPipeline::setupAvatarLights(bool for_edit)
{
    assertInitialized();

    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = environment.getCurrentSky();

    bool sun_up = environment.getIsSunUp();


    if (for_edit)
    {
        LLColor4 diffuse(1.f, 1.f, 1.f, 0.f);
        LLVector4 light_pos_cam(-8.f, 0.25f, 10.f, 0.f);  // w==0 => directional light
        LLMatrix4 camera_mat = LLViewerCamera::getInstance()->frameModelview().toMatrix4();
        LLMatrix4 camera_rot(camera_mat.getMat3());
        camera_rot.invert();
        LLVector4 light_pos = light_pos_cam * camera_rot;

        light_pos.normalize();

        LLLightState* light = gGL.getLight(1);

        mHWLightColors[1] = diffuse;

        light->setDiffuse(diffuse);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
        light->setPosition(light_pos);
        light->setConstantAttenuation(1.f);
        light->setLinearAttenuation(0.f);
        light->setQuadraticAttenuation(0.f);
        light->setSpotExponent(0.f);
        light->setSpotCutoff(180.f);
    }
    else if (gAvatarBacklight)
    {
        LLVector3 light_dir = sun_up ? LLVector3(mSunDir) : LLVector3(mMoonDir);
        LLVector3 opposite_pos = -light_dir;
        LLVector3 orthog_light_pos = light_dir % LLVector3::z_axis;
        LLVector4 backlight_pos = LLVector4(lerp(opposite_pos, orthog_light_pos, 0.3f), 0.0f);
        backlight_pos.normalize();

        LLColor4 light_diffuse = sun_up ? mSunDiffuse : mMoonDiffuse;

        LLColor4 backlight_diffuse(1.f - light_diffuse.mV[VRED], 1.f - light_diffuse.mV[VGREEN], 1.f - light_diffuse.mV[VBLUE], 1.f);
        F32 max_component = 0.001f;
        for (S32 i = 0; i < 3; i++)
        {
            if (backlight_diffuse.mV[i] > max_component)
            {
                max_component = backlight_diffuse.mV[i];
            }
        }
        F32 backlight_mag;
        if (LLEnvironment::instance().getIsSunUp())
        {
            backlight_mag = BACKLIGHT_DAY_MAGNITUDE_OBJECT;
        }
        else
        {
            backlight_mag = BACKLIGHT_NIGHT_MAGNITUDE_OBJECT;
        }
        backlight_diffuse *= backlight_mag / max_component;

        mHWLightColors[1] = backlight_diffuse;

        LLLightState* light = gGL.getLight(1);

        light->setPosition(backlight_pos);
        light->setDiffuse(backlight_diffuse);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
        light->setConstantAttenuation(1.f);
        light->setLinearAttenuation(0.f);
        light->setQuadraticAttenuation(0.f);
        light->setSpotExponent(0.f);
        light->setSpotCutoff(180.f);
    }
    else
    {
        LLLightState* light = gGL.getLight(1);

        mHWLightColors[1] = LLColor4::black;

        light->setDiffuse(LLColor4::black);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
    }
}

static F32 calc_light_dist(LLVOVolume* light, const LLVector3& cam_pos, F32 max_dist)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    F32 inten = light->getLightIntensity();
    if (inten < .001f)
    {
        return max_dist;
    }
    bool selected = light->isSelected();
    if (selected)
    {
        return 0.f; // selected lights get highest priority
    }
    F32 radius = light->getLightRadius();
    F32 dist = dist_vec(light->getRenderPosition(), cam_pos);
    dist = llmax(dist - radius, 0.f);
    if (light->mDrawable.notNull() && light->mDrawable->isState(LLDrawable::ACTIVE))
    {
        // moving lights get a little higher priority (too much causes artifacts)
        dist = llmax(dist - radius * 0.25f, 0.f);
    }
    return dist;
}

// How much of a nearby light shows for its fade clock (see calcNearbyLights): none at 0
// rising to all of it at LIGHT_FADE_TIME, and all of it at -0 falling to none at
// -LIGHT_FADE_TIME.
static F32 light_fade_brightness(F32 fade)
{
    if (fade >= LIGHT_FADE_TIME)
    {
        return 1.f;
    }
    return llclamp(fade >= 0.f ? fade / LIGHT_FADE_TIME : 1.f + fade / LIGHT_FADE_TIME, 0.f, 1.f);
}

// The stored depths a view-space span of lit points can hold, from ALDepthBounds::viewSpan's: under forward-Z the 24-bit
// depth stores the step nearest a point's depth, as much as half a step past the span's ends, which far off is more than
// any margin in view space (a step is about 15 cm at 500 m), so the span is let out by two steps, kept off the
// clear. A float depth's rounding lies far inside the view-space margin.
static void widen_for_storage(F64& zmin, F64& zmax)
{
    if (!LLRender::sReverseZ)
    {
        constexpr F64 STEP = 1.0 / 16777215.0;
        F64 drawn_min, drawn_max;
        ALDepthBounds::drawn(drawn_min, drawn_max);
        zmin = llmax(zmin - 2.0 * STEP, drawn_min);
        zmax = llmin(zmax + 2.0 * STEP, drawn_max);
    }
}

// Holds a light's draw to the stored depths its light reaches: its sphere, `size` around its view-space centre (the
// light shaders' cutoff), widened a little for what reconstruction loses and for the depth's storage. To everything
// drawn where the projection cannot say.
static void bound_to_light(ALDepthBounds& bounds, const LLMatrix4a& proj, F32 view_z, F32 size)
{
    const F32 reach = size * 1.01f + 0.01f;
    F64 zmin, zmax;
    if (ALDepthBounds::viewSpan(proj, view_z + reach, view_z - reach, zmin, zmax))
    {
        widen_for_storage(zmin, zmax);
    }
    else
    {
        ALDepthBounds::drawn(zmin, zmax);
    }
    bounds.set(zmin, zmax);
}

void LLPipeline::calcNearbyLights(LLCamera& camera)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    assertInitialized();

    // The nearby set is the frame camera's, and its fade clock runs once a frame. Probe faces
    // and impostor bakes render from cameras of their own and take the set as the frame left
    // it: a bake runs before the frame's deferred lighting, which would otherwise draw the
    // set in the bake camera's order.
    if (gCubeSnapshot || LLPipeline::sImpostorRender || LLPipeline::sRenderingHUDs || LLApp::isExiting())
    {
        return;
    }

    static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);

    if (local_light_count >= 1)
    {
        // mNearbyLight (and all light_set_t's) are sorted such that
        // begin() == the closest light and rbegin() == the farthest light
        const LLVector3& cam_pos = camera.getOrigin();

        F32 max_dist;
        // The draw distance, which the depth convention holds the setting to.
        if (LLPipeline::sRenderDeferred)
        {
            max_dist = gAgentCamera.mDrawDistance;
        }
        else
        {
            max_dist = llmin(gAgentCamera.mDrawDistance, LIGHT_MAX_RADIUS * 4.f);
        }

        // UPDATE THE EXISTING NEARBY LIGHTS
        // Distance is the set's sort key, so a light cannot be updated where it stands: each
        // one is lifted out by its node, given its new distance and fade, and dropped into
        // updated_lights in its new order. The node moves with it, so nothing is reallocated.
        light_set_t updated_lights;
        const F32 frame_interval = gFrameIntervalSeconds.value();
        for (light_set_t::iterator iter = mNearbyLights.begin();
            iter != mNearbyLights.end();)
        {
            const Light* light = &(*iter);
            LLDrawable* drawable = light->drawable;
            const LLViewerObject *vobj = light->drawable->getVObj();
            if(vobj && vobj->isAttachment())
            {
                if (!sRenderAttachedLights)
                {
                    drawable->clearState(LLDrawable::NEARBY_LIGHT);
                    iter = mNearbyLights.erase(iter);
                    continue;
                }

                LLVOAvatar *avatar = vobj->getAvatar();
                if (avatar && (avatar->isTooComplex() || avatar->isInMuteList() || avatar->isTooSlow()))
                {
                    drawable->clearState(LLDrawable::NEARBY_LIGHT);
                    iter = mNearbyLights.erase(iter);
                    continue;
                }
            }

            LLVOVolume* volight = drawable->getVOVolume();
            if (!volight || !drawable->isState(LLDrawable::LIGHT))
            {
                drawable->clearState(LLDrawable::NEARBY_LIGHT);
                iter = mNearbyLights.erase(iter);
                continue;
            }
            if (light->fade <= -LIGHT_FADE_TIME)
            {
                drawable->clearState(LLDrawable::NEARBY_LIGHT);
                iter = mNearbyLights.erase(iter);
                continue;
            }

            F32 dist = calc_light_dist(volight, cam_pos, max_dist);
            F32 fade = light->fade;
            // actual fade gets decreased/increased below, once a frame for every light;
            // setupHWLights only reads it
            // light->fade value is 'time'.
            // >=0 and light will become visible as value increases
            // <0 and light will fade out
            if (dist < max_dist)
            {
                if (fade < 0)
                {
                    // mark light to fade in
                    // if fade was -LIGHT_FADE_TIME - it was fully invisible
                    // if fade -0 - it was fully visible
                    // visibility goes up from 0 to LIGHT_FADE_TIME.
                    fade += LIGHT_FADE_TIME;
                }
            }
            else
            {
                // mark light to fade out
                // visibility goes down from -0 to -LIGHT_FADE_TIME.
                if (fade >= LIGHT_FADE_TIME)
                {
                    fade = -0.0001f; // was fully visible
                }
                else if (fade >= 0)
                {
                    // 0.75 visible light should stay 0.75 visible, but should reverse direction
                    fade -= LIGHT_FADE_TIME;
                }
            }

            // advance the fade in whichever direction it now runs; a fully visible light
            // holds, and one that reaches -LIGHT_FADE_TIME is erased next frame
            if (fade < LIGHT_FADE_TIME)
            {
                fade += (fade >= 0.f) ? frame_interval : -frame_interval;
            }

            light_set_t::node_type node = mNearbyLights.extract(iter++);
            node.value().dist = dist;
            node.value().fade = fade;
            updated_lights.insert(std::move(node));
        }
        mNearbyLights.swap(updated_lights);

        // FIND NEW LIGHTS THAT ARE IN RANGE
        for (LLDrawable::ordered_drawable_set_t::iterator iter = mLights.begin();
             iter != mLights.end(); ++iter)
        {
            LLDrawable* drawable = *iter;
            LLVOVolume* light = drawable->getVOVolume();
            if (!light || drawable->isState(LLDrawable::NEARBY_LIGHT))
            {
                continue;
            }
            if (light->isHUDAttachment())
            {
                continue; // no lighting from HUD objects
            }
            if (light->isAttachment())
            {
                if (!sRenderAttachedLights)
                {
                    continue;
                }
                LLVOAvatar* av = light->getAvatar();
                if (av && (av->isTooComplex() || av->isInMuteList() || av->isTooSlow()))
                {
                    // avatars that are already in the list will be removed by removeMutedAVsLights
                    continue;
                }
            }
            F32 dist = calc_light_dist(light, cam_pos, max_dist);
            if (dist >= max_dist)
            {
                continue;
            }

            mNearbyLights.insert(Light(drawable, dist, 0.f));
            drawable->setState(LLDrawable::NEARBY_LIGHT);
        }

        //mark nearby lights not-removable.
        for (light_set_t::iterator iter = mNearbyLights.begin();
             iter != mNearbyLights.end(); iter++)
        {
            const Light* light = &(*iter);
            ((LLViewerOctreeEntryData*) light->drawable)->setVisible();
        }
    }
}

void LLPipeline::setupHWLights()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    assertInitialized();

    if (LLPipeline::sRenderingHUDs)
    {
        return;
    }

    F32 light_scale = 1.f;

    if (gCubeSnapshot)
    { //darken local lights when probe ambiance is above 1
        light_scale = mReflectionMapManager.mLightScale;
    }
    else
    {
        static LLCachedControl<F32> alchemy_light_scale(gSavedSettings, "AlchemyGlobalLightScale", 1.f);
        light_scale = alchemy_light_scale;
    }


    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = environment.getCurrentSky();

    // Ambient
    LLColor4 ambient = psky->getTotalAmbient();

    gGL.setAmbientLightColor(ambient);

    bool sun_up  = environment.getIsSunUp();
    bool moon_up = environment.getIsMoonUp();

    // Light 0 = Sun or Moon (All objects)
    {
        LLVector4 sun_dir(environment.getSunDirection(), 0.0f);
        LLVector4 moon_dir(environment.getMoonDirection(), 0.0f);

        mSunDir.setVec(sun_dir);
        mMoonDir.setVec(moon_dir);

        // The same light the Environment block's atmospheric terms take (LLSettingsVOSky::applySpecial),
        // black while neither body is up.
        LLColor3 sun_diffuse;
        LLColor3 moon_diffuse;
        psky->getLitLightColors(sun_diffuse, moon_diffuse);
        mSunDiffuse.set(sun_diffuse, 1.f);
        mMoonDiffuse.set(moon_diffuse, 1.f);

        // prevent underlighting from having neither lightsource facing us
        if (!sun_up && !moon_up)
        {
            mSunDir.setVec(LLVector4(0.0, 1.0, 0.0, 0.0));
            mMoonDir.setVec(LLVector4(0.0, 1.0, 0.0, 0.0));
        }

        LLVector4 light_dir = sun_up ? mSunDir : mMoonDir;

        mHWLightColors[0] = sun_up ? mSunDiffuse : mMoonDiffuse;

        LLLightState* light = gGL.getLight(0);
        light->setPosition(light_dir);

        light->setSunPrimary(sun_up);
        light->setDiffuse(mHWLightColors[0]);
        light->setDiffuseB(mMoonDiffuse);
        light->setAmbient(psky->getTotalAmbient());
        light->setSpecular(LLColor4::black);
        light->setConstantAttenuation(1.f);
        light->setLinearAttenuation(0.f);
        light->setQuadraticAttenuation(0.f);
        light->setSpotExponent(0.f);
        light->setSpotCutoff(180.f);
    }

    // Light 1 = Backlight (for avatars)
    // (set by enableLightsAvatar)

    S32 cur_light = 2;

    // Nearby lights = LIGHT 2-7

    mLightMovingMask = 0;

    static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);

    if (local_light_count >= 1)
    {
        for (light_set_t::iterator iter = mNearbyLights.begin();
             iter != mNearbyLights.end(); ++iter)
        {
            LLDrawable* drawable = iter->drawable;
            LLVOVolume* light = drawable->getVOVolume();
            if (!light)
            {
                continue;
            }

            if (light->isAttachment())
            {
                if (!sRenderAttachedLights)
                {
                    continue;
                }
            }

            if (drawable->isState(LLDrawable::ACTIVE))
            {
                mLightMovingMask |= (1<<cur_light);
            }

            //send linear light color to shader
            LLColor4  light_color = light->getLightLinearColor() * light_scale;
            light_color.mV[3] = 0.0f;

            // fade in/out light
            light_color *= light_fade_brightness(iter->fade);

            if (light_color.magVecSquared() < 0.001f)
            {
                continue;
            }

            LLVector3 light_pos(light->getRenderPosition());
            LLVector4 light_pos_gl(light_pos, 1.0f);

            F32 adjusted_radius = light->getLightRadius() * (sRenderDeferred ? 1.5f : 1.0f);
            if (adjusted_radius <= 0.001f)
            {
                continue;
            }

            F32 x = (3.f * (1.f + (light->getLightFalloff() * 2.0f)));  // why this magic?  probably trying to match a historic behavior.
            F32 linatten = x / adjusted_radius;                         // % of brightness at radius

            mHWLightColors[cur_light] = light_color;
            LLLightState* light_state = gGL.getLight(cur_light);

            light_state->setPosition(light_pos_gl);
            light_state->setDiffuse(light_color);
            light_state->setAmbient(LLColor4::black);
            light_state->setConstantAttenuation(0.f);
            light_state->setSize(light->getLightRadius() * 1.5f);
            light_state->setFalloff(light->getLightFalloff(DEFERRED_LIGHT_FALLOFF));

            if (sRenderDeferred)
            {
                light_state->setLinearAttenuation(linatten);
                light_state->setQuadraticAttenuation(light->getLightFalloff(DEFERRED_LIGHT_FALLOFF) + 1.f); // get falloff to match for forward deferred rendering lights
            }
            else
            {
                light_state->setLinearAttenuation(linatten);
                light_state->setQuadraticAttenuation(0.f);
            }


            if (light->isLightSpotlight() // directional (spot-)light
                && (LLPipeline::sRenderDeferred || RenderSpotLightsInNondeferred)) // these are only rendered as GL spotlights if we're in deferred rendering mode *or* the setting forces them on
            {
                LLQuaternion quat = light->getRenderRotation();
                LLVector3 at_axis(0,0,-1); // this matches deferred rendering's object light direction
                at_axis *= quat;

                light_state->setSpotDirection(at_axis);
                light_state->setSpotCutoff(90.f);
                light_state->setSpotExponent(2.f);

                LLVector3 spotParams = light->getSpotLightParams();

                const LLColor4 specular(0.f, 0.f, 0.f, spotParams[2]);
                light_state->setSpecular(specular);
            }
            else // omnidirectional (point) light
            {
                light_state->setSpotExponent(0.f);
                light_state->setSpotCutoff(180.f);

                // we use specular.z = 1.0 as a cheap hack for the shaders to know that this is omnidirectional rather than a spotlight
                const LLColor4 specular(0.f, 0.f, 1.f, 0.f);
                light_state->setSpecular(specular);
            }
            cur_light++;
            if (cur_light >= 8)
            {
                break; // safety
            }
        }
    }
    for ( ; cur_light < 8 ; cur_light++)
    {
        mHWLightColors[cur_light] = LLColor4::black;
        LLLightState* light = gGL.getLight(cur_light);
        light->setSunPrimary(true);
        light->setDiffuse(LLColor4::black);
        light->setAmbient(LLColor4::black);
        light->setSpecular(LLColor4::black);
    }

    // Bookmark comment to allow searching for mSpecialRenderMode == 3 (avatar edit mode),
    // prev site of forward (non-deferred) character light injection, removed by SL-13522 09/20

    // Init GL state
    for (S32 i = 0; i < 8; ++i)
    {
        gGL.getLight(i)->disable();
    }
    mLightMask = 0;
}

void LLPipeline::enableLights(U32 mask)
{
    assertInitialized();

    if (mLightMask != mask)
    {
        stop_glerror();
        if (mask)
        {
            stop_glerror();
            for (S32 i=0; i<8; i++)
            {
                LLLightState* light = gGL.getLight(i);
                if (mask & (1<<i))
                {
                    light->enable();
                    light->setDiffuse(mHWLightColors[i]);
                }
                else
                {
                    light->disable();
                    light->setDiffuse(LLColor4::black);
                }
            }
            stop_glerror();
        }
        mLightMask = mask;
        stop_glerror();
    }
}

void LLPipeline::enableLightsDynamic()
{
    assertInitialized();
    U32 mask = 0xff & (~2); // Local lights
    enableLights(mask);

    if (isAgentAvatarValid())
    {
        if (gAgentAvatarp->mSpecialRenderMode == 0) // normal
        {
            gPipeline.enableLightsAvatar();
        }
        else if (gAgentAvatarp->mSpecialRenderMode == 2)  // anim preview
        {
            gPipeline.enableLightsAvatarEdit(LLColor4(0.7f, 0.6f, 0.3f, 1.f));
        }
    }
}

void LLPipeline::enableLightsAvatar()
{
    U32 mask = 0xff; // All lights
    setupAvatarLights(false);
    enableLights(mask);
}

void LLPipeline::enableLightsPreview()
{
    disableLights();

    LLColor4 ambient = PreviewAmbientColor;
    gGL.setAmbientLightColor(ambient);

    LLColor4 diffuse0 = PreviewDiffuse0;
    LLColor4 specular0 = PreviewSpecular0;
    LLColor4 diffuse1 = PreviewDiffuse1;
    LLColor4 specular1 = PreviewSpecular1;
    LLColor4 diffuse2 = PreviewDiffuse2;
    LLColor4 specular2 = PreviewSpecular2;

    LLVector3 dir0 = PreviewDirection0;
    LLVector3 dir1 = PreviewDirection1;
    LLVector3 dir2 = PreviewDirection2;

    dir0.normVec();
    dir1.normVec();
    dir2.normVec();

    LLVector4 light_pos(dir0, 0.0f);

    LLLightState* light = gGL.getLight(1);

    light->enable();
    light->setPosition(light_pos);
    light->setDiffuse(diffuse0);
    light->setAmbient(ambient);
    light->setSpecular(specular0);
    light->setSpotExponent(0.f);
    light->setSpotCutoff(180.f);

    light_pos = LLVector4(dir1, 0.f);

    light = gGL.getLight(2);
    light->enable();
    light->setPosition(light_pos);
    light->setDiffuse(diffuse1);
    light->setAmbient(ambient);
    light->setSpecular(specular1);
    light->setSpotExponent(0.f);
    light->setSpotCutoff(180.f);

    light_pos = LLVector4(dir2, 0.f);
    light = gGL.getLight(3);
    light->enable();
    light->setPosition(light_pos);
    light->setDiffuse(diffuse2);
    light->setAmbient(ambient);
    light->setSpecular(specular2);
    light->setSpotExponent(0.f);
    light->setSpotCutoff(180.f);
}


void LLPipeline::enableLightsAvatarEdit(const LLColor4& color)
{
    U32 mask = 0x2002; // Avatar backlight only, set ambient
    setupAvatarLights(true);
    enableLights(mask);

    gGL.setAmbientLightColor(color);
}

void LLPipeline::enableLightsFullbright()
{
    assertInitialized();
    U32 mask = 0x1000; // Non-0 mask, set ambient
    enableLights(mask);
}

void LLPipeline::disableLights()
{
    enableLights(0); // no lighting (full bright)
}

//============================================================================

class LLMenuItemGL;
class LLInvFVBridge;
struct cat_folder_pair;
class LLVOBranch;
class LLVOLeaf;

void LLPipeline::findReferences(LLDrawable *drawablep)
{
    assertInitialized();
    if (mLights.find(drawablep) != mLights.end())
    {
        LL_INFOS() << "In mLights" << LL_ENDL;
    }
    if (std::find(mMovedList.begin(), mMovedList.end(), drawablep) != mMovedList.end())
    {
        LL_INFOS() << "In mMovedList" << LL_ENDL;
    }
    if (std::find(mShiftList.begin(), mShiftList.end(), drawablep) != mShiftList.end())
    {
        LL_INFOS() << "In mShiftList" << LL_ENDL;
    }
    if (mRetexturedList.find(drawablep) != mRetexturedList.end())
    {
        LL_INFOS() << "In mRetexturedList" << LL_ENDL;
    }

    if (std::find(mBuildQ1.begin(), mBuildQ1.end(), drawablep) != mBuildQ1.end())
    {
        LL_INFOS() << "In mBuildQ1" << LL_ENDL;
    }

    S32 count;

    count = gObjectList.findReferences(drawablep);
    if (count)
    {
        LL_INFOS() << "In other drawables: " << count << " references" << LL_ENDL;
    }
}

bool LLPipeline::verify()
{
    bool ok = assertInitialized();
    if (ok)
    {
        for (pool_set_t::iterator iter = mPools.begin(); iter != mPools.end(); ++iter)
        {
            LLDrawPool *poolp = *iter;
            if (!poolp->verify())
            {
                ok = false;
            }
        }
    }

    if (!ok)
    {
        LL_WARNS() << "Pipeline verify failed!" << LL_ENDL;
    }
    return ok;
}

//////////////////////////////
//
// Collision detection
//
//

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 *  A method to compute a ray-AABB intersection.
 *  Original code by Andrew Woo, from "Graphics Gems", Academic Press, 1990
 *  Optimized code by Pierre Terdiman, 2000 (~20-30% faster on my Celeron 500)
 *  Epsilon value added by Klaus Hartmann. (discarding it saves a few cycles only)
 *
 *  Hence this version is faster as well as more robust than the original one.
 *
 *  Should work provided:
 *  1) the integer representation of 0.0f is 0x00000000
 *  2) the sign bit of the float is the most significant one
 *
 *  Report bugs: p.terdiman@codercorner.com
 *
 *  \param      aabb        [in] the axis-aligned bounding box
 *  \param      origin      [in] ray origin
 *  \param      dir         [in] ray direction
 *  \param      coord       [out] impact coordinates
 *  \return     true if ray intersects AABB
 */
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//#define RAYAABB_EPSILON 0.00001f
// Read a float's IEEE-754 bit pattern as U32. The previous (U32&)x form
// was a strict-aliasing violation; std::bit_cast lowers to the same load
// without UB.
#define IR(x)   (std::bit_cast<U32>(x))

bool LLRayAABB(const LLVector3 &center, const LLVector3 &size, const LLVector3& origin, const LLVector3& dir, LLVector3 &coord, F32 epsilon)
{
    bool Inside = true;
    LLVector3 MinB = center - size;
    LLVector3 MaxB = center + size;
    LLVector3 MaxT;
    MaxT.mV[VX]=MaxT.mV[VY]=MaxT.mV[VZ]=-1.0f;

    // Find candidate planes.
    for(U32 i=0;i<3;i++)
    {
        if(origin.mV[i] < MinB.mV[i])
        {
            coord.mV[i] = MinB.mV[i];
            Inside      = false;

            // Calculate T distances to candidate planes
            if(IR(dir.mV[i]))   MaxT.mV[i] = (MinB.mV[i] - origin.mV[i]) / dir.mV[i];
        }
        else if(origin.mV[i] > MaxB.mV[i])
        {
            coord.mV[i] = MaxB.mV[i];
            Inside      = false;

            // Calculate T distances to candidate planes
            if(IR(dir.mV[i]))   MaxT.mV[i] = (MaxB.mV[i] - origin.mV[i]) / dir.mV[i];
        }
    }

    // Ray origin inside bounding box
    if(Inside)
    {
        coord = origin;
        return true;
    }

    // Get largest of the maxT's for final choice of intersection
    U32 WhichPlane = 0;
    if(MaxT.mV[1] > MaxT.mV[WhichPlane])    WhichPlane = 1;
    if(MaxT.mV[2] > MaxT.mV[WhichPlane])    WhichPlane = 2;

    // Check final candidate actually inside box
    if(IR(MaxT.mV[WhichPlane])&0x80000000) return false;

    for(U32 i=0;i<3;i++)
    {
        if(i!=WhichPlane)
        {
            coord.mV[i] = origin.mV[i] + MaxT.mV[WhichPlane] * dir.mV[i];
            if (epsilon > 0)
            {
                if(coord.mV[i] < MinB.mV[i] - epsilon || coord.mV[i] > MaxB.mV[i] + epsilon)    return false;
            }
            else
            {
                if(coord.mV[i] < MinB.mV[i] || coord.mV[i] > MaxB.mV[i])    return false;
            }
        }
    }
    return true;    // ray hits box
}

//////////////////////////////
//
// Macros, functions, and inline methods from other classes
//
//

void LLPipeline::setLight(LLDrawable *drawablep, bool is_light)
{
    if (drawablep && assertInitialized())
    {
        if (is_light)
        {
            mLights.insert(drawablep);
            drawablep->setState(LLDrawable::LIGHT);
        }
        else
        {
            drawablep->clearState(LLDrawable::LIGHT);
            mLights.erase(drawablep);
        }
    }
}

//static
void LLPipeline::toggleRenderType(U32 type)
{
    gPipeline.mRenderTypeEnabled[type] = !gPipeline.mRenderTypeEnabled[type];
    if (type == LLPipeline::RENDER_TYPE_WATER)
    {
        gPipeline.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_VOIDWATER] = !gPipeline.mRenderTypeEnabled[LLPipeline::RENDER_TYPE_VOIDWATER];
    }
}

//static
void LLPipeline::toggleRenderTypeControl(U32 type)
{
    gPipeline.toggleRenderType(type);
}

//static
bool LLPipeline::hasRenderTypeControl(U32 type)
{
    return gPipeline.hasRenderType(type);
}

// Allows UI items labeled "Hide foo" instead of "Show foo"
//static
bool LLPipeline::toggleRenderTypeControlNegated(S32 type)
{
    return !gPipeline.hasRenderType(type);
}

//static
void LLPipeline::toggleRenderDebug(U64 bit)
{
    if (gPipeline.hasRenderDebugMask(bit))
    {
        LL_INFOS() << "Toggling render debug mask " << std::hex << bit << " off" << std::dec << LL_ENDL;
    }
    else
    {
        LL_INFOS() << "Toggling render debug mask " << std::hex << bit << " on" << std::dec << LL_ENDL;
    }
    gPipeline.mRenderDebugMask ^= bit;
}


//static
bool LLPipeline::toggleRenderDebugControl(U64 bit)
{
    return gPipeline.hasRenderDebugMask(bit);
}

//static
void LLPipeline::toggleRenderDebugFeature(U32 bit)
{
    gPipeline.mRenderDebugFeatureMask ^= bit;
}


//static
bool LLPipeline::toggleRenderDebugFeatureControl(U32 bit)
{
    return gPipeline.hasRenderDebugFeatureMask(bit);
}

void LLPipeline::setRenderDebugFeatureControl(U32 bit, bool value)
{
    if (value)
    {
        gPipeline.mRenderDebugFeatureMask |= bit;
    }
    else
    {
        gPipeline.mRenderDebugFeatureMask &= !bit;
    }
}

void LLPipeline::pushRenderDebugFeatureMask()
{
    mRenderDebugFeatureStack.push(mRenderDebugFeatureMask);
}

void LLPipeline::popRenderDebugFeatureMask()
{
    if (mRenderDebugFeatureStack.empty())
    {
        LL_ERRS() << "Depleted render feature stack." << LL_ENDL;
    }

    mRenderDebugFeatureMask = mRenderDebugFeatureStack.top();
    mRenderDebugFeatureStack.pop();
}

// static
void LLPipeline::setRenderScriptedBeacons(bool val)
{
    sRenderScriptedBeacons = val;
}

// static
void LLPipeline::toggleRenderScriptedBeacons()
{
    sRenderScriptedBeacons = !sRenderScriptedBeacons;
}

// static
bool LLPipeline::getRenderScriptedBeacons()
{
    return sRenderScriptedBeacons;
}

// static
void LLPipeline::setRenderScriptedTouchBeacons(bool val)
{
    sRenderScriptedTouchBeacons = val;
}

// static
void LLPipeline::toggleRenderScriptedTouchBeacons()
{
    sRenderScriptedTouchBeacons = !sRenderScriptedTouchBeacons;
}

// static
bool LLPipeline::getRenderScriptedTouchBeacons()
{
    return sRenderScriptedTouchBeacons;
}

// static
void LLPipeline::setRenderMOAPBeacons(bool val)
{
    sRenderMOAPBeacons = val;
}

// static
void LLPipeline::toggleRenderMOAPBeacons()
{
    sRenderMOAPBeacons = !sRenderMOAPBeacons;
}

// static
bool LLPipeline::getRenderMOAPBeacons()
{
    return sRenderMOAPBeacons;
}

// static
void LLPipeline::setRenderPhysicalBeacons(bool val)
{
    sRenderPhysicalBeacons = val;
}

// static
void LLPipeline::toggleRenderPhysicalBeacons()
{
    sRenderPhysicalBeacons = !sRenderPhysicalBeacons;
}

// static
bool LLPipeline::getRenderPhysicalBeacons()
{
    return sRenderPhysicalBeacons;
}

// static
void LLPipeline::setRenderParticleBeacons(bool val)
{
    sRenderParticleBeacons = val;
}

// static
void LLPipeline::toggleRenderParticleBeacons()
{
    sRenderParticleBeacons = !sRenderParticleBeacons;
}

// static
bool LLPipeline::getRenderParticleBeacons()
{
    return sRenderParticleBeacons;
}

// static
void LLPipeline::setRenderSoundBeacons(bool val)
{
    sRenderSoundBeacons = val;
}

// static
void LLPipeline::toggleRenderSoundBeacons()
{
    sRenderSoundBeacons = !sRenderSoundBeacons;
}

// static
bool LLPipeline::getRenderSoundBeacons()
{
    return sRenderSoundBeacons;
}

// static
void LLPipeline::setRenderBeacons(bool val)
{
    sRenderBeacons = val;
}

// static
void LLPipeline::toggleRenderBeacons()
{
    sRenderBeacons = !sRenderBeacons;
}

// static
bool LLPipeline::getRenderBeacons()
{
    return sRenderBeacons;
}

// static
void LLPipeline::setRenderHighlights(bool val)
{
    sRenderHighlight = val;
}

// static
void LLPipeline::toggleRenderHighlights()
{
    sRenderHighlight = !sRenderHighlight;
}

// static
bool LLPipeline::getRenderHighlights()
{
    return sRenderHighlight;
}

// static
void LLPipeline::setRenderHighlightTextureChannel(LLRender::eTexIndex channel)
{
    if (channel != sRenderHighlightTextureChannel)
    {
        sRenderHighlightTextureChannel = channel;
    }
}

LLVOPartGroup* LLPipeline::lineSegmentIntersectParticle(const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection,
                                                        S32* face_hit)
{
    LLVector4a local_end = end;

    LLVector4a position;

    LLDrawable* drawable = NULL;

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        LLSpatialPartition* part = region->getSpatialPartition(LLViewerRegion::PARTITION_PARTICLE);
        if (part && hasRenderType(part->mDrawableType))
        {
            LLDrawable* hit = part->lineSegmentIntersect(start, local_end, true, false, true, false, face_hit, &position, NULL, NULL, NULL);
            if (hit)
            {
                drawable = hit;
                local_end = position;
            }
        }
    }

    LLVOPartGroup* ret = NULL;
    if (drawable)
    {
        //make sure we're returning an LLVOPartGroup
        llassert(drawable->getVObj()->getPCode() == LLViewerObject::LL_VO_PART_GROUP);
        ret = (LLVOPartGroup*) drawable->getVObj().get();
    }

    if (intersection)
    {
        *intersection = position;
    }

    return ret;
}

LLViewerObject* LLPipeline::lineSegmentIntersectInWorld(const LLVector4a& start, const LLVector4a& end,
                                                        bool pick_transparent,
                                                        bool pick_rigged,
                                                        bool pick_unselectable,
                                                        bool pick_reflection_probe,
                                                        S32* face_hit,
                                                        LLVector4a* intersection,         // return the intersection point
                                                        LLVector2* tex_coord,            // return the texture coordinates of the intersection point
                                                        LLVector4a* normal,               // return the surface normal at the intersection point
                                                        LLVector4a* tangent             // return the surface tangent at the intersection point
    )
{
    LLDrawable* drawable = NULL;

    LLVector4a local_end = end;

    LLVector4a position;

    sPickAvatar = false; //! LLToolMgr::getInstance()->inBuildMode();

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        for (U32 j = 0; j < LLViewerRegion::NUM_PARTITIONS; j++)
        {
            if ((j == LLViewerRegion::PARTITION_VOLUME) ||
                (j == LLViewerRegion::PARTITION_BRIDGE) ||
                (j == LLViewerRegion::PARTITION_AVATAR) || // for attachments
                (j == LLViewerRegion::PARTITION_CONTROL_AV) ||
                (j == LLViewerRegion::PARTITION_TERRAIN) ||
                (j == LLViewerRegion::PARTITION_TREE) ||
                (j == LLViewerRegion::PARTITION_GRASS))  // only check these partitions for now
            {
                LLSpatialPartition* part = region->getSpatialPartition(j);
                if (part && hasRenderType(part->mDrawableType))
                {
                    LLDrawable* hit = part->lineSegmentIntersect(start, local_end, pick_transparent, pick_rigged, pick_unselectable, pick_reflection_probe, face_hit, &position, tex_coord, normal, tangent);
                    if (hit)
                    {
                        drawable = hit;
                        local_end = position;
                    }
                }
            }
        }
    }

    if (!sPickAvatar)
    {
        //save hit info in case we need to restore
        //due to attachment override
        LLVector4a local_normal;
        LLVector4a local_tangent;
        LLVector2 local_texcoord;
        S32 local_face_hit = -1;

        if (face_hit)
        {
            local_face_hit = *face_hit;
        }
        if (tex_coord)
        {
            local_texcoord = *tex_coord;
        }
        if (tangent)
        {
            local_tangent = *tangent;
        }
        else
        {
            local_tangent.clear();
        }
        if (normal)
        {
            local_normal = *normal;
        }
        else
        {
            local_normal.clear();
        }

        const F32 ATTACHMENT_OVERRIDE_DIST = 0.1f;

        //check against avatars
        sPickAvatar = true;
        for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
                iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
        {
            LLViewerRegion* region = *iter;

            LLSpatialPartition* part = region->getSpatialPartition(LLViewerRegion::PARTITION_AVATAR);
            if (part && hasRenderType(part->mDrawableType))
            {
                LLDrawable* hit = part->lineSegmentIntersect(start, local_end, pick_transparent, pick_rigged, pick_unselectable, pick_reflection_probe, face_hit, &position, tex_coord, normal, tangent);
                if (hit)
                {
                    LLVector4a delta;
                    delta.setSub(position, local_end);

                    if (!drawable ||
                        !drawable->getVObj()->isAttachment() ||
                        delta.getLength3().getF32() > ATTACHMENT_OVERRIDE_DIST)
                    { //avatar overrides if previously hit drawable is not an attachment or
                      //attachment is far enough away from detected intersection
                        drawable = hit;
                        local_end = position;
                    }
                    else
                    { //prioritize attachments over avatars
                        position = local_end;

                        if (face_hit)
                        {
                            *face_hit = local_face_hit;
                        }
                        if (tex_coord)
                        {
                            *tex_coord = local_texcoord;
                        }
                        if (tangent)
                        {
                            *tangent = local_tangent;
                        }
                        if (normal)
                        {
                            *normal = local_normal;
                        }
                    }
                }
            }
        }
    }

    // check all avatar nametags (silly, isn't it?)
    for (LLCharacter* character : LLCharacter::sInstances)
    {
        LLVOAvatar* avatar = (LLVOAvatar*)character;
        if (avatar->mNameText.notNull() &&
            avatar->mNameText->lineSegmentIntersect(start, local_end, position))
        {
            drawable = avatar->mDrawable;
            local_end = position;
        }
    }

    if (intersection)
    {
        *intersection = position;
    }

    return drawable ? drawable->getVObj().get() : NULL;
}

LLViewerObject* LLPipeline::lineSegmentIntersectInHUD(const LLVector4a& start, const LLVector4a& end,
                                                      bool pick_transparent,
                                                      S32* face_hit,
                                                      LLVector4a* intersection,         // return the intersection point
                                                      LLVector2* tex_coord,            // return the texture coordinates of the intersection point
                                                      LLVector4a* normal,               // return the surface normal at the intersection point
                                                      LLVector4a* tangent               // return the surface tangent at the intersection point
    )
{
    LLDrawable* drawable = NULL;

    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
            iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;

        bool toggle = false;
        if (!hasRenderType(LLPipeline::RENDER_TYPE_HUD))
        {
            toggleRenderType(LLPipeline::RENDER_TYPE_HUD);
            toggle = true;
        }

        LLSpatialPartition* part = region->getSpatialPartition(LLViewerRegion::PARTITION_HUD);
        if (part)
        {
            LLDrawable* hit = part->lineSegmentIntersect(start, end, pick_transparent, false, true, false, face_hit, intersection, tex_coord, normal, tangent);
            if (hit)
            {
                drawable = hit;
            }
        }

        if (toggle)
        {
            toggleRenderType(LLPipeline::RENDER_TYPE_HUD);
        }
    }
    return drawable ? drawable->getVObj().get() : NULL;
}

LLSpatialPartition* LLPipeline::getSpatialPartition(LLViewerObject* vobj)
{
    if (vobj)
    {
        LLViewerRegion* region = vobj->getRegion();
        if (region)
        {
            return region->getSpatialPartition(vobj->getPartitionType());
        }
    }
    return NULL;
}

void LLPipeline::resetVertexBuffers(LLDrawable* drawable)
{
    if (!drawable)
    {
        return;
    }

    for (S32 i = 0; i < drawable->getNumFaces(); i++)
    {
        LLFace* facep = drawable->getFace(i);
        if (facep)
        {
            facep->clearVertexBuffer();
        }
    }
}

void LLPipeline::renderObjects(U32 type, bool texture, bool batch_texture, bool rigged)
{
    assertInitialized();
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;

    if (rigged)
    {
        mSimplePool->pushRiggedBatches(type + 1, texture, batch_texture);
    }
    else
    {
        mSimplePool->pushBatches(type, texture, batch_texture);
    }

    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
}

void LLPipeline::renderGLTFObjects(U32 type, bool texture, bool rigged)
{
    assertInitialized();
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;

    if (rigged)
    {
        mSimplePool->pushRiggedGLTFBatches(type + 1, texture);
    }
    else
    {
        mSimplePool->pushGLTFBatches(type, texture);
    }

    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
}

// Currently only used for shadows -Cosmic,2023-04-19
void LLPipeline::renderAlphaObjects(bool rigged)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    assertInitialized();
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
    S32 sun_up = LLEnvironment::instance().getIsSunUp() ? 1 : 0;
    U32 target_width = LLRenderTarget::sCurResX;
    U32 type = LLRenderPass::PASS_ALPHA;
    // for gDeferredShadowAlphaMaskProgram
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin;
    // for gDeferredShadowGLTFAlphaBlendProgram
    const LLVOAvatar* lastAvatarGLTF = nullptr;
    U64 lastMeshIdGLTF = 0;
    bool skipLastSkinGLTF;
    // GLTF material bind cache; invalidated in the non-GLTF branches below since
    // mSimplePool->pushBatch rebinds texture units and would clobber the material
    LLFetchedGLTFMaterial* lastMatGLTF = nullptr;
    LLViewerTexture* lastTexGLTF = nullptr;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);

    // The caster's uniforms, set when a draw binds a different program from the last draw's: nothing
    // the batches push between them changes these.
    LLGLSLShader* configured = nullptr;
    const auto bind_caster = [&](LLGLSLShader& program, bool legacy)
    {
        program.bind(rigged);
        LLGLSLShader* bound = LLGLSLShader::sCurBoundShaderPtr;
        if (bound != configured)
        {
            configured = bound;
            bound->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
            bound->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
            if (legacy)
            {
                bound->uniform1f(LLShaderMgr::DEFERRED_SHADOW_STIPPLE, 1.f); // blended
            }
            bound->setMinimumAlpha(ALPHA_BLEND_CUTOFF);
        }
    };

    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo* pparams = *i;
        LLCullResult::increment_iterator(i, end);

        if (rigged != (pparams->mAvatar != nullptr))
        {
            // Pool contains both rigged and non-rigged DrawInfos. Only draw
            // the objects we're interested in in this pass.
            continue;
        }

        if (rigged)
        {
            if (pparams->mGLTFMaterial)
            {
                bind_caster(gDeferredShadowGLTFAlphaBlendProgram, false);
                LLRenderPass::pushRiggedGLTFBatch(*pparams, lastAvatarGLTF, lastMeshIdGLTF, skipLastSkinGLTF, lastMatGLTF, lastTexGLTF);
            }
            else
            {
                bind_caster(gDeferredShadowAlphaMaskProgram, true);
                lastMatGLTF = nullptr; // pushBatch clobbers texture units
                lastTexGLTF = nullptr;
                if (mSimplePool->uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
                {
                    mSimplePool->pushBatch(*pparams, true, true);
                }
            }
        }
        else
        {
            if (pparams->mGLTFMaterial)
            {
                bind_caster(gDeferredShadowGLTFAlphaBlendProgram, false);
                LLRenderPass::pushGLTFBatch(*pparams, lastMatGLTF, lastTexGLTF);
            }
            else
            {
                bind_caster(gDeferredShadowAlphaMaskProgram, true);
                lastMatGLTF = nullptr; // pushBatch clobbers texture units
                lastTexGLTF = nullptr;
                mSimplePool->pushBatch(*pparams, true, true);
            }
        }
    }

    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
}

// Currently only used for shadows -Cosmic,2023-04-19
void LLPipeline::renderMaskedObjects(U32 type, bool texture, bool batch_texture, bool rigged)
{
    assertInitialized();
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
    if (rigged)
    {
        mAlphaMaskPool->pushRiggedMaskBatches(type+1, texture, batch_texture);
    }
    else
    {
        mAlphaMaskPool->pushMaskBatches(type, texture, batch_texture);
    }
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
}

// Currently only used for shadows -Cosmic,2023-04-19
void LLPipeline::renderFullbrightMaskedObjects(U32 type, bool texture, bool batch_texture, bool rigged)
{
    assertInitialized();
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
    if (rigged)
    {
        mFullbrightAlphaMaskPool->pushRiggedMaskBatches(type+1, texture, batch_texture);
    }
    else
    {
        mFullbrightAlphaMaskPool->pushMaskBatches(type, texture, batch_texture);
    }
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
    gGLLastMatrix = NULL;
}

void apply_cube_face_rotation(U32 face)
{
    switch (face)
    {
        case 0:
            gGL.rotatef(90.f, 0, 1, 0);
            gGL.rotatef(180.f, 1, 0, 0);
        break;
        case 2:
            gGL.rotatef(-90.f, 1, 0, 0);
        break;
        case 4:
            gGL.rotatef(180.f, 0, 1, 0);
            gGL.rotatef(180.f, 0, 0, 1);
        break;
        case 1:
            gGL.rotatef(-90.f, 0, 1, 0);
            gGL.rotatef(180.f, 1, 0, 0);
        break;
        case 3:
            gGL.rotatef(90, 1, 0, 0);
        break;
        case 5:
            gGL.rotatef(180, 0, 0, 1);
        break;
    }
}

void validate_framebuffer_object()
{
    GLenum status;
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    switch(status)
    {
        case GL_FRAMEBUFFER_COMPLETE:
            //framebuffer OK, no error.
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            // frame buffer not OK: probably means unsupported depth buffer format
            LL_ERRS() << "Framebuffer Incomplete Missing Attachment." << LL_ENDL;
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            // frame buffer not OK: probably means unsupported depth buffer format
            LL_ERRS() << "Framebuffer Incomplete Attachment." << LL_ENDL;
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            /* choose different formats */
            LL_ERRS() << "Framebuffer unsupported." << LL_ENDL;
            break;
        default:
            LL_ERRS() << "Unknown framebuffer status." << LL_ENDL;
            break;
    }
}

void LLPipeline::bindScreenToTexture()
{

}

void LLPipeline::visualizeBuffers(LLRenderTarget* src, LLRenderTarget* dst, U32 bufferIndex)
{
    dst->bindTarget();
    gDeferredBufferVisualProgram.bind();
    // Attachment 0 of deferredScreen is sRGB, so match what the lighting pass sees; the
    // other attachments and the other targets this visualises are not, where the bit is inert.
    gDeferredBufferVisualProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src,
                                             ALSamplers::BilinearMirror | ALSampler::SRGBDecode,
                                             bufferIndex);

    if (RenderBufferVisualization != 4)
        gDeferredBufferVisualProgram.uniform1f(LLShaderMgr::MIP_LEVEL, 0);
    else
        gDeferredBufferVisualProgram.uniform1f(LLShaderMgr::MIP_LEVEL, 8);

    mScreenTriangleVB->setBuffer();
    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    gDeferredBufferVisualProgram.unbind();
    dst->flush();
}

void LLPipeline::requestScenePixel(S32 x, S32 y, scene_pixel_cb_t callback)
{
    if (!callback)
    {
        return;
    }
    mScenePixelX = x;
    mScenePixelY = y;
    mScenePixelCallback = std::move(callback);
    mScenePixelPending = true;
}

void LLPipeline::serviceScenePixelProbe(LLRenderTarget* src)
{
    if (!mScenePixelPending || !src || gCubeSnapshot)
    {
        return;
    }

    // Cleared before the callback runs, not after: the callback is free to ask
    // for another sample, and swallowing that request would make a tool that
    // stays armed sample exactly once.
    mScenePixelPending = false;
    scene_pixel_cb_t callback;
    callback.swap(mScenePixelCallback);

    // glReadPixels upsets nSight, which is why every other readback in the
    // tree is gated the same way. Nothing to report, so nothing is reported --
    // the caller has already been told the answer comes later, and a tool that
    // simply does not fire beats one that fires with a lie in it.
    if (LLRender::sNsightDebugSupport)
    {
        return;
    }

    // The mouse arrives in scaled window coordinates; the buffer is in raw
    // pixels, is only the world view rather than the whole window, and may be
    // rendered at a fraction of that again under resolution scaling. All three
    // conversions live in worldViewUV, in one place, because the scopes'
    // cursor readout needs exactly the same walk -- and the first version of
    // that one skipped the scaled-to-raw step, which is invisible on a machine
    // where the two spaces happen to match.
    F32 u = 0.f, v = 0.f;
    if (!worldViewUV(mScenePixelX, mScenePixelY, u, v))
    {
        // Clicked outside the 3D view. Nothing there to sample.
        return;
    }

    const S32 px = llclamp((S32)(u * (F32)src->getWidth()),  0, (S32)src->getWidth()  - 1);
    const S32 py = llclamp((S32)(v * (F32)src->getHeight()), 0, (S32)src->getHeight() - 1);

    F32 texel[4] = { 0.f, 0.f, 0.f, 0.f };
    src->bindTarget();
    glReadPixels(px, py, 1, 1, GL_RGBA, GL_FLOAT, texel);
    src->flush();

    // A synchronous read stalls the pipe. That is the right trade here: it
    // happens once, on a click, and a PBO round trip would mean holding the
    // request across frames for a result no one is waiting on but the person
    // who just clicked.
    callback(LLColor3(texel[0], texel[1], texel[2]));
}

void LLPipeline::releaseScopeBuffers()
{
    mScopeCaptures.release();
    mScopeSample.release();
    mScopeData.clear();
}

// static
bool LLPipeline::worldViewUV(S32 scaled_x, S32 scaled_y, F32& u, F32& v)
{
    // Everything here stays in scaled window space, and the answer comes out as
    // a fraction of the world view -- which is all a buffer covering that view
    // needs, whatever resolution it happens to be rendered at.
    //
    // The previous version converted the mouse to raw pixels by the *window's*
    // raw/scaled ratio and then measured it against the *world view's* raw
    // rect. Those are two different scalings, and mixing them leaves an error
    // proportional to the distance from the origin: dead on in one corner and
    // visibly adrift in the far one. Staying in one space cannot express that
    // mistake. It is also what the viewer itself does when it asks whether the
    // cursor is over the world (llviewerwindow.cpp, getWorldViewRectScaled
    // against mCurrentMousePoint).
    const LLRect world = gViewerWindow->getWorldViewRectScaled();
    if (world.getWidth() <= 0 || world.getHeight() <= 0)
    {
        return false;
    }

    u = (F32)(scaled_x - world.mLeft)   / (F32)world.getWidth();
    v = (F32)(scaled_y - world.mBottom) / (F32)world.getHeight();
    return (u >= 0.f && u <= 1.f && v >= 0.f && v <= 1.f);
}

bool LLPipeline::getScopePixel(S32 scaled_x, S32 scaled_y, LLColor4U& out) const
{
    const S32 width  = mScopeSample.getWidth();
    const S32 height = mScopeSample.getHeight();
    if (width <= 0 || height <= 0 || mScopeReadback.size() < (size_t)width * height * 4)
    {
        return false;
    }

    F32 u = 0.f, v = 0.f;
    if (!worldViewUV(scaled_x, scaled_y, u, v))
    {
        return false;
    }

    // The sample is a point decimation of the whole world view, so this is a
    // plain rescale with no letterboxing or crop to undo. Row 0 is the bottom:
    // glReadPixels fills bottom-up and the world rect counts y upwards too, so
    // v == 0 is the bottom on both sides and there is no flip.
    const S32 sx = llclamp((S32)(u * (F32)width),  0, width  - 1);
    const S32 sy = llclamp((S32)(v * (F32)height), 0, height - 1);

    const size_t at = ((size_t)sy * width + sx) * 4;
    out.set(mScopeReadback[at], mScopeReadback[at + 1], mScopeReadback[at + 2], 255);
    return true;
}

void LLPipeline::clearReferenceStill()
{
    mReferenceStill.release();
    mReferenceStillWanted = false;
}

void LLPipeline::captureReferenceStill(LLRenderTarget* src)
{
    if (!src || gCubeSnapshot)
    {
        return;
    }

    // A still taken at one resolution cannot honestly be compared against a
    // frame at another: sampled by UV it would stretch, and a reference you
    // cannot trust geometrically is worse than no reference at all. Drop it
    // and let the user grab again -- which is what a resize does to every
    // other full-resolution target here anyway.
    if (mReferenceStill.getWidth() != src->getWidth() ||
        mReferenceStill.getHeight() != src->getHeight())
    {
        mReferenceStill.release();
    }

    if (!mReferenceStillWanted)
    {
        return;
    }
    mReferenceStillWanted = false;

    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("reference still");

    if (mReferenceStill.getWidth() == 0)
    {
        // GL_RGB10_A2 whether or not HDR is on. It is what the post chain uses
        // in HDR and the same four bytes per pixel as the GL_RGBA8 it uses
        // otherwise, so this copy is exact in the first case and lossless in
        // the second, without having to track which one the source was.
        if (!mReferenceStill.allocate(src->getWidth(), src->getHeight(), GL_RGB10_A2))
        {
            LL_WARNS("Pipeline") << "Could not allocate the reference still target." << LL_ENDL;
            mReferenceStill.release();
            return;
        }
    }

    mReferenceStill.copyContents(*src, 0, 0, src->getWidth(), src->getHeight(),
                                 0, 0, mReferenceStill.getWidth(), mReferenceStill.getHeight(),
                                 GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void LLPipeline::captureScopeSample(LLRenderTarget* src)
{
    if (!sScopeCapture || !src || gCubeSnapshot)
    {
        return;
    }

    // Scopes are read, not watched. Sampling every frame would multiply the
    // cost by six for a display no one can follow that fast; hardware scopes
    // update at about this rate for the same reason.
    static LLCachedControl<F32> interval(gSavedSettings, "AlchemyScopeSampleInterval", 0.1f);
    if (mScopeSampleTimer.getStarted() && mScopeSampleTimer.getElapsedTimeF32() < llmax(interval(), 0.f))
    {
        return;
    }
    mScopeSampleTimer.reset();
    mScopeSampleTimer.start();

    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("scope sample");

    static LLCachedControl<S32> sample_width(gSavedSettings, "AlchemyScopeSampleWidth", 320);
    const S32 width = llclamp(sample_width(), 32, 1024);
    const F32 aspect = (F32)llmax(1U, src->getHeight()) / (F32)llmax(1U, src->getWidth());
    const S32 height = llclamp(ll_round((F32)width * aspect), 16, 1024);
    const S32 pixels = width * height;
    const size_t bytes = (size_t)pixels * 4;

    if (mScopeSample.getWidth() != (U32)width || mScopeSample.getHeight() != (U32)height)
    {
        // Resolution changed under us (window resize, or the debug key moved).
        // Anything in flight was measured against the old size, so drop it --
        // and the readback slots with it, since they were sized to that old
        // frame. Left alone, a grown sample would be read into slots too small
        // for it. The block below rebuilds them at the new size, exactly as it
        // built them the first time.
        mScopeSample.release();
        mScopeCaptures.release();
        // The last readback was laid out at the old stride. getScopePixel
        // indexes it against the target's dimensions, which are about to be
        // the new ones, so a shrink would read the wrong pixel until the next
        // collect. mScopeData stays: its bins are shares, still a true
        // measurement of the same scene, and clearing it would blank the plot.
        mScopeReadback.clear();
        if (!mScopeSample.allocate(width, height, GL_RGBA8))
        {
            sScopeCapture = false;
            LL_WARNS("Pipeline") << "Could not allocate the scope sample target; scopes disabled." << LL_ENDL;
            return;
        }
    }

    if (!mScopeCaptures.isAllocated() && !mScopeCaptures.allocate(bytes, 2, "Scope readback"))
    {
        sScopeCapture = false;
        LL_WARNS("Pipeline") << "Could not allocate the scope readback; scopes disabled." << LL_ENDL;
        return;
    }

    // Collect the previous capture first. It was issued a whole sample
    // interval ago -- many frames -- so it has all but always landed, and
    // collecting it waits on nothing. One that has not is left for the next
    // sample, and nothing is captured meanwhile: the plot can wait.
    if (mScopeCaptures.getPending() > 0)
    {
        const bool new_size = mScopeReadback.size() != bytes;
        mScopeReadback.resize(bytes);
        if (!mScopeCaptures.collect(mScopeReadback.data(), bytes))
        {
            // Nothing was written: what it held stays, and a store just sized
            // for the new frame holds nothing yet.
            if (new_size)
            {
                mScopeReadback.clear();
            }
            return;
        }

        ALScopeData fresh;
        fresh.accumulate(mScopeReadback.data(), width, height);

        // Ease towards the new measurement. A point sample of ~57k pixels
        // spread over 256 bins carries visible shot noise; without this
        // the plot shimmers even on a still frame.
        static LLCachedControl<F32> smoothing(gSavedSettings, "AlchemyScopeSmoothing", 0.5f);
        mScopeData.blendToward(fresh, llclamp(smoothing(), 0.f, 1.f));
    }

    // GL_NEAREST on a downscaling blit picks one source texel per destination
    // texel: a point decimation of the frame, not an average of it. That
    // distinction is the whole point. A box filter would pull every value
    // toward the local mean, narrowing the histogram and erasing both tails --
    // so a small blown highlight, the thing a photographer most wants to be
    // warned about, would simply vanish into its neighbours.
    mScopeSample.copyContents(*src, 0, 0, src->getWidth(), src->getHeight(),
                              0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // No capture is in flight here, so one of the two slots is free and the
    // request is never refused.
    mScopeSample.bindTarget();
    mScopeCaptures.requestPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bytes);
    mScopeSample.flush();
}

void LLPipeline::generateLuminance(LLRenderTarget* src, LLRenderTarget* dst)
{
    // The exposure meter's grid (luminanceF.glsl), from the scene before bloom: metering a
    // buffer the exposure already shaped would feed the exposure back into itself.
    LL_PROFILE_GPU_ZONE("exposure meter grid");

    dst->bindTarget();

    LLGLDepthTest depth(GL_FALSE, GL_FALSE);

    gLuminanceProgram.bind();

    S32 channel = gLuminanceProgram.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);
    if (channel > -1)
    {
        src->bindTexture(0, channel, ALSamplers::BilinearClamp);
    }

    channel = gLuminanceProgram.enableTexture(LLShaderMgr::NORMAL_MAP);
    if (channel > -1)
    {
        // the G-buffer normals carry the flags that mark the sky
        mRT->deferredScreen.bindTexture(2, channel, ALSamplers::PointClamp);
    }

    // A bilinear tap averages a 2x2 block, so taps two texels apart cover a patch.
    const F32 patch_w = (F32)src->getWidth() / (F32)dst->getWidth();
    const F32 patch_h = (F32)src->getHeight() / (F32)dst->getHeight();
    gLuminanceProgram.uniform4f(LLShaderMgr::METER_PARAMS, patch_w, patch_h,
                                (F32)llmax(1, llceil(patch_w * 0.5f)), (F32)llmax(1, llceil(patch_h * 0.5f)));

    mScreenTriangleVB->setBuffer();
    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    gLuminanceProgram.unbind();
    dst->flush();
}

void LLPipeline::generateExposure(LLRenderTarget* src, LLRenderTarget* dst, bool use_history)
{
    LL_PROFILE_GPU_ZONE("exposure");

    static LLCachedControl<bool> dynamic_exposure_enabled(gSavedSettings, "RenderDynamicExposureEnabled", true);
    static LLCachedControl<F32> compensation(gSavedSettings, "AlchemyExposureCompensation", 0.f);
    static LLCachedControl<F32> min_ev(gSavedSettings, "AlchemyExposureMinEV", -3.f);
    static LLCachedControl<F32> max_ev(gSavedSettings, "AlchemyExposureMaxEV", 4.f);
    static LLCachedControl<F32> low_percent(gSavedSettings, "AlchemyExposureLowPercent", 10.f);
    static LLCachedControl<F32> high_percent(gSavedSettings, "AlchemyExposureHighPercent", 95.f);
    static LLCachedControl<F32> speed_up(gSavedSettings, "AlchemyExposureSpeedUp", 3.f);
    static LLCachedControl<F32> speed_down(gSavedSettings, "AlchemyExposureSpeedDown", 1.f);
    static LLCachedControl<U32> metering_mode(gSavedSettings, "AlchemyExposureMeteringMode", 1);
    static LLCachedControl<F32> sky_weight(gSavedSettings, "AlchemyExposureSkyWeight", 1.f);

    // A classic sky reproduces pre-PBR lighting, which assumed a fixed exposure.
    const bool meter = dynamic_exposure_enabled && !LLRender::sClassicMode;

    if (meter)
    {
        // The histogram (exposureHistogramV.glsl): the meter grid's points, summed per bin.
        LL_PROFILE_GPU_ZONE("exposure histogram");

        mExposureHistogram.bindTarget();
        mExposureHistogram.clear();

        LLGLDepthTest depth(GL_FALSE, GL_FALSE);
        LLGLEnable blend(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ADD);

        gExposureHistogramProgram.bind();
        S32 channel = gExposureHistogramProgram.enableTexture(LLShaderMgr::METER_MAP);
        if (channel > -1)
        {
            src->bindTexture(0, channel, ALSamplers::PointClamp);
        }

        const F32 bins_per_ev = (F32)EXPOSURE_HISTOGRAM_BINS / (EXPOSURE_HISTOGRAM_MAX_EV - EXPOSURE_HISTOGRAM_MIN_EV);
        gExposureHistogramProgram.uniform4f(LLShaderMgr::METER_PARAMS, (F32)metering_mode(), llmax(sky_weight(), 0.f),
                                            EXPOSURE_HISTOGRAM_MIN_EV, bins_per_ev);

        mExposureHistogramVB->setBuffer();
        mExposureHistogramVB->drawArrays(LLRender::POINTS, 0, mExposureHistogramVB->getNumVerts());

        gExposureHistogramProgram.unbind();
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
        mExposureHistogram.flush();
    }

    if (use_history && dst->isComplete() && mLastExposure.isComplete())
    {
        // Last frame's exposure, which this one adapts from, is what the target holds: the two trade their storage, and
        // this frame's is drawn whole into what was the frame before's.
        dst->swapFBORefs(mLastExposure);
    }

    dst->bindTarget();

    LLGLDepthTest depth(GL_FALSE, GL_FALSE);

    LLGLSLShader* shader = use_history ? &gExposureProgram : &gExposureProgramNoFade;
    shader->bind();

    S32 channel = shader->enableTexture(LLShaderMgr::EXPOSURE_HISTOGRAM);
    if (channel > -1)
    {
        mExposureHistogram.bindTexture(0, channel, ALSamplers::PointClamp);
    }

    S32 history_channel = -1;
    if (use_history)
    {
        history_channel = shader->enableTexture(LLShaderMgr::EXPOSURE_MAP);
        if (history_channel > -1)
        {
            mLastExposure.bindTexture(0, history_channel);
        }
    }

    // At least a percent of the weight between the trims, or nothing would be kept.
    const F32 low = llclamp(low_percent() * 0.01f, 0.f, 0.99f);
    const F32 high = llclamp(high_percent() * 0.01f, low + 0.01f, 1.f);
    shader->uniform1f(LLShaderMgr::DT, gFrameIntervalSeconds);
    shader->uniform4f(LLShaderMgr::DYNAMIC_EXPOSURE_PARAMS, log2f(EXPOSURE_KEY) + compensation(), llmin(min_ev(), max_ev()),
                      llmax(min_ev(), max_ev()), meter ? 1.f : 0.f);
    shader->uniform4f(LLShaderMgr::DYNAMIC_EXPOSURE_PARAMS2, low, high, llmax(speed_up(), 0.f), llmax(speed_down(), 0.f));

    mScreenTriangleVB->setBuffer();
    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    if (history_channel > -1)
    {
        gGL.getTextureSlot(history_channel)->unbind();
    }
    shader->unbind();
    dst->flush();
}

void LLPipeline::clearLensFlareState()
{
    // Only [0] is ever read as history: the swap in generateLensFlareState
    // makes it last frame's target before anything samples it, and [1] is
    // fully overwritten by the draw before it is read.
    mLensFlareState[0].bindTarget();
    mLensFlareState[0].clear();
    mLensFlareState[0].flush();
    mLensFlareStateValid = false;
}

// Sun coverage, colour and the temporally filtered flare drive, into a 2x1
// target the colour-correct programs read through uLensFlareStateMap. The
// only place the flare's visibility changes, at a bounded rate; the filter
// and the reasons for it are in lensFlareStateF.glsl. Runs right before
// colorCorrect, against the frame's final depth, in both the HDR and
// non-HDR paths.
void LLPipeline::generateLensFlareState(LLRenderTarget* src)
{
    static LLCachedControl<F32> lens_flare_strength(gSavedSettings, "RenderLensFlareStrength", 0.f);
    static LLCachedControl<F32> lens_flare_occlusion_scale(gSavedSettings, "RenderLensFlareOcclusionScale", 1.f);
    static LLCachedControl<F32> lens_flare_fade_time(gSavedSettings, "RenderLensFlareFadeTime", 0.35f);

    if (suppressesLensAndPrintEffects())
    {
        // A clean-plate render: colorCorrect zeroes the strength for this
        // frame, so hold the history rather than clear it, or the flare would
        // blink out and fade back in after every no-post snapshot.
        return;
    }

    if (lens_flare_strength() <= 0.f || !gLensFlareStateProgram.isComplete())
    {
        // Switched off, or nothing to run it with: leave no history behind,
        // or a stale drive would flash back on the frame the flare returns.
        if (mLensFlareStateValid)
        {
            clearLensFlareState();
        }
        return;
    }

    LL_PROFILE_GPU_ZONE("lens flare state");

    // Sun (or moon) direction to screen UV, with a soft fade over a margin
    // beyond the frame so the streak survives a sun just out of view.
    // Gate/divide on clip w, not z: w is convention-independent (reverse-Z
    // rewrites only the projection's z row), so w > 0 == "sun in front" and
    // xy/w == ndc under both conventions; z would flip sign under reverse-Z.
    LLEnvironment& environment = LLEnvironment::instance();
    const bool sun_up = environment.getIsSunUp();
    if (sun_up != mLensFlareSunUp)
    {
        // The history holds the old body's colour while the anchor moves to
        // the new one this frame; a sun flare fading out on the moon is wrong,
        // so start over.
        mLensFlareSunUp = sun_up;
        if (mLensFlareStateValid)
        {
            clearLensFlareState();
        }
    }
    const LLVector4 light_dir = sun_up ? mSunDir : mMoonDir;
    LLVector4a sun_eye, sun_clip;
    LLViewerCamera::getCurrent().getModelview().rotate(LLVector4a(light_dir.mV[0], light_dir.mV[1], light_dir.mV[2], 0.f), sun_eye);
    LLViewerCamera::getCurrent().getProjection().transform4(sun_eye, sun_clip);
    F32 edge_fade = 0.f;
    if (sun_clip[3] > 0.f)
    {
        const LLVector2 sun_ndc(sun_clip[0] / sun_clip[3], sun_clip[1] / sun_clip[3]);
        const LLVector2 sun_uv = sun_ndc * 0.5f + LLVector2(0.5f, 0.5f);
        constexpr F32 margin = 0.2f;
        edge_fade = llclamp((sun_uv.mV[VX] + margin) / margin, 0.f, 1.f)
                  * llclamp(((1.f + margin) - sun_uv.mV[VX]) / margin, 0.f, 1.f)
                  * llclamp((sun_uv.mV[VY] + margin) / margin, 0.f, 1.f)
                  * llclamp(((1.f + margin) - sun_uv.mV[VY]) / margin, 0.f, 1.f);
        mLensFlareSunUV = sun_uv;
    }

    // Probe radius as a fraction of screen height: the body's own angular
    // radius (updateHeavenlyBodyGeometry: HEAVENLY_BODY_FACTOR x disk radius
    // x sky scale, at unit distance) through the current vertical FOV, times
    // the setting. It follows zoom and the sky's sun scale, so an occluder
    // narrower than the disc dims the flare instead of cutting it.
    LLSettingsSky::ptr_t psky = environment.getCurrentSky();
    F32 disk_radius = 0.5f;
    if (gSky.mVOSkyp.notNull())
    {
        disk_radius = (sun_up ? gSky.mVOSkyp->getSun() : gSky.mVOSkyp->getMoon()).getDiskRadius();
    }
    const F32 body_scale = llmax(sun_up ? psky->getSunScale() : psky->getMoonScale(), 0.01f);
    // The drawn quad grows towards the horizon (1.3x wide and 1.2x tall at
    // dir.z = 0, llvosky.cpp); a circular probe takes the mean of the two.
    const F32 enlargement = 1.f + (1.f - light_dir.mV[VZ]) * 0.25f;
    const F32 half_tan    = HEAVENLY_BODY_FACTOR * disk_radius * body_scale * enlargement;
    const LLViewerCamera* camera = LLViewerCamera::getInstance();
    const F32 fov_y       = llclamp(camera->getView(), 0.01f, F_PI - 0.01f);
    // A tiled snapshot zooms the projection without touching the FOV, and the
    // radius has to follow the projection the centre was put through. Capped
    // so a wide sky sun scale at a narrow FOV cannot probe half the frame.
    const F32 radius_uv   = llmin(0.5f * half_tan / tanf(fov_y * 0.5f) * llmax(camera->getZoomFactor(), 1.f)
                                  * llclamp(lens_flare_occlusion_scale(), 0.25f, 2.f), 0.3f);

    // The shader derives its time constants and slew from FadeTime itself, so
    // the filter is defined in one file and its mirror can check it.
    const F32 fade = llclamp(lens_flare_fade_time(), 0.1f, 1.f);

    // Ping-pong by swapping handles: [1] becomes last frame's state, the
    // history this frame reads, and [0] is fully overwritten below.
    mLensFlareState[0].swapFBORefs(mLensFlareState[1]);

    mLensFlareState[0].bindTarget();
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);

    LLGLSLShader& shader = gLensFlareStateProgram;
    shader.bind();
    const S32 diffuse_channel = shader.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, ALSamplers::PointMirror);
    const S32 depth_channel   = shader.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mRT->deferredScreen);
    const S32 state_channel   = shader.bindTexture(LLShaderMgr::LENS_FLARE_STATE_MAP, &mLensFlareState[1], ALSamplers::PointClamp);

    shader.uniform1f(LLShaderMgr::DT, gFrameIntervalSeconds);
    shader.uniform2f(LLShaderMgr::SCREEN_RESOLUTION, (GLfloat)src->getWidth(), (GLfloat)src->getHeight());
    shader.uniform2f(LLShaderMgr::LENS_FLARE_SUN_POS, mLensFlareSunUV.mV[VX], mLensFlareSunUV.mV[VY]);
    shader.uniform1f(LLShaderMgr::LENS_FLARE_SUN_VISIBILITY, edge_fade);
    shader.uniform1f(LLShaderMgr::LENS_FLARE_OCCLUSION_RADIUS, radius_uv);
    shader.uniform1f(LLShaderMgr::LENS_FLARE_FADE_TIME, fade);

    mScreenTriangleVB->setBuffer();
    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    if (state_channel > -1)
    {
        gGL.getTextureSlot(state_channel)->unbind();
    }
    if (depth_channel > -1)
    {
        gGL.getTextureSlot(depth_channel)->unbind();
    }
    if (diffuse_channel > -1)
    {
        gGL.getTextureSlot(diffuse_channel)->unbind();
    }
    shader.unbind();
    mLensFlareState[0].flush();
    mLensFlareStateValid = true;
}

void LLPipeline::colorCorrect(LLRenderTarget* src, LLRenderTarget* dst, bool apply_tonemap, bool apply_color_grade,
                              const Bloom* bloom)
{
    LL_PROFILE_GPU_ZONE("colorcorrect");

    if (mToneCurveLutDirty)
    {
        // Borrows slot 0, so it has to run before anything below is bound.
        bakeToneCurveLut();
    }

    dst->bindTarget();
    {
        LLGLDepthTest depth(GL_FALSE, GL_FALSE);

        // Apply gamma correction to the frame here.
        static LLCachedControl<bool> color_grade_cc(gSavedSettings, "RenderColorGrade", false);
        static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
        static LLCachedControl<bool> buildNoPost(gSavedSettings, "RenderDisablePostProcessing", false);

        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

        // sGradeBypass is the hold-to-compare key. Both shader variants are
        // already built and bound by this same branch, so suppressing the
        // grade is a rebind rather than a recompile -- free, and instant on
        // the frame the key goes down.
        // gSnapshotNoPost is the snapshot floater's "No post-processing" box,
        // and it only ever selected a non-tonemap variant -- leaving the grade,
        // which is the most post-processing thing in the whole chain, running
        // on a frame that had asked for none of it.
        //
        // legacy_gamma deliberately does not do this. A legacy sky drops
        // tonemapping because it predates it, not because anybody asked for a
        // clean plate, and the user's grade still applies there.
        bool color_grade = apply_color_grade && color_grade_cc && !sGradeBypass && !gSnapshotNoPost;
        bool legacy_gamma = psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f;
        bool no_post = gSnapshotNoPost || legacy_gamma || (buildNoPost && gFloaterTools && gFloaterTools->isAvailable());
        LLGLSLShader* shader = nullptr;
        if (apply_tonemap)
        {
            if (legacy_gamma)
            {
                shader = no_post       ? color_grade ? &gCGColorgradeLegacyGammaProgram : &gCGLegacyGammaProgram
                         : color_grade ? &gCGTonemapColorgradeLegacyGammaProgram
                                       : &gCGTonemapLegacyGammaProgram;
            }
            else
            {
                shader = no_post       ? color_grade ? &gCGColorgradeGammaProgram : &gCGGammaProgram
                         : color_grade ? &gCGTonemapColorgradeProgram
                                       : &gCGTonemapProgram;
            }
        }
        else
        {
            shader = legacy_gamma ? color_grade ? &gCGColorgradeLegacyGammaProgram : &gCGLegacyGammaProgram
                                  : color_grade ? &gCGColorgradeGammaProgram : &gCGGammaProgram;
        }

        shader->bind();

        // Point sampled, except under chromatic aberration: its red and blue
        // taps fall between texels, where point sampling would step the fringe
        // a whole pixel at a time. Its centre tap, and the only tap without it,
        // land on texel centres either way, src being the size of dst.
        static LLCachedControl<F32> chromatic_aberration_strength(gSavedSettings, "RenderChromaticAberrationStrength", 0.f);
        const bool suppress_effects = suppressesLensAndPrintEffects();
        const F32  ca_strength      = suppress_effects ? 0.f : llclamp(chromatic_aberration_strength(), 0.f, 1.f);
        S32 diffuse_channel = shader->bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src,
                                                  ca_strength > 0.f ? ALSamplers::BilinearMirror : ALSamplers::PointMirror);
        S32 exposure_channel = shader->bindTexture(LLShaderMgr::EXPOSURE_MAP, &mExposureMap);

        // HDR bloom pyramid is folded into the tonemap variants of this shader
        // (permutation BLOOM_COMPOSITE). Bind the pyramid top + strength here so
        // the composite no longer needs its own pass. When HDR is off the shader
        // variant lacks the sampler and bindTexture is a no-op via getTextureChannel.
        //
        // A frame that made no pyramid -- a cutout frame makes none -- binds
        // black in its place at no strength, for the variants that sample it
        // whatever the strength.
        const bool bloomed = bloom && bloom->mMipCount > 0;
        S32 bloom_channel = bloomed ? shader->bindTexture(LLShaderMgr::BLOOM_SAMPLER, bloom->mMips[0], ALSamplers::BilinearMirror)
                                    : shader->bindTexture(LLShaderMgr::BLOOM_SAMPLER, LLViewerFetchedTexture::sBlackImagep,
                                                          ALSamplers::BilinearMirror);
        S32 cross_channel = -1;
        S32 state_channel = -1;
        if (bloom_channel > -1)
        {
            static LLCachedControl<F32>      bloom_strength(gSavedSettings, "RenderBloomStrength", 0.325f);
            static LLCachedControl<F32>      halation_strength(gSavedSettings, "RenderBloomHalationStrength", 0.0f);
            static LLCachedControl<LLColor3> halation_tint(gSavedSettings, "RenderBloomHalationTint", LLColor3(1.0f, 0.35f, 0.15f));
            // generateBloomHDR makes none shallower than the three levels its
            // downsample and upsample chain needs to mean anything.
            const F32 strength_gate = bloomed ? 1.0f : 0.0f;
            shader->uniform1f(LLShaderMgr::BLOOM_STRENGTH,    llmax(bloom_strength(), 0.0f)    * strength_gate);
            shader->uniform1f(LLShaderMgr::HALATION_STRENGTH, llmax(halation_strength(), 0.0f) * strength_gate);
            const LLColor3& tint = halation_tint();
            shader->uniform3f(LLShaderMgr::HALATION_TINT, tint.mV[0], tint.mV[1], tint.mV[2]);

            // Cross-filter streaks fold in here rather than in a fullscreen
            // pass of their own. That pass existed only to add a half-size
            // buffer into bloomMip[0], which cost a full-resolution
            // read-modify-write of the pyramid top every frame; this pass
            // already samples that pyramid, so one more sampler replaces all
            // of it.
            //
            // Added to bloom_term inside the shader rather than to the scene
            // directly, which keeps two couplings that were previously free:
            // the streaks stay scaled by bloom strength, and they keep
            // lighting the lens dirt through lens_light.
            static LLCachedControl<F32> streak_strength_setting(gSavedSettings, "RenderCrossFilterStrength", 0.f);
            // Drawn this frame: generateBloomHDR hands back the sum only
            // where it drew the arms.
            const bool streaks_live = bloom && bloom->mStreaks;
            const F32  streaks      = (streaks_live && !suppressesLensAndPrintEffects())
                                    ? llclamp(streak_strength_setting(), 0.f, CROSS_FILTER_MAX_STRENGTH) * strength_gate
                                    : 0.f;
            shader->uniform1f(LLShaderMgr::CROSS_STRENGTH, streaks);
            if (streaks > 0.f)
            {
                cross_channel = shader->bindTexture(LLShaderMgr::CROSS_FILTER_MAP,
                                                    bloom->mStreaks,
                                                    ALSamplers::BilinearClamp);
            }
        }

        shader->uniform2f(LLShaderMgr::SCREEN_RESOLUTION, (GLfloat)src->getWidth(), (GLfloat)src->getHeight());
        shader->uniform1f(LLShaderMgr::CUTOUT_PREMULTIPLIED, sCutoutRender ? 1.f : 0.f);

        // Without HDR these programs carry DITHER and the final blit does not
        // (LLViewerShaderMgr), since dst is the post chain's first 8-bit target.
        // With HDR nothing reads the dither uniforms and their sets fall away.
        static LLCachedControl<bool> dither_enabled(gSavedSettings, "RenderDitherEnabled", true);
        static LLCachedControl<bool> dither_animated(gSavedSettings, "RenderDitherAnimated", true);
        shader->uniform1f(LLShaderMgr::DITHER_AMOUNT, dither_enabled() ? 1.0f : 0.0f);
        shader->uniform1i(LLShaderMgr::DITHER_BITS, 8);
        shader->uniform1i(LLShaderMgr::DITHER_ANIMATE, dither_animated ? 1 : 0);
        shader->uniform1ui(LLShaderMgr::FRAME_ID, LLFrameTimer::getFrameCount());

        // The other half of the "No post-processing" contract. The grade is
        // gated where this pass's program is chosen and the print effects in
        // the final blit are gated in renderFinalize -- but chromatic
        // aberration and the lens flare are applied *inside* this program, in
        // every variant including the no-post ones, so their strengths have to
        // be silenced here or a clean plate still carries both (suppress_effects,
        // above).
        // The material preview renders through here too, inside the auxiliary
        // target pack. The flare state was measured for the world frame, so
        // only the main pack gets the flare; aberration is left as it was.
        const bool world_frame = (mRT == &mMainRT);

        // Chromatic aberration parameters
        static LLCachedControl<F32> chromatic_aberration_falloff(gSavedSettings, "RenderChromaticAberrationFalloff", 1.f);
        static LLCachedControl<F32> chromatic_aberration_angle(gSavedSettings, "RenderChromaticAberrationAngle", 0.f);
        static LLCachedControl<F32> chromatic_aberration_offset_r_x(gSavedSettings, "RenderChromaticAberrationOffsetRX", -1.f);
        static LLCachedControl<F32> chromatic_aberration_offset_r_y(gSavedSettings, "RenderChromaticAberrationOffsetRY", 0.f);
        static LLCachedControl<F32> chromatic_aberration_offset_b_x(gSavedSettings, "RenderChromaticAberrationOffsetBX", 1.f);
        static LLCachedControl<F32> chromatic_aberration_offset_b_y(gSavedSettings, "RenderChromaticAberrationOffsetBY", 0.f);
        static LLCachedControl<F32> chromatic_aberration_anisotropy(gSavedSettings, "RenderChromaticAberrationAnisotropy", 0.f);
        // Precompute shader-friendly forms once on the CPU: square the
        // strength (and pre-apply the 0.02 peak-offset scale), take the
        // reciprocal of falloff, and turn the angle into a (sin, cos) pair.
        // Saves one mul, one div, and one sincos per fragment.
        F32 ca_falloff  = llclamp(chromatic_aberration_falloff(),  0.5f, 4.f);
        F32 ca_angle_rad = llclamp(chromatic_aberration_angle(), 0.f, 360.f) * 0.01745329252f;
        shader->uniform1f(LLShaderMgr::CA_AMOUNT, ca_strength * ca_strength * 0.02f);
        shader->uniform1f(LLShaderMgr::CA_FALLOFF, 1.f / ca_falloff);
        shader->uniform2f(LLShaderMgr::CA_ANGLE_SIN_COS, sinf(ca_angle_rad), cosf(ca_angle_rad));
        shader->uniform2f(LLShaderMgr::CA_OFFSET_R, llclamp(chromatic_aberration_offset_r_x(), -1.f, 1.f), llclamp(chromatic_aberration_offset_r_y(), -1.f, 1.f));
        shader->uniform2f(LLShaderMgr::CA_OFFSET_B, llclamp(chromatic_aberration_offset_b_x(), -1.f, 1.f), llclamp(chromatic_aberration_offset_b_y(), -1.f, 1.f));
        shader->uniform1f(LLShaderMgr::CA_ANISOTROPY, llclamp(chromatic_aberration_anisotropy(), -1.f, 1.f));

        // Lens flare parameters
        {
            static LLCachedControl<F32> lens_flare_strength(gSavedSettings, "RenderLensFlareStrength", 0.f);
            static LLCachedControl<F32> lens_flare_streak_length(gSavedSettings, "RenderLensFlareStreakLength", 0.5f);
            static LLCachedControl<F32> lens_flare_streak_falloff(gSavedSettings, "RenderLensFlareStreakFalloff", 1.5f);
            static LLCachedControl<F32> lens_flare_streak_thickness(gSavedSettings, "RenderLensFlareStreakThickness", 0.08f);
            static LLCachedControl<F32> lens_flare_streak_intensity(gSavedSettings, "RenderLensFlareStreakIntensity", 1.f);
            static LLCachedControl<LLColor3> lens_flare_streak_tint(gSavedSettings, "RenderLensFlareStreakTint", LLColor3(0.6f, 0.7f, 1.0f));
            static LLCachedControl<F32> lens_flare_chromatic_spread(gSavedSettings, "RenderLensFlareChromaticSpread", 0.08f);
            static LLCachedControl<F32> lens_flare_glow_radius(gSavedSettings, "RenderLensFlareGlowRadius", 0.12f);
            static LLCachedControl<F32> lens_flare_glow_falloff(gSavedSettings, "RenderLensFlareGlowFalloff", 8.f);
            static LLCachedControl<F32> lens_flare_glow(gSavedSettings, "RenderLensFlareGlow", 1.f);
            static LLCachedControl<F32> lens_flare_ghost(gSavedSettings, "RenderLensFlareGhost", 0.f);
            static LLCachedControl<S32> lens_flare_ghost_count(gSavedSettings, "RenderLensFlareGhostCount", 4);
            static LLCachedControl<F32> lens_flare_ghost_spacing(gSavedSettings, "RenderLensFlareGhostSpacing", 0.3f);
            static LLCachedControl<F32> lens_flare_halo(gSavedSettings, "RenderLensFlareHalo", 0.f);
            static LLCachedControl<F32> lens_flare_halo_radius(gSavedSettings, "RenderLensFlareHaloRadius", 0.5f);
            static LLCachedControl<F32> lens_flare_halo_width(gSavedSettings, "RenderLensFlareHaloWidth", 0.15f);
            static LLCachedControl<F32> lens_flare_starburst(gSavedSettings, "RenderLensFlareStarburst", 0.f);
            static LLCachedControl<S32> lens_flare_starburst_spikes(gSavedSettings, "RenderLensFlareStarburstSpikes", 4);
            static LLCachedControl<F32> lens_flare_starburst_sharpness(gSavedSettings, "RenderLensFlareStarburstSharpness", 24.f);
            static LLCachedControl<F32> lens_flare_starburst_length(gSavedSettings, "RenderLensFlareStarburstLength", 0.25f);

            // Zeroing the master strength both hits the shader's early-out and
            // skips the whole detail block below.
            F32 strength = (suppress_effects || !world_frame) ? 0.f : llclamp(lens_flare_strength(), 0.f, 1.f);
            shader->uniform1f(LLShaderMgr::LENS_FLARE_STRENGTH, strength);

            if (strength > 0.f)
            {
                // Sun UV and the filtered drive both come from generateLensFlareState.
                shader->uniform2f(LLShaderMgr::LENS_FLARE_SUN_POS, mLensFlareSunUV.mV[VX], mLensFlareSunUV.mV[VY]);
                state_channel = shader->bindTexture(LLShaderMgr::LENS_FLARE_STATE_MAP, &mLensFlareState[0], ALSamplers::PointClamp);

                // Anamorphic streak
                shader->uniform1f(LLShaderMgr::LENS_FLARE_STREAK_LENGTH, llclamp(lens_flare_streak_length(), 0.01f, 2.f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_STREAK_FALLOFF, llclamp(lens_flare_streak_falloff(), 0.1f, 10.f));
                // User-facing "thickness" (0-1) maps to the shader's vertical half-thickness in UV space.
                F32 streak_thickness = llclamp(lens_flare_streak_thickness(), 0.f, 1.f) * 0.05f;
                shader->uniform1f(LLShaderMgr::LENS_FLARE_STREAK_WIDTH, llmax(streak_thickness, 0.001f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_STREAK_INTENSITY, llclamp(lens_flare_streak_intensity(), 0.f, 5.f));
                LLColor3 tint = linearColor3(lens_flare_streak_tint());
                shader->uniform3f(LLShaderMgr::LENS_FLARE_STREAK_TINT, tint.mV[0], tint.mV[1], tint.mV[2]);
                // User-facing 0-1 spread maps to shader's internal UV offset (0-0.1).
                shader->uniform1f(LLShaderMgr::LENS_FLARE_CHROMATIC_SPREAD, llclamp(lens_flare_chromatic_spread(), 0.f, 1.f) * 0.1f);

                // Central glow
                shader->uniform1f(LLShaderMgr::LENS_FLARE_GLOW_RADIUS, llclamp(lens_flare_glow_radius(), 0.01f, 0.5f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_GLOW_FALLOFF, llclamp(lens_flare_glow_falloff(), 1.f, 30.f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_GLOW, llclamp(lens_flare_glow(), 0.f, 5.f));

                // Optional ghosts & halo — intensity doubles as the on/off switch.
                shader->uniform1f(LLShaderMgr::LENS_FLARE_GHOST, llclamp(lens_flare_ghost(), 0.f, 5.f));
                shader->uniform1i(LLShaderMgr::LENS_FLARE_GHOST_COUNT, llclamp(lens_flare_ghost_count(), 0, 8));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_GHOST_SPACING, llclamp(lens_flare_ghost_spacing(), 0.1f, 1.f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_HALO, llclamp(lens_flare_halo(), 0.f, 5.f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_HALO_RADIUS, llclamp(lens_flare_halo_radius(), 0.01f, 1.f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_HALO_WIDTH, llclamp(lens_flare_halo_width(), 0.01f, 0.5f));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_STARBURST, llclamp(lens_flare_starburst(), 0.f, 5.f));
                shader->uniform1i(LLShaderMgr::LENS_FLARE_STARBURST_SPIKES, llclamp(lens_flare_starburst_spikes(), 1, 32));
                shader->uniform1f(LLShaderMgr::LENS_FLARE_STARBURST_SHARPNESS, llclamp(lens_flare_starburst_sharpness(), 1.f, 256.f));
                // User-facing "length" (0 = short, 1 = long spikes) maps inversely to the shader's
                // exponential radial falloff rate. Use a reciprocal so the perceived spike extent
                // is roughly linear in the control; the +0.05 keeps falloff finite at length=0.
                F32 starburst_length = llclamp(lens_flare_starburst_length(), 0.f, 1.f);
                F32 starburst_falloff = 4.f / (starburst_length + 0.05f);
                shader->uniform1f(LLShaderMgr::LENS_FLARE_STARBURST_FALLOFF, starburst_falloff);

                const bool sun_up = LLEnvironment::instance().getIsSunUp();
                LLColor4 light_color = linearColor3(sun_up ? mSunDiffuse : mMoonDiffuse);
                shader->uniform3f(LLShaderMgr::LENS_FLARE_LIGHT_COLOR, light_color.mV[0], light_color.mV[1], light_color.mV[2]);
            }
        }

        // Lens dirt
        //
        // Forced off whenever there is no plate -- generateLensDirt allocates
        // only while the effect is on, and gives the memory back when it is
        // not -- so the shader's early-out fires and the sampler is never read
        // unbound, the same guard the grading LUT and the reference still use.
        // Also off under a clean plate: dirt is a look, not a quantisation aid.
        // And only on the world frame, like the flare: the plate is made at
        // the screen's shape, and the material preview is not that shape.
        S32 dirt_channel = -1;
        {
            static LLCachedControl<F32> lens_dirt_strength(gSavedSettings, "RenderLensDirtStrength", 0.f);
            static LLCachedControl<F32> lens_dirt_bloom(gSavedSettings, "RenderLensDirtBloomResponse", 1.f);
            static LLCachedControl<F32> lens_dirt_flare(gSavedSettings, "RenderLensDirtFlareResponse", 1.f);

            const F32 dirt_strength = (suppress_effects || !world_frame || !mLensDirtMap.isComplete())
                                    ? 0.f
                                    : llclamp(lens_dirt_strength(), 0.f, 2.f);

            shader->uniform1f(LLShaderMgr::LENS_DIRT_STRENGTH, dirt_strength);
            shader->uniform1f(LLShaderMgr::LENS_DIRT_BLOOM_RESPONSE, llclamp(lens_dirt_bloom(), 0.f, 2.f));
            shader->uniform1f(LLShaderMgr::LENS_DIRT_FLARE_RESPONSE, llclamp(lens_dirt_flare(), 0.f, 2.f));

            if (dirt_strength > 0.f)
            {
                dirt_channel = shader->bindTexture(LLShaderMgr::LENS_DIRT_MAP,
                                                   &mLensDirtMap,
                                                   ALSamplers::BilinearClamp);
            }
        }

        if (apply_tonemap)
        {
            // Exposure parameters
            static LLCachedControl<F32> exposure(gSavedSettings, "RenderExposure", 1.f);
            shader->uniform1f(LLShaderMgr::EXPOSURE, llclamp(exposure(), 0.5f, 4.f));

            // Tonemap type and parameters
            static LLCachedControl<S32> tonemap_type_setting(gSavedSettings, "AlchemyRenderTonemapType", 0U);
            shader->uniform1i(LLShaderMgr::TONEMAP_TYPE, tonemap_type_setting);
            shader->uniform1f(LLShaderMgr::TONEMAP_MIX, psky->getTonemapMix(should_auto_adjust()));

            constexpr F32 max_screen_brightness = 1.f;
            switch (tonemap_type_setting)
            {
                case 2: // ACES Godot
                {
                    static LLCachedControl<F32> tonemap_aces_white(gSavedSettings, "RenderTonemapACESWhite", 6.f);
                    F32                         white = llmax(1.0f, tonemap_aces_white());

                    // These constants must match those in the shader code.
                    const float exposure_bias = 1.2f;
                    const float A             = 0.0245786f;
                    const float B             = 0.000090537f;
                    const float C             = 0.983729f;
                    const float D             = 0.432951f;
                    const float E             = 0.238081f;

                    white *= exposure_bias;
                    float white_tonemapped = (white * (white + A) - B) / (white * (C * white + D) + E);
                    shader->uniform4f(LLShaderMgr::TONEMAP_PARAMS, white_tonemapped, 0.f, 0.f, 0.f);
                    break;
                }
                case 3: // Reinhard
                {
                    // The Reinhard tonemapper is not designed to have a white parameter
                    // that is less than the output max value. This is especially important
                    // in the variable Extended Dynamic Range (EDR) paradigm where the
                    // output max value may change to be greater or less than the white
                    // parameter, depending on the available dynamic range.
                    static LLCachedControl<F32> tonemap_reinhard_white(gSavedSettings, "RenderTonemapReinhardWhite", 6.f);

                    F32 white         = llmax(max_screen_brightness, tonemap_reinhard_white());
                    F32 white_squared = (white * white) / max_screen_brightness;

                    shader->uniform4f(LLShaderMgr::TONEMAP_PARAMS, white_squared, 0.f, 0.f, 0.f);
                    break;
                }
                case 4: // Filmic
                {
                    static LLCachedControl<F32> tonemap_filmic_white(gSavedSettings, "RenderTonemapFilmicWhite", 6.f);
                    F32                         white = llmax(1.0f, tonemap_filmic_white());

                    // These constants must match those in the shader code.
                    const float exposure_bias = 2.0f;
                    const float A             = 0.22f * exposure_bias * exposure_bias; // bias baked into constants for performance
                    const float B             = 0.30f * exposure_bias;
                    const float C             = 0.10f;
                    const float D             = 0.20f;
                    const float E             = 0.01f;
                    const float F             = 0.30f;

                    F32 white_tonemapped = ((white * (A * white + C * B) + D * E) / (white * (A * white + B) + D * F)) - E / F;

                    shader->uniform4f(LLShaderMgr::TONEMAP_PARAMS, white_tonemapped, 0.f, 0.f, 0.f);
                    break;
                }
                case 6: // AgX
                {
                    static LLCachedControl<F32> tonemap_agx_contrast(gSavedSettings, "RenderTonemapAgxContrast", 1.25f);
                    static LLCachedControl<F32> tonemap_agx_white(gSavedSettings, "RenderTonemapAgxWhite", 16.29f);

                    float agx_white = llmax(2.f, tonemap_agx_white());

                    // Calculate allenwp tonemapping curve parameters on the CPU to improve shader performance.
                    // Source and details: https://allenwp.com/blog/2025/05/29/allenwp-tonemapping-curve/

                    // These constants must match the those in the shader code.
                    // 18% "middle gray" is perceptually 50% of the brightness of reference white.
                    const float awp_crossover_point = 0.18f;
                    // When output_max_value and/or awp_crossover_point are no longer constant, awp_shoulder_max can
                    // be calculated on the CPU and passed in as tonemap_parameters.tonemap_e.
                    const float awp_shoulder_max = max_screen_brightness - awp_crossover_point;

                    float awp_high_clip = agx_white;

                    // awp_toe_a is a solution generated by Mathematica that ensures intersection at awp_crossover_point.
                    float awp_toe_a = ((1.0f / awp_crossover_point) - 1.0f) * pow(awp_crossover_point, tonemap_agx_contrast);
                    // Slope formula is simply the derivative of the toe function with an input of awp_crossover_point.
                    float awp_slope_denom = pow(awp_crossover_point, tonemap_agx_contrast) + awp_toe_a;
                    float awp_slope       = (tonemap_agx_contrast * pow(awp_crossover_point, tonemap_agx_contrast - 1.0f) * awp_toe_a) /
                                      (awp_slope_denom * awp_slope_denom);

                    float awp_w = awp_high_clip - awp_crossover_point;
                    awp_w       = awp_w * awp_w;
                    awp_w       = awp_w / awp_shoulder_max;
                    awp_w       = awp_w * awp_slope;

                    shader->uniform4f(LLShaderMgr::TONEMAP_PARAMS, tonemap_agx_contrast, awp_toe_a, awp_slope, awp_w);
                    break;
                }
                default:
                    break;
            }
        }

        // Color correction LUT
        S32 cglut_channel = -1;
        S32 tone_curve_channel = -1;
        if (color_grade)
        {
            // Per-section bypass. Uploading a group's identity rather than its
            // settings lands in the early-out the shader already has for that
            // step, so a bypassed group is cheaper than an active one and no
            // variant or recompile is involved. The settings themselves are
            // untouched, which is the whole point -- see sGradeBypassMask.
            const U32  bypass         = sGradeBypassMask;
            const bool skip_basic     = (bypass & GRADE_BYPASS_BASIC)     != 0;
            const bool skip_primaries = (bypass & GRADE_BYPASS_PRIMARIES) != 0;
            const bool skip_split     = (bypass & GRADE_BYPASS_SPLIT)     != 0;
            const bool skip_lut       = (bypass & GRADE_BYPASS_LUT)       != 0;
            const bool skip_curve     = (bypass & GRADE_BYPASS_CURVE)     != 0;

            static const F32 IDENTITY_ZEROS[3] = { 0.f, 0.f, 0.f };
            static const F32 IDENTITY_ONES[3]  = { 1.f, 1.f, 1.f };

            if (mCGLut.notNull() && !skip_lut)
            {
                cglut_channel = shader->getTextureChannel(LLShaderMgr::COLOR_GRADE_LUT);
                if (cglut_channel > -1)
                {
                    mCGLut->bind(cglut_channel);
                }

                shader->uniform4fv(LLShaderMgr::COLOR_GRADE_LUT_SIZE, 1, mCGLutSize.mV);

                static LLCachedControl<F32> cglut_strength(gSavedSettings, "RenderColorGradeLUTStrength", 1.f);
                shader->uniform1f(LLShaderMgr::COLOR_GRADE_LUT_STRENGTH, cglut_strength);
            }
            else
            {
                shader->uniform1f(LLShaderMgr::COLOR_GRADE_LUT_STRENGTH, 0.f); // Disable lut path
            }

            // --- Linear-space grading (pre-tonemap) ---
            // White balance: resolve (CCT offset, Duv) into a linear-sRGB gain
            // on the CPU. The map lives in ALWhiteBalanceSolver so the
            // eyedropper can invert the same function this uploads -- range
            // clamping and the Duv-to-uv scale included, since a solver that
            // disagreed about either would return a pair this then altered.
            static LLCachedControl<F32>       cg_wb_cct(gSavedSettings, "RenderColorGradeWhiteBalanceCCT", 0.f);
            static LLCachedControl<F32>       cg_wb_duv(gSavedSettings, "RenderColorGradeWhiteBalanceDuv", 0.f);
            const LLVector3 wb_gain = ALWhiteBalanceSolver::gain((F32)cg_wb_cct(), (F32)cg_wb_duv());
            shader->uniform3fv(LLShaderMgr::COLOR_GRADE_WHITE_BALANCE_GAIN, 1, skip_basic ? IDENTITY_ONES : wb_gain.mV);

            // Lift / Gamma / Gain — gamma is inverted on the CPU.
            static LLCachedControl<LLVector3> cg_lift(gSavedSettings, "RenderColorGradeLift", LLVector3(0.f, 0.f, 0.f));
            static LLCachedControl<LLVector3> cg_gamma_cc(gSavedSettings, "RenderColorGradeGamma", LLVector3(1.f, 1.f, 1.f));
            static LLCachedControl<LLVector3> cg_gain(gSavedSettings, "RenderColorGradeGain", LLVector3(1.f, 1.f, 1.f));
            const LLVector3 lift_v  = cg_lift();
            const LLVector3 gamma_v = cg_gamma_cc();
            const LLVector3 gain_v  = cg_gain();
            const F32 lift_arr[3] = {
                llclamp(lift_v.mV[0], -0.5f, 0.5f),
                llclamp(lift_v.mV[1], -0.5f, 0.5f),
                llclamp(lift_v.mV[2], -0.5f, 0.5f) };
            const F32 gain_arr[3] = {
                llclamp(gain_v.mV[0], 0.5f, 1.5f),
                llclamp(gain_v.mV[1], 0.5f, 1.5f),
                llclamp(gain_v.mV[2], 0.5f, 1.5f) };
            const F32 inv_gamma_arr[3] = {
                1.0f / llclamp(gamma_v.mV[0], 0.5f, 1.5f),
                1.0f / llclamp(gamma_v.mV[1], 0.5f, 1.5f),
                1.0f / llclamp(gamma_v.mV[2], 0.5f, 1.5f) };
            shader->uniform3fv(LLShaderMgr::COLOR_GRADE_LIFT,         1, skip_primaries ? IDENTITY_ZEROS : lift_arr);
            shader->uniform3fv(LLShaderMgr::COLOR_GRADE_INV_GAMMA_CC, 1, skip_primaries ? IDENTITY_ONES  : inv_gamma_arr);
            shader->uniform3fv(LLShaderMgr::COLOR_GRADE_GAIN,         1, skip_primaries ? IDENTITY_ONES  : gain_arr);

            // --- Split toning ---
            // Tints → ratios: `tint / max(dot(tint, LUMA), 1e-4)` precomputed.
            // Identity (tint == vec3(0.5)) maps to ratio == vec3(1).
            static LLCachedControl<LLColor3> split_shadow_tint(gSavedSettings, "RenderSplitToneShadowTint", LLColor3(0.5f, 0.5f, 0.5f));
            static LLCachedControl<LLColor3> split_highlight_tint(gSavedSettings, "RenderSplitToneHighlightTint", LLColor3(0.5f, 0.5f, 0.5f));
            static LLCachedControl<LLColor3> split_midtone_tint(gSavedSettings, "RenderSplitToneMidtoneTint", LLColor3(0.5f, 0.5f, 0.5f));
            static LLCachedControl<F32>      split_midtone_amount(gSavedSettings, "RenderSplitToneMidtoneAmount", 0.f);
            static LLCachedControl<F32>      split_balance(gSavedSettings, "RenderSplitToneBalance", 0.f);
            static LLCachedControl<F32>      split_amount(gSavedSettings, "RenderSplitToneAmount", 0.f);
            static LLCachedControl<F32>      split_shadow_width(gSavedSettings, "RenderSplitToneShadowWidth", 0.35f);
            static LLCachedControl<F32>      split_highlight_width(gSavedSettings, "RenderSplitToneHighlightWidth", 0.35f);

            constexpr F32 CG_LUMA_R = 0.2126f, CG_LUMA_G = 0.7152f, CG_LUMA_B = 0.0722f;
            auto tint_to_ratio = [](const LLColor3& tint, F32 out[3]) {
                F32 l = llmax(tint.mV[0] * CG_LUMA_R + tint.mV[1] * CG_LUMA_G + tint.mV[2] * CG_LUMA_B, 1e-4f);
                F32 inv = 1.0f / l;
                out[0] = tint.mV[0] * inv;
                out[1] = tint.mV[1] * inv;
                out[2] = tint.mV[2] * inv;
            };
            F32 shadow_ratio[3], highlight_ratio[3], midtone_ratio[3];
            tint_to_ratio(split_shadow_tint(),    shadow_ratio);
            tint_to_ratio(split_highlight_tint(), highlight_ratio);
            tint_to_ratio(split_midtone_tint(),   midtone_ratio);
            // Ramps as {scale, bias}. The balance -> mid map and the width floor
            // live in ALCurveModel, so the Lightbox graph draws what this uploads.
            const F32 tone_mid = ALCurveModel::splitToneMid(split_balance());
            const ALCurveModel::SplitToneRamp shadow_ramp    = ALCurveModel::splitToneShadowRamp(tone_mid, split_shadow_width());
            const ALCurveModel::SplitToneRamp highlight_ramp = ALCurveModel::splitToneHighlightRamp(tone_mid, split_highlight_width());
            shader->uniform3fv(LLShaderMgr::SPLIT_TONE_SHADOW_RATIO,    1, shadow_ratio);
            shader->uniform3fv(LLShaderMgr::SPLIT_TONE_HIGHLIGHT_RATIO, 1, highlight_ratio);
            shader->uniform3fv(LLShaderMgr::SPLIT_TONE_MIDTONE_RATIO,   1, midtone_ratio);
            shader->uniform1f(LLShaderMgr::SPLIT_TONE_MIDTONE_AMOUNT, skip_split ? 0.f : llclamp(split_midtone_amount(), 0.0f, 1.0f));
            shader->uniform2f(LLShaderMgr::SPLIT_TONE_SHADOW_RAMP,    shadow_ramp.mScale,    shadow_ramp.mBias);
            shader->uniform2f(LLShaderMgr::SPLIT_TONE_HIGHLIGHT_RAMP, highlight_ramp.mScale, highlight_ramp.mBias);
            shader->uniform1f(LLShaderMgr::SPLIT_TONE_AMOUNT,         skip_split ? 0.f : llclamp(split_amount(), 0.0f, 1.0f));

            // --- Display-space grading ---
            // Every slider is folded into a {scale, bias} pair on the CPU
            // so the shader is one FMA per helper. Identity defaults land
            // exactly on the scale=1/bias=0 fast-path.
            static LLCachedControl<F32> cg_black_point(gSavedSettings, "RenderColorGradeBlackPoint", 0.f);
            static LLCachedControl<F32> cg_white_point(gSavedSettings, "RenderColorGradeWhitePoint", 1.f);
            static LLCachedControl<F32> cg_brightness(gSavedSettings, "RenderColorGradeBrightness", 0.f);
            static LLCachedControl<F32> cg_contrast(gSavedSettings, "RenderColorGradeContrast", 1.f);
            static LLCachedControl<F32> cg_highlights(gSavedSettings, "RenderColorGradeHighlights", 0.f);
            static LLCachedControl<F32> cg_shadows(gSavedSettings, "RenderColorGradeShadows", 0.f);
            static LLCachedControl<F32> cg_saturation(gSavedSettings, "RenderColorGradeSaturation", 1.f);
            static LLCachedControl<F32> cg_vibrance(gSavedSettings, "RenderColorGradeVibrance", 0.f);
            static LLCachedControl<F32> cg_hue_shift(gSavedSettings, "RenderColorGradeHueShift", 0.f);

            const F32 black_point = llclamp(cg_black_point(), 0.0f, 0.5f);
            const F32 white_point = llclamp(cg_white_point(), 0.5f, 1.0f);
            const F32 bwp_scale   = 1.0f / llmax(white_point - black_point, 1e-4f);
            const F32 bwp_bias    = -black_point * bwp_scale;
            const F32 brightness  = llclamp(cg_brightness(), -0.5f, 0.5f);
            const F32 contrast    = llclamp(cg_contrast(),    0.0f, 2.0f);
            const F32 bc_scale    = contrast;
            const F32 bc_bias     = (brightness - 0.5f) * contrast + 0.5f;
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_BWP_SCALE,         skip_basic ? 1.f : bwp_scale);
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_BWP_BIAS,          skip_basic ? 0.f : bwp_bias);
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_BC_SCALE,          skip_basic ? 1.f : bc_scale);
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_BC_BIAS,           skip_basic ? 0.f : bc_bias);
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_HIGHLIGHTS_SCALED, skip_basic ? 0.f : llclamp(cg_highlights(), -1.0f, 1.0f) * 0.3f);
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_SHADOWS_SCALED,    skip_basic ? 0.f : llclamp(cg_shadows(),    -1.0f, 1.0f) * 0.3f);
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_SATURATION,        skip_basic ? 1.f : llclamp(cg_saturation(),  0.0f, 2.0f));
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_VIBRANCE,          skip_basic ? 0.f : llclamp(cg_vibrance(),   -1.0f, 1.0f));
            shader->uniform1f(LLShaderMgr::COLOR_GRADE_HUE_SHIFT_NORM,    skip_basic ? 0.f : llclamp(cg_hue_shift(), -180.0f, 180.0f) / 360.0f);

            // --- Tone curve (step 12) ---
            // Baked into mToneCurveLut by bakeToneCurveLut when a curve setting
            // commits; per frame this is a bind and three uniforms. Amount 0 is
            // the shader's fast path and doubles as the bypass identity, so a
            // bypassed section, an all-identity stack, a zero Amount and a
            // missing texture all land there with nothing bound -- the same
            // shape as the 3D LUT above.
            static LLCachedControl<F32> cg_curve_amount(gSavedSettings, "RenderColorGradeCurveAmount", 1.f);
            const F32  curve_amount = llclamp(cg_curve_amount(), 0.f, 1.f);
            const bool curve_active = !skip_curve && !mToneCurveIdentity && mToneCurveLut != 0 && curve_amount > 0.f;
            if (curve_active)
            {
                tone_curve_channel = shader->enableTexture(LLShaderMgr::COLOR_GRADE_CURVE_LUT);
                if (tone_curve_channel > -1)
                {
                    // Clamp: a lookup table must saturate past its ends (see bindBrdfLut).
                    gGL.getTextureSlot(tone_curve_channel)->bindManual(ALTextureSlot::TT_TEXTURE, mToneCurveLut,
                                                                        gGL.getSampler(ALSamplers::BilinearClamp));
                }
                F32 lut_scale, lut_bias;
                ALToneCurveSet::lutScaleBias(ALToneCurveSet::LUT_SIZE, lut_scale, lut_bias);
                shader->uniform2f(LLShaderMgr::COLOR_GRADE_CURVE_LUT_SCALE, lut_scale, lut_bias);
                shader->uniform1f(LLShaderMgr::COLOR_GRADE_CURVE_AMOUNT, curve_amount);
            }
            else
            {
                shader->uniform1f(LLShaderMgr::COLOR_GRADE_CURVE_AMOUNT, 0.f); // fast path; sampler unread
            }
        }

        mScreenTriangleVB->setBuffer();
        mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        if (cglut_channel > -1 && mCGLut.notNull())
        {
            mCGLut->unbind(cglut_channel);
        }
        if (dirt_channel > -1)
        {
            gGL.getTextureSlot(dirt_channel)->unbind();
        }
        if (tone_curve_channel > -1)
        {
            gGL.getTextureSlot(tone_curve_channel)->unbind();
        }
        if (exposure_channel > -1)
        {
            gGL.getTextureSlot(exposure_channel)->unbind();
        }
        if (cross_channel > -1)
        {
            gGL.getTextureSlot(cross_channel)->unbind();
        }
        if (bloom_channel > -1)
        {
            gGL.getTextureSlot(bloom_channel)->unbind();
        }
        if (state_channel > -1)
        {
            gGL.getTextureSlot(state_channel)->unbind();
        }
        gGL.getTextureSlot(diffuse_channel)->unbind();
        shader->unbind();
    }
    dst->flush();
}

void LLPipeline::copyScreenSpaceReflections(LLRenderTarget* src, LLRenderTarget* dst)
{

    // Where the plan has the copy, SSR with probes to trace it through, and the copy is built: a reconcile that could
    // build the render size at no size at all leaves it unbuilt under a plan that wants it.
    if (mResourcePlan.wants(ALRenderResourcePlan::Id::SSRScene) && dst->isComplete() && !gCubeSnapshot)
    {
        LL_PROFILE_GPU_ZONE("ssr copy");
        dst->copyContents(*src, 0, 0, src->getWidth(), src->getHeight(), 0, 0, dst->getWidth(), dst->getHeight(),
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
        copySceneDepth(*dst);
    }
}

// The pixels of a `width` x `height` screen the water reads its copy of the screen at, from the view drawn now: where
// its field can be drawn (the surface's slab, A past the water each way), and as far again as a ripple moves a read.
// waterF.glsl moves one by at most the scale above the water, a fraction of the screen, and its filter takes a texel
// more. From below the surface, which fills most of the view and moves its reads by the scale below, the whole screen.
static LLRect water_refraction_rect(const ALWaterSurface& surface, S32 width, S32 height)
{
    const LLRect whole(0, height, width, 0);
    LLVector3 lo(F32_MAX, F32_MAX, F32_MAX);
    LLVector3 hi(-F32_MAX, -F32_MAX, -F32_MAX);
    surface.addExtents(lo, hi);
    LLSettingsWater::ptr_t water = LLEnvironment::instance().getCurrentWater();
    if (LLViewerCamera::getInstance()->cameraUnderWater() || lo.mV[VX] > hi.mV[VX] || !water)
    {
        return whole;
    }
    const F32 scale = llmax(water->getScaleAbove(), 0.f);
    const LLCamera& view = LLViewerCamera::getCurrent();
    return ALWaterField::screenRect(view.getModelview(), view.getProjection(), lo, hi, width, height,
                                    (S32)ceilf(scale * (F32)width) + 1, (S32)ceilf(scale * (F32)height) + 1);
}

void LLPipeline::beginWaterTargets()
{
    using Plan = ALRenderResourcePlan;
    llassert(!mWaterTargets);
    mWaterTargets.emplace(mTransientTargets);
    // The pack's own, at the size its screen renders at, and kept through 1800 frames unacquired, 30 seconds at 60 fps:
    // a view that looks up from the sea, or a capture that comes round again, would otherwise find the depth copy
    // freed, and the frame that draws the water next build it mid-pass.
    constexpr U32                     WATER_KEEP_FRAMES = 1800;
    const ALTransientTargetPool::Keep keep(mTransientTargets, WATER_KEEP_FRAMES);
    mWaterRefraction = acquireResource(*mWaterTargets, Plan::inPack(renderPack(), Plan::Slot::Water));
    mWaterDepthCopy  = acquireResource(*mWaterTargets, Plan::inPack(renderPack(), Plan::Slot::DepthCopy));

    // The colour over the part of the screen the water reads it at, the blit covering that and the rest of the copy
    // left as the pool gave it; the depth whole, which the waves' depth pyramid reduces, copySceneDepth drawing over
    // every texel of its target.
    LLRenderTarget& src = mRT->screen;
    if (mWaterRefraction)
    {
        const LLRect rect = water_refraction_rect(mWaterSurface, (S32)src.getWidth(), (S32)src.getHeight());
        if (rect.notEmpty())
        {
            mWaterRefraction->copyContents(src, rect.mLeft, rect.mBottom, rect.mRight, rect.mTop, rect.mLeft, rect.mBottom,
                                           rect.mRight, rect.mTop, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
    }
    if (mWaterDepthCopy)
    {
        copySceneDepth(*mWaterDepthCopy);
    }
}

void LLPipeline::endWaterTargets()
{
    mWaterRefraction = nullptr;
    mWaterDepthCopy  = nullptr;
    mWaterTargets.reset();
}

void LLPipeline::copySceneDepth(LLRenderTarget& dst)
{
    LL_PROFILE_GPU_ZONE("depth copy");
    LLRenderTarget& src = mRT->deferredScreen;
    if (!gCopyDepthProgram.isComplete())
    {
        // Unstretched, where the two differ in size, but the depth all the same.
        dst.copyContents(src, 0, 0, src.getWidth(), src.getHeight(), 0, 0, dst.getWidth(), dst.getHeight(),
                         GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        return;
    }

    dst.bindTarget();
    {
        LLGLSColorMask no_color(false, false);
        LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
        LLGLDisable no_blend(GL_BLEND);
        LLGLDisable no_cull(GL_CULL_FACE);

        gCopyDepthProgram.bind();
        gCopyDepthProgram.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &src, ALSamplers::PointClamp);
        mScreenTriangleVB->setBuffer();
        mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        gCopyDepthProgram.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
        gCopyDepthProgram.unbind();
    }
    dst.flush();
}

LLRenderTarget* LLPipeline::generateGlow(LLRenderTarget* src, ALTransientTargets& scope)
{
    LL_PROFILE_GPU_ZONE("glow generate");

    // No glow, or no blur to run, is no glow: nothing for combineGlow to add, and no pass of it to keep the final blit
    // from sharpening the frame.
    const S32 kernel = RenderGlowIterations * 2;
    if (!sRenderGlow || kernel <= 0)
    {
        return nullptr;
    }

    // The extract and the blur's other buffer for the blur alone; the glow from the caller's scope, until combineGlow
    // has added it. Each is cleared before it is drawn.
    ALTransientTargets blur(mTransientTargets);
    LLRenderTarget*    extract = acquireResource(blur, ALRenderResourcePlan::Id::Glow2);
    LLRenderTarget*    other   = extract ? acquireResource(blur, ALRenderResourcePlan::Id::Glow0) : nullptr;
    LLRenderTarget*    glow    = other ? acquireResource(scope, ALRenderResourcePlan::Id::Glow1) : nullptr;
    if (!glow)
    {
        return nullptr;
    }
    // The blur ping-pongs between the two, ending in the glow, as the iterations it runs are an even count.
    LLRenderTarget* const buffers[2] = { other, glow };

    {
        extract->bindTarget();
        extract->clear();

        gGlowExtractProgram.bind();
        F32 maxAlpha = RenderGlowMaxExtractAlpha;
        F32 warmthAmount = RenderGlowWarmthAmount;
        LLVector3 lumWeights = RenderGlowLumWeights;
        LLVector3 warmthWeights = RenderGlowWarmthWeights;

        // 9999, the default, leaves glow to alpha alone: the source is the 8-bit
        // post target, so luminance never reaches 1 and only a threshold below
        // 1 makes anything glow by brightness.
        static LLCachedControl<F32> glow_min_luminance(gSavedSettings, "RenderGlowMinLuminance", 9999.f);
        gGlowExtractProgram.uniform1f(LLShaderMgr::GLOW_MIN_LUMINANCE, glow_min_luminance());
        gGlowExtractProgram.uniform1f(LLShaderMgr::GLOW_MAX_EXTRACT_ALPHA, maxAlpha);
        gGlowExtractProgram.uniform3f(LLShaderMgr::GLOW_LUM_WEIGHTS, lumWeights.mV[0], lumWeights.mV[1],
            lumWeights.mV[2]);
        gGlowExtractProgram.uniform3f(LLShaderMgr::GLOW_WARMTH_WEIGHTS, warmthWeights.mV[0], warmthWeights.mV[1],
            warmthWeights.mV[2]);
        gGlowExtractProgram.uniform1f(LLShaderMgr::GLOW_WARMTH_AMOUNT, warmthAmount);

        if (shaderInputs().mLocal.mGlowNoise)
        {
            S32 channel = gGlowExtractProgram.enableTexture(LLShaderMgr::GLOW_NOISE_MAP);
            if (channel > -1)
            {
                // Tiled across the screen, so GL_REPEAT as before.
                gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, mTrueNoiseMap,
                                                    gGL.getSampler(ALSamplers::PointWrap));
            }
            gGlowExtractProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                                          (GLfloat)extract->getWidth(),
                                          (GLfloat)extract->getHeight());
        }

        {
            LLGLEnable blend_on(GL_BLEND);

            gGL.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

            gGlowExtractProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, src);

            gGL.color4f(1, 1, 1, 1);
            gPipeline.enableLightsFullbright();

            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            extract->flush();
        }

        gGlowExtractProgram.unbind();

        // Power of two between 1 and 512: the clamp ALRenderResourcePlan sizes
        // the glow targets with, so the step is in texels of the targets drawn here.
        U32 glowResPow = (U32)mResourceInputs.mGlowResolutionPow;
        const U32 glow_res = llmax(1, llmin(512, 1 << glowResPow));

        F32 delta = RenderGlowWidth / glow_res;
        // Use half the glow width if we have the res set to less than 9 so that it looks
        // almost the same in either case.
        if (glowResPow < 9)
        {
            delta *= 0.5f;
        }
        F32 strength = RenderGlowStrength;

        gGlowProgram.bind();
        gGlowProgram.uniform1f(LLShaderMgr::GLOW_STRENGTH, strength);

        for (S32 i = 0; i < kernel; i++)
        {
            buffers[i % 2]->bindTarget();
            buffers[i % 2]->clear();

            if (i == 0)
            {
                gGlowProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, extract);
            }
            else
            {
                gGlowProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, buffers[(i - 1) % 2]);
            }

            if (i % 2 == 0)
            {
                gGlowProgram.uniform2f(LLShaderMgr::GLOW_DELTA, delta, 0);
            }
            else
            {
                gGlowProgram.uniform2f(LLShaderMgr::GLOW_DELTA, 0, delta);
            }

            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            buffers[i % 2]->flush();
        }

        gGlowProgram.unbind();
    }
    return glow;
}

// HDR bloom pyramid: threshold-extract into mBloomMip[0], downsample down the
// pyramid with a Karis-averaged 13-tap filter, then upsample back with an
// additive 3x3 tent filter. Halation is carried alongside in the alpha channel.
bool LLPipeline::bloomHDRReady() const
{
    return mRT->bloomMipCount >= 3 &&
           gBloomExtractProgram.isComplete() &&
           gBloomDownsampleProgram.isComplete() &&
           gBloomDownsampleFirstProgram.isComplete() &&
           gBloomUpsampleProgram.isComplete();
}

LLPipeline::Bloom LLPipeline::generateBloomHDR(LLRenderTarget* src, ALTransientTargets& scope)
{
    LL_PROFILE_GPU_ZONE("bloom hdr generate");

    Bloom bloom;
    if (!bloomHDRReady())
    {
        return bloom;
    }

    // At zero strength nothing the pyramid holds reaches the frame: colorCorrect scales the
    // bloom and the streaks by bloom strength and the halation by its own, which only the
    // BLOOM_HALATION programs composite, and samples none of it while those are zero.
    // Nothing else reads the pyramid, so none is taken from the pool.
    static LLCachedControl<F32>  bloom_strength(gSavedSettings, "RenderBloomStrength", 0.325f);
    static LLCachedControl<F32>  halation_strength(gSavedSettings, "RenderBloomHalationStrength", 0.0f);
    static LLCachedControl<bool> halation_enabled(gSavedSettings, "RenderBloomHalation", false);
    if (bloom_strength() <= 0.f && !(halation_enabled() && halation_strength() > 0.f))
    {
        return bloom;
    }

    // The pyramid from the caller's scope, held until colorCorrect has composited mip 0. Each level is cleared and drawn
    // whole below, whatever the pool's target held before.
    for (U32 mip = 0; mip < mRT->bloomMipCount; ++mip)
    {
        bloom.mMips[mip] = acquireResource(scope, ALRenderResourcePlan::bloomMip(renderPack(), mip));
        if (!bloom.mMips[mip])
        {
            break;
        }
        ++bloom.mMipCount;
    }
    if (bloom.mMipCount < 3)
    {
        return Bloom{};
    }

    static LLCachedControl<F32> bloom_threshold(gSavedSettings, "RenderBloomThreshold", 1.0f);
    static LLCachedControl<F32> bloom_knee(gSavedSettings, "RenderBloomKnee", 0.5f);
    static LLCachedControl<F32> bloom_firefly_clamp(gSavedSettings, "RenderBloomFireflyClamp", 24.f);
    static LLCachedControl<F32> bloom_scatter(gSavedSettings, "RenderBloomScatter", 0.7f);
    static LLCachedControl<F32> alpha_glow_boost(gSavedSettings, "RenderBloomAlphaGlowBoost", 2.0f);

    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
    static LLCachedControl<bool> buildNoPost(gSavedSettings, "RenderDisablePostProcessing", false);
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    bool legacy_gamma = psky->getReflectionProbeAmbiance(should_auto_adjust) == 0.f;
    bool no_post = gSnapshotNoPost || legacy_gamma || (buildNoPost && gFloaterTools && gFloaterTools->isAvailable());

    LLGLDepthTest depth(GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);

    // Extract pass: write thresholded bloom + halation into mip 0.
    {
        LLGLDisable blend(GL_BLEND);
        bloom.mMips[0]->bindTarget();
        bloom.mMips[0]->clear();

        gBloomExtractProgram.bind();
        gBloomExtractProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, src);
        gBloomExtractProgram.uniform1f(LLShaderMgr::BLOOM_THRESHOLD, no_post ? 99999.f :bloom_threshold());
        gBloomExtractProgram.uniform1f(LLShaderMgr::BLOOM_KNEE, llmax(bloom_knee(), 0.0f));
        // no_post already disables extraction via the threshold; keep the clamp out of its way.
        gBloomExtractProgram.uniform1f(LLShaderMgr::BLOOM_FIREFLY_CLAMP,
                                       no_post ? 1e9f : llmax(bloom_firefly_clamp(), 0.f));
        gBloomExtractProgram.uniform1f(LLShaderMgr::BLOOM_ALPHA_GLOW_BOOST, llmax(alpha_glow_boost(), 0.0f));

        // Reuse the warmth weights from legacy glow so the halation red-bias
        // matches artist expectations set by the old RenderGlowWarmthWeights knob.
        LLVector3 warmth = RenderGlowWarmthWeights;
        gBloomExtractProgram.uniform3f(LLShaderMgr::HALATION_LUM_WEIGHTS, warmth.mV[0], warmth.mV[1], warmth.mV[2]);

        mScreenTriangleVB->setBuffer();
        mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        bloom.mMips[0]->flush();
        gBloomExtractProgram.unbind();
    }

    // Downsample chain: mip[i-1] -> mip[i]. The first downsample uses the
    // partial-Karis variant (1 / (1 + lum) per 2x2 group) to suppress fireflies
    // from full-res specular highlights before they bleed across the pyramid.
    // Subsequent levels run the plain 13-tap since they operate on already-
    // averaged data where firefly energy has been amortized out.
    {
        LLGLDisable blend(GL_BLEND);
        for (U32 i = 1; i < bloom.mMipCount; ++i)
        {
            LLGLSLShader* shader = (i == 1) ? &gBloomDownsampleFirstProgram
                                            : &gBloomDownsampleProgram;
            LLRenderTarget* srcMip = bloom.mMips[i - 1];

            bloom.mMips[i]->bindTarget();
            bloom.mMips[i]->clear();

            shader->bind();
            shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, srcMip, ALSamplers::BilinearMirror);
            shader->uniform2f(LLShaderMgr::BLOOM_TEXEL_SIZE,
                              1.0f / (F32)srcMip->getWidth(),
                              1.0f / (F32)srcMip->getHeight());

            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            bloom.mMips[i]->flush();
            shader->unbind();
        }
    }

    // ---- Cross-screen (star) filter ---------------------------------------
    //
    // Streaks every thresholded highlight, the way an etched glass filter
    // diffracts any bright point in frame. Distinct from the lens flare
    // starburst, which is locked to the sun and drawn procedurally around it.
    //
    // Split across the upsample chain on purpose: the streak input has to be
    // read *before* the upsample walk mutates the mips, but the result has to
    // be added *after* it, or the walk would smear the streaks back through
    // the pyramid.
    static LLCachedControl<F32> streak_strength_setting(gSavedSettings, "RenderCrossFilterStrength", 0.f);
    static LLCachedControl<S32> cross_points(gSavedSettings, "RenderCrossFilterPoints", 4);
    static LLCachedControl<F32> cross_angle(gSavedSettings, "RenderCrossFilterAngle", 0.f);
    static LLCachedControl<F32> cross_length(gSavedSettings, "RenderCrossFilterLength", 1.f);
    static LLCachedControl<F32> cross_falloff(gSavedSettings, "RenderCrossFilterFalloff", 1.5f);
    static LLCachedControl<F32> cross_chromatic(gSavedSettings, "RenderCrossFilterChromatic", 0.f);

    // Streaks ride the bloom pyramid and are scaled by bloom strength where
    // they are composited, so at bloom strength 0 they are invisible even while
    // halation keeps the pyramid running, and the twelve-draw chain is skipped.
    const F32  streak_strength = (no_post || bloom_strength() <= 0.f)
                               ? 0.f
                               : llclamp(streak_strength_setting(), 0.f, CROSS_FILTER_MAX_STRENGTH);
    const bool streaks_on      = (streak_strength > 0.f) && gCrossFilterProgram.isComplete();

    if (streaks_on)
    {
        // Streak from mip 0, which at this point in the pass still holds the
        // raw thresholded extract: the downsample chain writes mips 1 and up
        // and leaves mip 0 untouched, so it is the sharpest and cleanest
        // "which pixels are bright" answer available. Not a lower mip to save
        // fill: mip 1 is a 13-tap downsample, so the highlight being streaked
        // is already a blob before the streak starts, and a streak can be no
        // thinner than the point it is drawn from.
        //
        // Cost scales with RenderBloomResolutionScale, which sizes the whole
        // pyramid -- lowering it makes the streaks cheaper and softer together.
        //
        // Twelve passes at four arms is a lot of fill at full resolution, and
        // quartering the area is the cheapest lever that does not touch arm
        // count or reach. The source stays mip 0, so the *point* being streaked
        // is still the sharp extract rather than a pre-blurred mip -- what is
        // lost is arm resolution, not arm origin.
        //
        // The first pass gets a proper box downsample for free: a half-res texel
        // centre lands exactly on the corner between two full-res texels, so the
        // bilinear fetch averages the 2x2 group rather than point-sampling it.
        const U32 streak_w = llmax(1u, bloom.mMips[0]->getWidth() / 2);
        const U32 streak_h = llmax(1u, bloom.mMips[0]->getHeight() / 2);

        // Three buffers, not two: each arm needs its own ping-pong chain, and
        // the arms have to accumulate somewhere that is neither the chain's
        // scratch nor its source. Accumulating straight into bloomMip[0] would
        // work for the first arm and then feed the second arm its own output.
        //
        // All three from the pool, RGBA16F as the HDR scene they streak: the
        // chain's two for the chain alone, and the sum from the caller's scope
        // until colorCorrect has composited it. Each pass reads one texture and
        // writes another, so no read waits on a barrier.
        const ALRenderTargetDesc streak_desc{ .width = streak_w, .height = streak_h, .color = { GL_RGBA16F } };
        ALTransientTargets       chain(mTransientTargets);
        LLRenderTarget*          scratch_a = chain.acquire(streak_desc, "bloom.cross.a");
        LLRenderTarget*          scratch_b = chain.acquire(streak_desc, "bloom.cross.b");
        LLRenderTarget*          sum       = scope.acquire(streak_desc, "bloom.cross.sum");

        if (scratch_a && scratch_b && sum)
        {
            const F32 angle_rad = llclamp(cross_angle(), 0.f, 360.f) * DEG_TO_RAD;
            const S32 arms      = llclamp(cross_points(), 2, 12);

            gCrossFilterProgram.bind();

            // Base step in texels of the streak target, near 1 by design: it
            // multiplies every offset, so at 2 the chain lands on even texels
            // only and real gaps open between them. Reach comes from the three
            // quadrupling passes (0..63 texels), not from scaling this up.
            gCrossFilterProgram.uniform1f(LLShaderMgr::CROSS_LENGTH, llclamp(cross_length(), 0.25f, 2.f));

            // Falloff is authored as a 0..1.5 tightness and converted here to
            // the exponential base the shader wants, rather than exposing the
            // base: weights are pow(base, -step_index) and step_index reaches
            // 63 across the chain, so a base of 1.5 attenuates the far taps by
            // 1e-11 and everything usable lives between 1.0 and roughly 1.1.
            // This maps the whole slider onto that band: the value is how many
            // e-folds of brightness are lost between the core and the tip of
            // an arm, over six.
            const F32 tightness = llclamp(cross_falloff(), 0.1f, 3.f);
            // The chain's exact reach, TAPS^3 - 1, derived from the same constant
            // the shader compiles against -- see CROSS_FILTER_TAPS.
            const F32 max_step  = (F32)(CROSS_FILTER_TAPS * CROSS_FILTER_TAPS * CROSS_FILTER_TAPS - 1);
            gCrossFilterProgram.uniform1f(LLShaderMgr::CROSS_FALLOFF, expf(tightness * 6.f / max_step));
            gCrossFilterProgram.uniform1f(LLShaderMgr::CROSS_CHROMATIC, llclamp(cross_chromatic(), 0.f, 1.f));

            // One three-pass chain per arm, each strictly one-sided.
            //
            // Streaking every direction in a single pass is what produced the
            // spikes: a tap could run forward in one pass and backward in the
            // next, so net offsets became +/-i +/-4j +/-16k with independent
            // signs and their weights tracked how far the path travelled rather
            // than where it ended. Per-arm chains restore the base-4 tiling the
            // whole construction depends on.
            for (S32 arm = 0; arm < arms; ++arm)
            {
                const F32 theta = angle_rad + (2.f * F_PI * (F32)arm) / (F32)arms;
                const F32 dir_x = cosf(theta);
                const F32 dir_y = sinf(theta);
                gCrossFilterProgram.uniform2f(LLShaderMgr::CROSS_DIR, dir_x, dir_y);

                // Pass 0 reads mip 0; passes 1 and 2 read the pass before.
                LLRenderTarget* sources[3]      = { bloom.mMips[0], scratch_a, scratch_b };
                LLRenderTarget* destinations[3] = { scratch_a, scratch_b, sum };
                const F32       scales[3]       = { 1.f, (F32)CROSS_FILTER_TAPS,
                                                    (F32)(CROSS_FILTER_TAPS * CROSS_FILTER_TAPS) };

                auto streak_pass = [&](S32 pass)
                {
                    LLRenderTarget* src = sources[pass];

                    // The sampler's edge clamp keeps every tap on the source.
                    gCrossFilterProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, src, ALSamplers::BilinearClamp);

                    // Always the *streak target's* texel, never the source's.
                    // The base-4 tiling only holds if every pass steps in the
                    // same unit, and pass 0 reads a full-resolution mip while
                    // the rest read half-resolution scratch -- using each
                    // source's own texel would double the stride midway through
                    // the chain and break the tiling that the whole
                    // construction depends on.
                    gCrossFilterProgram.uniform2f(LLShaderMgr::CROSS_TEXEL,
                                                  1.f / (F32)streak_w,
                                                  1.f / (F32)streak_h);
                    gCrossFilterProgram.uniform1f(LLShaderMgr::CROSS_PASS_SCALE, scales[pass]);

                    mScreenTriangleVB->setBuffer();
                    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                };

                // Two scratch passes overwrite; blending stays off. No clear:
                // the fullscreen triangle writes every texel of the target with
                // blending disabled, whatever the pool's target held before.
                {
                    LLGLDisable blend(GL_BLEND);
                    for (S32 pass = 0; pass < 2; ++pass)
                    {
                        destinations[pass]->bindTarget();
                        streak_pass(pass);
                        destinations[pass]->flush();
                    }
                }

                // The arm's last pass adds into the shared accumulator -- except
                // the first, which overwrites it. The fullscreen triangle covers
                // every texel, so arm 0 establishes the buffer and a clear would
                // be the same redundant fill the scratch passes avoid. It does
                // couple correctness to the first iteration running, which holds
                // because `arms` is clamped to at least 2 above.
                {
                    LLGLState blend(GL_BLEND, arm > 0);
                    gGL.setSceneBlendType(LLRender::BT_ADD);

                    sum->bindTarget();
                    streak_pass(2);
                    sum->flush();

                    gGL.setSceneBlendType(LLRender::BT_ALPHA);
                }
            }

            gCrossFilterProgram.unbind();

            bloom.mStreaks = sum;
        }
    }

    // Upsample chain: mip[i] -> mip[i-1] with additive blend. Walks from the
    // smallest mip back up to mip 0, leaving the final bloom in mBloomMip[0].
    {
        LLGLEnable blend(GL_BLEND);
        gGL.setSceneBlendType(LLRender::BT_ADD);

        gBloomUpsampleProgram.bind();
        gBloomUpsampleProgram.uniform1f(LLShaderMgr::BLOOM_SCATTER, llmax(bloom_scatter(), 0.0f));

        for (S32 i = (S32)bloom.mMipCount - 1; i > 0; --i)
        {
            LLRenderTarget* srcMip = bloom.mMips[i];
            LLRenderTarget* dstMip = bloom.mMips[i - 1];

            dstMip->bindTarget();

            gBloomUpsampleProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, srcMip, ALSamplers::BilinearMirror);
            gBloomUpsampleProgram.uniform2f(LLShaderMgr::BLOOM_TEXEL_SIZE,
                                            1.0f / (F32)srcMip->getWidth(),
                                            1.0f / (F32)srcMip->getHeight());

            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            dstMip->flush();
        }

        gBloomUpsampleProgram.unbind();
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
    }

    // The summed arms stay in their own target. colorCorrect samples them
    // alongside the pyramid, so there is no composite pass here to write them
    // into mip 0.
    return bloom;
}

// Composite the bloom pyramid (mBloomMip[0]) additively into the pre-tonemap
// scene buffer. Halation rides in the alpha channel and is tinted at composite.
// The main render path folds this into colorCorrectF (BLOOM_COMPOSITE). This
// function has never had a caller anywhere in the tree; it is kept as a
// standalone equivalent, but note it is no longer equivalent -- cross-filter
// streaks are composited only in colorCorrectF, so this path would drop them.
void LLPipeline::compositeBloomHDR(LLRenderTarget* scene, const Bloom& bloom)
{
    LL_PROFILE_GPU_ZONE("bloom hdr composite");

    if (bloom.mMipCount < 3 || !gBloomCompositeProgram.isComplete())
    {
        return;
    }

    static LLCachedControl<F32> bloom_strength(gSavedSettings, "RenderBloomStrength", 0.325f);
    static LLCachedControl<F32> halation_strength(gSavedSettings, "RenderBloomHalationStrength", 0.0f);
    static LLCachedControl<LLColor3> halation_tint(gSavedSettings, "RenderBloomHalationTint", LLColor3(1.0f, 0.35f, 0.15f));

    LLGLDepthTest depth(GL_FALSE);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLEnable blend(GL_BLEND);
    gGL.setSceneBlendType(LLRender::BT_ADD);

    scene->bindTarget();

    gBloomCompositeProgram.bind();
    gBloomCompositeProgram.bindTexture(LLShaderMgr::BLOOM_SAMPLER, bloom.mMips[0], ALSamplers::BilinearMirror);
    gBloomCompositeProgram.uniform1f(LLShaderMgr::BLOOM_STRENGTH, llmax(bloom_strength(), 0.0f));
    gBloomCompositeProgram.uniform1f(LLShaderMgr::HALATION_STRENGTH, llmax(halation_strength(), 0.0f));
    const LLColor3& tint = halation_tint();
    gBloomCompositeProgram.uniform3f(LLShaderMgr::HALATION_TINT, tint.mV[0], tint.mV[1], tint.mV[2]);

    mScreenTriangleVB->setBuffer();
    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    gBloomCompositeProgram.unbind();

    scene->flush();

    gGL.setSceneBlendType(LLRender::BT_ALPHA);
}

bool LLPipeline::applyCAS(LLRenderTarget* src, LLRenderTarget* dst)
{
    static LLCachedControl<F32> cas_sharpness(gSavedSettings, "RenderCASSharpness", 0.4f);
    LL_PROFILE_GPU_ZONE("cas");
    if (cas_sharpness == 0.0f || !gCASProgram.isComplete())
    {
        return false;
    }

    // Bind setup:
    dst->bindTarget();

    gCASProgram.bind();

    {
        varAU4(const0);
        varAU4(const1);
        CasSetup(const0, const1,
            cas_sharpness(),             // Sharpness tuning knob (0.0 to 1.0).
            (AF1)src->getWidth(), (AF1)src->getHeight(),  // Input size.
            (AF1)dst->getWidth(), (AF1)dst->getHeight()); // Output size.

        gCASProgram.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, const0);
        gCASProgram.uniform4uiv(LLShaderMgr::CAS_PARAM_1, 1, const1);

        gCASProgram.uniform2f(LLShaderMgr::OUT_SCREEN_RES, (AF1)dst->getWidth(), (AF1)dst->getHeight());
    }

    gCASProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src, ALSamplers::PointMirror);

    // Draw
    gPipeline.mScreenTriangleVB->setBuffer();
    gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

    gCASProgram.unbind();

    dst->flush();
    return true;
}

bool LLPipeline::applyFXAA(LLRenderTarget* src, LLRenderTarget* dst)
{
    LL_PROFILE_GPU_ZONE("FXAA");
    {
        llassert(!gCubeSnapshot);
        bool multisample = mResourceInputs.mFSAAType == 1 && gFXAAProgram[0].isComplete();
        if (!multisample)
        {
            return false;
        }

        {
            LL_PROFILE_GPU_ZONE("aa");
            S32 width = dst->getWidth();
            S32 height = dst->getHeight();

            dst->bindTarget();

            static LLCachedControl<U32> aa_quality(gSavedSettings, "RenderFSAASamples", 0U);
            U32 fsaa_quality = std::clamp(aa_quality(), 0U, 3U);

            LLGLSLShader* shader = &gFXAAProgram[fsaa_quality];
            shader->bind();

            // Straight from the source, with no luma baked into an intermediate first: FXAA computes
            // each tap's luma from its colour (FXAA_LUMA_FROM_RGB), so it reads the post chain's
            // target at that target's precision, RGB10_A2 under HDR.
            S32 channel = shader->enableTexture(LLShaderMgr::DIFFUSE_MAP);
            if (channel > -1)
            {
                src->bindTexture(0, channel, ALSamplers::BilinearMirror);
            }

            // The destination is an FBO; bindTarget already set its viewport.
            // Forcing the full world-view viewport here makes RenderResolutionDivisor
            // write only the lower-left fraction of the reduced-size target.

            F32 scale_x = (F32)width / src->getWidth();
            F32 scale_y = (F32)height / src->getHeight();
            shader->uniform2f(LLShaderMgr::FXAA_TC_SCALE, scale_x, scale_y);
            shader->uniform2f(LLShaderMgr::FXAA_RCP_SCREEN_RES, 1.f / width * scale_x, 1.f / height * scale_y);
            shader->uniform4f(LLShaderMgr::FXAA_RCP_FRAME_OPT, -0.5f / width * scale_x, -0.5f / height * scale_y,
                0.5f / width * scale_x, 0.5f / height * scale_y);
            shader->uniform4f(LLShaderMgr::FXAA_RCP_FRAME_OPT2, -2.f / width * scale_x, -2.f / height * scale_y,
                2.f / width * scale_x, 2.f / height * scale_y);

            {
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                S32 depth_channel = shader->getTextureChannel(LLShaderMgr::DEFERRED_DEPTH);
                gGL.getTextureSlot(depth_channel)->bind(&mRT->deferredScreen, true);

                mScreenTriangleVB->setBuffer();
                mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            }

            shader->unbind();
            dst->flush();
        }
    }
    return true;
}

LLPipeline::SMAATargets LLPipeline::generateSMAABuffers(LLRenderTarget* src, ALTransientTargets& scope)
{
    llassert(!gCubeSnapshot);
    SMAATargets smaa;
    if (mResourceInputs.mFSAAType != 2 || !gSMAAEdgeDetectProgram[0].isComplete())
    {
        return smaa;
    }

    // Both from the caller's scope until the neighbourhood blend has read the weights, the weights over the edges'
    // stencil where the plan shares it. Each is cleared below before it is drawn.
    smaa.mEdges = acquireResource(scope, ALRenderResourcePlan::Id::SMAAEdges);
    smaa.mBlend = smaa.mEdges ? acquireResource(scope, ALRenderResourcePlan::Id::SMAABlend, {}, smaa.mEdges) : nullptr;
    const bool multisample = smaa.mEdges && smaa.mBlend;

    // Present everything.
    if (multisample)
    {
        LL_PROFILE_GPU_ZONE("SMAA Edge");
        static LLCachedControl<U32> aa_quality(gSavedSettings, "RenderFSAASamples", 0U);
        U32 fsaa_quality = std::clamp(aa_quality(), 0U, 3U);

        S32 width = src->getWidth();
        S32 height = src->getHeight();

        float rt_metrics[] = { 1.f / width, 1.f / height, (float)width, (float)height };

        LLGLDepthTest depth(GL_FALSE, GL_FALSE);

        static LLCachedControl<bool> use_sample(gSavedSettings, "RenderSMAAUseSample", false);
        const bool use_predication = shaderInputs().mLocal.mSMAAPredication;
        // Stencil optimization requires all three passes to share the same stencil attachment.
        bool use_stencil = mResourceInputs.mSMAAStencil && smaa.mEdges->hasStencil() && smaa.mBlend->hasStencil();
        LLGLState stencil(GL_STENCIL_TEST, use_stencil);
        {
            // Bind setup:
            LLRenderTarget& dest = *smaa.mEdges;
            LLGLSLShader& edge_shader = gSMAAEdgeDetectProgram[fsaa_quality];

            dest.bindTarget();
            if (use_stencil)
            {
                // The stencil clear writes only the bits the stencil write mask allows.
                glStencilMask(0xFF);
            }
            dest.clear(use_stencil ? (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) : GL_COLOR_BUFFER_BIT);

            edge_shader.bind();
            edge_shader.uniform4fv(LLShaderMgr::SMAA_RT_METRICS, 1, rt_metrics);

            S32 channel = edge_shader.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);
            if (channel > -1)
            {
                if (!use_sample)
                {
                    src->bindTexture(0, channel, ALSamplers::BilinearClamp);
                }
                else
                {
                    gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, mSMAASampleMap,
                                                        gGL.getSampler(ALSamplers::BilinearClamp));
                }
            }

            S32 pred_channel = -1;
            if (use_predication)
            {
                pred_channel = edge_shader.enableTexture(LLShaderMgr::SMAA_PREDICATION_TEX);
                if (pred_channel > -1)
                {
                    gGL.getTextureSlot(pred_channel)->bind(&mRT->deferredScreen, true,
                                                       gGL.getSampler(ALSamplers::PointClamp));
                }
                static LLCachedControl<F32> pred_threshold(gSavedSettings, "RenderSMAAPredicationThreshold", 0.01f);
                static LLCachedControl<F32> pred_scale(gSavedSettings, "RenderSMAAPredicationScale", 2.f);
                static LLCachedControl<F32> pred_strength(gSavedSettings, "RenderSMAAPredicationStrength", 0.4f);
                edge_shader.uniform1f(LLShaderMgr::SMAA_PREDICATION_THRESHOLD, pred_threshold);
                edge_shader.uniform1f(LLShaderMgr::SMAA_PREDICATION_SCALE, llclamp((F32)pred_scale, 1.f, 5.f));
                edge_shader.uniform1f(LLShaderMgr::SMAA_PREDICATION_STRENGTH, llclamp((F32)pred_strength, 0.f, 1.f));
            }

            if (use_stencil)
            {
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                glStencilMask(0xFF);
            }
            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            edge_shader.unbind();
            dest.flush();

            gGL.getTextureSlot(channel)->unbindFast();
            if (pred_channel > -1)
            {
                gGL.getTextureSlot(pred_channel)->unbindFast();
            }
        }

        {
            // Bind setup:
            LLRenderTarget& dest = *smaa.mBlend;
            LLGLSLShader& blend_weights_shader = gSMAABlendWeightsProgram[fsaa_quality];

            dest.bindTarget();
            // Preserve the stencil mask written by the edge-detect pass.
            dest.clear(GL_COLOR_BUFFER_BIT);

            blend_weights_shader.bind();
            blend_weights_shader.uniform4fv(LLShaderMgr::SMAA_RT_METRICS, 1, rt_metrics);

            S32 edge_tex_channel = blend_weights_shader.enableTexture(LLShaderMgr::SMAA_EDGE_TEX);
            if (edge_tex_channel > -1)
            {
                smaa.mEdges->bindTexture(0, edge_tex_channel, ALSamplers::BilinearClamp);
            }
            S32 area_tex_channel = blend_weights_shader.enableTexture(LLShaderMgr::SMAA_AREA_TEX);
            if (area_tex_channel > -1)
            {
                gGL.getTextureSlot(area_tex_channel)->bindManual(ALTextureSlot::TT_TEXTURE, mSMAAAreaMap,
                                                             gGL.getSampler(ALSamplers::BilinearClamp));
            }
            S32 search_tex_channel = blend_weights_shader.enableTexture(LLShaderMgr::SMAA_SEARCH_TEX);
            if (search_tex_channel > -1)
            {
                gGL.getTextureSlot(search_tex_channel)->bindManual(ALTextureSlot::TT_TEXTURE, mSMAASearchMap,
                                                               gGL.getSampler(ALSamplers::BilinearClamp));
            }

            if (use_stencil)
            {
                glStencilFunc(GL_EQUAL, 1, 0xFF);
                glStencilMask(0x00);
            }
            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            if (use_stencil)
            {
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glStencilMask(0xFF);
            }
            blend_weights_shader.unbind();
            dest.flush();
            gGL.getTextureSlot(edge_tex_channel)->unbindFast();
            gGL.getTextureSlot(area_tex_channel)->unbindFast();
            gGL.getTextureSlot(search_tex_channel)->unbindFast();
        }
    }
    return multisample ? smaa : SMAATargets{};
}

bool LLPipeline::applySMAA(LLRenderTarget* src, LLRenderTarget* dst, const SMAATargets& smaa)
{
    LL_PROFILE_GPU_ZONE("SMAA");
    llassert(!gCubeSnapshot);
    if (!smaa.mBlend)
    {
        return false;
    }

    {
        static LLCachedControl<U32> aa_quality(gSavedSettings, "RenderFSAASamples", 0U);
        U32 fsaa_quality = std::clamp(aa_quality(), 0U, 3U);

        S32 width = src->getWidth();
        S32 height = src->getHeight();

        float rt_metrics[] = { 1.f / width, 1.f / height, (float)width, (float)height };

        LLGLDepthTest    depth(GL_FALSE, GL_FALSE);

        static LLCachedControl<bool> use_sample(gSavedSettings, "RenderSMAAUseSample", false);

        {
            // Bind setup:
            LLRenderTarget* bound_target = dst;
            LLGLSLShader& blend_shader = gSMAANeighborhoodBlendProgram[fsaa_quality];

            bound_target->bindTarget();
            bound_target->clear(GL_COLOR_BUFFER_BIT);

            blend_shader.bind();
            blend_shader.uniform4fv(LLShaderMgr::SMAA_RT_METRICS, 1, rt_metrics);

            S32 diffuse_channel = blend_shader.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);
            if(diffuse_channel > -1)
            {
                src->bindTexture(0, diffuse_channel, ALSamplers::BilinearClamp);
            }

            S32 blend_channel = blend_shader.enableTexture(LLShaderMgr::SMAA_BLEND_TEX);
            if (blend_channel > -1)
            {
                smaa.mBlend->bindTexture(0, blend_channel, ALSamplers::BilinearMirror);
            }

            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            bound_target->flush();
            blend_shader.unbind();
            gGL.getTextureSlot(diffuse_channel)->unbindFast();
            gGL.getTextureSlot(blend_channel)->unbindFast();
        }
    }
    return true;
}

void LLPipeline::combineGlow(LLRenderTarget* src, LLRenderTarget* dst, LLRenderTarget* glow)
{
    LL_PROFILE_GPU_ZONE("glow combine");

    // Go ahead and do our glow combine here in our destination.  We blit this later into the front buffer.
    dst->bindTarget();

    {

        gGlowCombineProgram.bind();

        gGlowCombineProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, src);
        gGlowCombineProgram.bindTexture(LLShaderMgr::DEFERRED_EMISSIVE, glow);

        mScreenTriangleVB->setBuffer();
        mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    }

    dst->flush();
}

// Depth of field, run pre-tonemap on linear HDR, from mRT->screen into a target
// of the screen's description from `scope`, which it returns; null where it does
// not run.
//
// The two passes are a reduced-resolution gather blur, then a combine that mixes
// the blur with the sharp scene by CoF. Both work the signed circle of
// confusion out from the scene's depth wherever they read it
// (dofCoFUtilF.glsl), so nothing carries it between them.
//
// The combine writes every channel, the scene's alpha as the scene holds it.
// mRT->screen.a carries the legacy alpha-tagged prim glow, which bloomExtractF
// reads in the HDR path and -- after colorCorrect passes alpha straight
// through -- glowExtractF reads as its *only* live key in the non-HDR path, so
// the defocused scene has to carry it exactly for every alpha contract in the
// chain to hold.
LLRenderTarget* LLPipeline::renderDoF(ALTransientTargets& scope)
{
    LL_PROFILE_GPU_ZONE("dof");
    {
        static LLCachedControl<bool> RenderDepthOfFieldInEditMode(gSavedSettings, "RenderDepthOfFieldInEditMode", false);
        static LLCachedControl<bool> RenderFocusPointLocked(gSavedSettings, "RenderFocusPointLocked", false);
        static LLCachedControl<bool> RenderFocusPointFollowsPointer(gSavedSettings, "RenderFocusPointFollowsPointer", false);
        bool dof_enabled =
            (RenderDepthOfFieldInEditMode || !LLToolMgr::getInstance()->inBuildMode()) &&
            RenderDepthOfField &&
            !gCubeSnapshot;

        gViewerWindow->setup3DViewport();

        if (dof_enabled)
        {
            LLGLDisable blend(GL_BLEND);

            // The defocused scene, in the screen's own description, over the
            // scene's depth, which neither pass tests or writes: from the
            // caller's scope, until the frame is presented.
            LLRenderTarget* out = acquireResource(scope, ALRenderResourcePlan::Id::MainScreen, "dof.out");

            // The blur runs at CameraDoFResScale of the screen, in a target of
            // exactly that size, for the effect alone: in bloom mip 0's format
            // under HDR, R11F_G11F_B10F (RGBA16F with halation), and the post
            // chain's RGBA8 without, which the gather fills with sqrt(colour)
            // (DOF_SQRT_ENCODED), since 8-bit linear bands near black. It
            // carries no alpha.
            const bool hdr = hdrEnabled();
            const std::optional<ALRenderTargetDesc>& blur_like =
                mResourcePlan.desc(hdr ? ALRenderResourcePlan::bloomMip(ALRenderResourcePlan::Pack::Main, 0)
                                       : ALRenderResourcePlan::Id::MainPostPong);
            const F32 dof_scale  = llmin((F32)CameraDoFResScale, 1.f);
            const U32 dof_width  = llmax(1u, (U32)(mRT->screen.getWidth() * dof_scale));
            const U32 dof_height = llmax(1u, (U32)(mRT->screen.getHeight() * dof_scale));
            std::optional<ALRenderTargetDesc> blur_desc;
            if (blur_like)
            {
                blur_desc = ALRenderTargetDesc{ .width = dof_width, .height = dof_height, .color = { blur_like->color[0] } };
            }
            // Another scale is another description, and the blur at the last one is wanted no more: freed now, so a
            // drag of the slider through its steps holds one blur at a time, not one a step until each is trimmed. One
            // the plan has a resource of, as bloom level 0's at full scale, stays for that resource.
            if (mDoFBlurDesc && mDoFBlurDesc != blur_desc && !mResourcePlan.describes(*mDoFBlurDesc))
            {
                mTransientTargets.trim(*mDoFBlurDesc);
            }
            mDoFBlurDesc = blur_desc;
            ALTransientTargets dof(mTransientTargets);
            LLRenderTarget*    blur = blur_desc ? dof.acquire(*blur_desc, "dof.blur") : nullptr;
            if (!out || !blur)
            {
                return nullptr;
            }

            // depth of field focal plane calculations
            static F32 current_distance = 16.f;
            static F32 start_distance = 16.f;
            static F32 transition_time = 1.f;

            LLVector3 focus_point;
            static LLVector3 last_focus_point{};
            if (RenderFocusPointLocked && !last_focus_point.isExactlyZero())
            {
                focus_point = last_focus_point;
            }
            else
            {
                LLViewerObject* obj = LLViewerMediaFocus::getInstance()->getFocusedObject();
                if (obj && obj->mDrawable && obj->isSelected())
                { // focus on selected media object
                    S32 face_idx = LLViewerMediaFocus::getInstance()->getFocusedFace();
                    if (obj && obj->mDrawable)
                    {
                        LLFace* face = obj->mDrawable->getFace(face_idx);
                        if (face)
                        {
                            focus_point = face->getPositionAgent();
                        }
                    }
                }

                if (focus_point.isExactlyZero())
                {
                    if (LLViewerJoystick::getInstance()->getOverrideCamera() || RenderFocusPointFollowsPointer)
                    { // focus on point under cursor
                        focus_point.set(gDebugRaycastIntersection.getF32ptr());
                    }
                    else if (gAgentCamera.cameraMouselook())
                    { // focus on point under mouselook crosshairs
                        LLVector4a result;
                        result.clear();

                        gViewerWindow->cursorIntersect(-1, -1, 512.f, nullptr, -1, false, false, true, true, nullptr, &result);

                        focus_point.set(result.getF32ptr());
                    }
                    else
                    {
                        // focus on alt-zoom target
                        LLViewerRegion* region = gAgent.getRegion();
                        if (region)
                        {
                            focus_point = LLVector3(gAgentCamera.getFocusGlobal() - region->getOriginGlobal());
                        }
                    }
                }
            }
            last_focus_point = focus_point;

            LLVector3 eye = LLViewerCamera::getInstance()->getOrigin();
            F32 target_distance = 16.f;
            if (!focus_point.isExactlyZero())
            {
                target_distance = LLViewerCamera::getInstance()->getAtAxis() * (focus_point - eye);
            }

            if (transition_time >= 1.f && fabsf(current_distance - target_distance) / current_distance > 0.01f)
            { // large shift happened, interpolate smoothly to new target distance
                transition_time = 0.f;
                start_distance = current_distance;
            }
            else if (transition_time < 1.f)
            { // currently in a transition, continue interpolating
                transition_time += 1.f / CameraFocusTransitionTime * gFrameIntervalSeconds.value();
                transition_time = llmin(transition_time, 1.f);

                F32 t = cosf(transition_time * F_PI + F_PI) * 0.5f + 0.5f;
                current_distance = start_distance + (target_distance - start_distance) * t;
            }
            else
            { // small or no change, just snap to target distance
                current_distance = target_distance;
            }

            // convert to mm
            F32 subject_distance = current_distance * 1000.f;
            F32 fnumber = CameraFNumber;
            F32 default_focal_length = CameraFocalLength;

            F32 fov = LLViewerCamera::getInstance()->getView();

            const F32 default_fov = CameraFieldOfView * F_PI / 180.f;

            // F32 aspect_ratio = (F32) mRT->screen.getWidth()/(F32)mRT->screen.getHeight();

            F32 dv = 2.f * default_focal_length * tanf(default_fov / 2.f);

            F32 focal_length = dv / (2 * tanf(fov / 2.f));

            // F32 tan_pixel_angle = tanf(LLDrawable::sCurPixelAngle);

            // from wikipedia -- c = |s2-s1|/s2 * f^2/(N(S1-f))
            // where     N = fnumber
            //           s2 = dot distance
            //           s1 = subject distance
            //           f = focal length
            //

            F32 blur_constant = focal_length * focal_length / (fnumber * (subject_distance - focal_length));
            blur_constant /= 1000.f; // convert to meters for shader
            F32 magnification = focal_length / (subject_distance - focal_length);

            // The circle of confusion's, which both passes work it out from.
            const F32  tan_pixel_angle = tanf(1.f / LLDrawable::sCurPixelAngle);
            const auto set_cof_uniforms = [&](LLGLSLShader& program)
            {
                program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mRT->deferredScreen, ALSamplers::PointMirror);
                program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (GLfloat)mRT->screen.getWidth(), (GLfloat)mRT->screen.getHeight());
                program.uniform1f(LLShaderMgr::DOF_FOCAL_DISTANCE, -subject_distance / 1000.f);
                program.uniform1f(LLShaderMgr::DOF_BLUR_CONSTANT, blur_constant);
                program.uniform1f(LLShaderMgr::DOF_TAN_PIXEL_ANGLE, tan_pixel_angle);
                program.uniform1f(LLShaderMgr::DOF_MAGNIFICATION, magnification);
                program.uniform1f(LLShaderMgr::DOF_MAX_COF, CameraMaxCoF);
            };

            // The fraction of the screen the blur covers, for the CoF arithmetic.
            const F32 res_scale = llmin((F32)dof_width / (F32)mRT->screen.getWidth(),
                                        (F32)dof_height / (F32)mRT->screen.getHeight());

            { // gather blur at CameraDoFResScale into its own target, every texel of it
                blur->bindTarget();

                static LLCachedControl<bool> RenderDepthOfFieldNearBlur(gSavedSettings, "RenderDepthOfFieldNearBlur", false);

                // Shaped aperture, anamorphic deformation, optical vignetting,
                // defocus fringing and the two aberrations all live in the
                // innermost sample loop,
                // so they are compiled out rather than branched over. One
                // define covers them all: the shaped variant is bound only
                // when at least one is actually doing something, and within it
                // each gates on its own uniform the way the lens flare's
                // sub-effects do. `shaped` below must stay in lockstep with
                // the effects inside the shader's DOF_SHAPED block -- an
                // effect missing from it is a dead control whenever it is the
                // only one active.
                static LLCachedControl<S32> bokeh_blades(gSavedSettings, "RenderBokehApertureBlades", 0);
                static LLCachedControl<F32> bokeh_rotation(gSavedSettings, "RenderBokehApertureRotation", 0.f);
                static LLCachedControl<F32> bokeh_curvature(gSavedSettings, "RenderBokehApertureCurvature", 0.f);
                static LLCachedControl<F32> bokeh_anamorphic(gSavedSettings, "RenderBokehAnamorphicSqueeze", 1.f);
                static LLCachedControl<F32> bokeh_cat_eye(gSavedSettings, "RenderBokehCatEyeAmount", 0.f);
                static LLCachedControl<F32> bokeh_fringe(gSavedSettings, "RenderBokehFringeAmount", 0.f);
                static LLCachedControl<LLColor3> bokeh_fringe_near(gSavedSettings, "RenderBokehFringeNearTint", LLColor3(1.f, 0.85f, 1.f));
                static LLCachedControl<LLColor3> bokeh_fringe_far(gSavedSettings, "RenderBokehFringeFarTint", LLColor3(0.85f, 1.f, 0.9f));
                static LLCachedControl<F32> bokeh_spherical(gSavedSettings, "RenderBokehSphericalAberration", 0.f);
                static LLCachedControl<F32> bokeh_field(gSavedSettings, "RenderBokehFieldStretch", 0.f);
                static LLCachedControl<F32> bokeh_field_falloff(gSavedSettings, "RenderBokehFieldFalloff", 2.f);
                static LLCachedControl<F32> bokeh_coma(gSavedSettings, "RenderBokehComaAsymmetry", 0.f);

                const S32 blades  = llclamp(bokeh_blades(), 0, 11);
                const F32 cat_eye = llclamp(bokeh_cat_eye(), 0.f, 1.5f);
                const F32 fringe  = llclamp(bokeh_fringe(), 0.f, 1.f);
                const F32 squeeze = llclamp(bokeh_anamorphic(), 0.25f, 4.f);
                const F32 spherical = llclamp(bokeh_spherical(), -1.f, 1.f);
                const F32 field     = llclamp(bokeh_field(), -1.f, 1.f);
                const F32 coma      = llclamp(bokeh_coma(), 0.f, 1.f);
                const bool anamorphic = (squeeze < 0.999f) || (squeeze > 1.001f);
                // Comatic asymmetry earns its place here even though it reads
                // like a modifier: it biases the disc along the field
                // direction, which exists whether or not anything stretched
                // it, so it is a standalone effect rather than a shape control
                // for the stretch. RenderBokehFieldFalloff genuinely is one and
                // is deliberately absent.
                const bool shaped = (blades >= 3) || (cat_eye > 0.f) || (fringe > 0.f) || anamorphic
                                 || (spherical != 0.f) || (field != 0.f) || (coma > 0.f);

                LLGLSLShader& post_program = RenderDepthOfFieldNearBlur
                    ? (shaped ? gDeferredPostProgramShaped : gDeferredPostProgram)
                    : (shaped ? gDeferredPostProgramNoNearShaped : gDeferredPostProgramNoNear);

                // A gather or a combine that did not build would leave the blur, or the defocused scene the chain reads
                // in the screen's place, as the pool holds it: the frame goes on in focus instead.
                const LLGLSLShader& combine_program =
                    RenderDepthOfFieldNearBlur ? gDeferredDoFCombineProgram : gDeferredDoFCombineProgramNoNear;
                if (!post_program.isComplete() || !combine_program.isComplete())
                {
                    blur->flush();
                    return nullptr;
                }

                post_program.bind();
                post_program.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mRT->screen, ALSamplers::PointMirror);
                set_cof_uniforms(post_program);

                // Gather weighting. Defaults are a plain energy-conserving
                // average plus a firefly ceiling; the highlight boost is
                // opt-in. See the note above dofSample for why the old
                // `0.25 + r+g+b` weight could not survive the move to linear.
                static LLCachedControl<F32> bokeh_threshold(gSavedSettings, "RenderBokehHighlightThreshold", 0.f);
                static LLCachedControl<F32> bokeh_gain(gSavedSettings, "RenderBokehHighlightGain", 0.f);
                static LLCachedControl<F32> bokeh_clamp(gSavedSettings, "RenderBokehHighlightClamp", 64.f);
                post_program.uniform1f(LLShaderMgr::BOKEH_HIGHLIGHT_THRESHOLD, llmax(bokeh_threshold(), 0.f));
                post_program.uniform1f(LLShaderMgr::BOKEH_HIGHLIGHT_GAIN, llmax(bokeh_gain(), 0.f));
                post_program.uniform1f(LLShaderMgr::BOKEH_HIGHLIGHT_CLAMP, llmax(bokeh_clamp(), 0.f));

                // Shaped-aperture uniforms. Skipped entirely for the unshaped
                // programs, where they are not in the linked binary anyway --
                // the setters would no-op, but the sector bake would still run.
                if (shaped)
                {
                    // A regular N-gon's inscribed radius at angle theta is
                    //   cos(pi/N) / cos(mod(theta + rot, 2pi/N) - pi/N)
                    // so the sector geometry is baked once here and the loop is
                    // left with the single cosine it genuinely needs per sample.
                    const F32 sides       = (F32)llmax(blades, 3);
                    const F32 half_sector = F_PI / sides;
                    post_program.uniform1i(LLShaderMgr::BOKEH_BLADES, blades);
                    post_program.uniform1f(LLShaderMgr::BOKEH_APERTURE_ROTATION,
                                           llclamp(bokeh_rotation(), 0.f, 360.f) * DEG_TO_RAD);
                    post_program.uniform1f(LLShaderMgr::BOKEH_APERTURE_CURVATURE, llclamp(bokeh_curvature(), 0.f, 1.f));
                    post_program.uniform3f(LLShaderMgr::BOKEH_APERTURE_CONST,
                                           half_sector, 2.f * half_sector, cosf(half_sector));
                    // Anamorphic stretch, sent area-preserving: the two axes
                    // multiply to 1, so the slider changes the shape of the
                    // blur without also changing how much of it there is.
                    // Above 1 is taller than wide, the classic anamorphic oval;
                    // below 1 is wider than tall.
                    const F32 anam_root = sqrtf(squeeze);
                    post_program.uniform2f(LLShaderMgr::BOKEH_ANAMORPHIC, 1.f / anam_root, anam_root);
                    post_program.uniform1f(LLShaderMgr::BOKEH_CAT_EYE, cat_eye);
                    post_program.uniform1f(LLShaderMgr::BOKEH_FRINGE_AMOUNT, fringe);
                    post_program.uniform3fv(LLShaderMgr::BOKEH_FRINGE_NEAR_TINT, 1, bokeh_fringe_near().mV);
                    post_program.uniform3fv(LLShaderMgr::BOKEH_FRINGE_FAR_TINT, 1, bokeh_fringe_far().mV);

                    // Aberrations. Spherical goes up raw: the shader folds in
                    // both the sign of the circle of confusion and the
                    // blur-size fade, because both depend on the fragment
                    // rather than on the frame.
                    post_program.uniform1f(LLShaderMgr::BOKEH_SPHERICAL, spherical);
                    post_program.uniform1f(LLShaderMgr::BOKEH_FIELD_STRETCH, field);
                    post_program.uniform1f(LLShaderMgr::BOKEH_FIELD_FALLOFF,
                                           llclamp(bokeh_field_falloff(), 1.f, 4.f));
                    post_program.uniform1f(LLShaderMgr::BOKEH_COMA_ASYMMETRY, coma);
                }

                mScreenTriangleVB->setBuffer();
                mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

                post_program.unbind();

                blur->flush();
            }

            { // combine result based on CoF, into the defocused scene, every texel of it
                static LLCachedControl<bool> RenderDepthOfFieldNearBlur(gSavedSettings, "RenderDepthOfFieldNearBlur", false);
                LLGLSLShader& combine_program = RenderDepthOfFieldNearBlur ? gDeferredDoFCombineProgram : gDeferredDoFCombineProgramNoNear;

                out->bindTarget();

                combine_program.bind();
                // The blur is smaller than the screen (CameraDoFResScale), so it
                // is upsampled filtered: point sampling repeats its texels in an
                // irregular pattern that shimmers as the CoF moves. Its target is
                // exactly its size, so the edge clamp keeps every tap inside it.
                combine_program.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, blur, ALSamplers::BilinearClamp);
                combine_program.bindTexture(LLShaderMgr::DEFERRED_LIGHT, &mRT->screen, ALSamplers::PointMirror);
                set_cof_uniforms(combine_program);
                combine_program.uniform1f(LLShaderMgr::DOF_RES_SCALE, res_scale);

                mScreenTriangleVB->setBuffer();
                mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

                combine_program.unbind();

                out->flush();
            }
            return out;
        }
    }
    return nullptr;
}

void LLPipeline::renderFinalize()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("renderFinalize");

    llassert(!gCubeSnapshot);
    LLVertexBuffer::unbind();
    LLGLState::checkStates();

    assertInitialized();

    gGL.color4f(1, 1, 1, 1);
    LLGLDepthTest depth(GL_FALSE);
    LLGLDisable blend(GL_BLEND);
    LLGLDisable cull(GL_CULL_FACE);

    enableLightsFullbright();

    gGL.setColorMask(true, true);
    gGL.setClearColor(LLColor4::transparent);

    const bool hdr = hdrEnabled();

    // A cutout frame measures nothing and leaves nothing behind. It is mostly
    // empty, so metering it would brighten the subject, and the next live frame
    // would reflect and meter a world with everything but the subject cut out.
    // The last live frame's exposure serves, as it does for the scopes.
    //
    // Everything that spreads past a pixel is left out as well -- depth of
    // field, bloom, glow, anti-aliasing and sharpening -- since each carries
    // the subject's edge into what is to be left empty, where the coverage
    // written after the frame (copyCutoutCoverageToAlpha) knows nothing of it.
    // Edges come from rendering the cutout larger and averaging it down.
    const bool cutout = sCutoutRender;

    if (hdr && !cutout)
    {
        copyScreenSpaceReflections(&mRT->screen, &mSceneMap);

        generateLuminance(&mRT->screen, &mLuminanceMap);

        generateExposure(&mLuminanceMap, &mExposureMap);
    }

    // Lens flare sun state (see lensFlareStateF.glsl), in both HDR paths.
    generateLensFlareState(&mRT->screen);

    // Read any pending scene probe here, while the buffer still holds linear
    // radiance and is still sharp. One line later it has been white balanced,
    // graded and tonemapped, and a sample taken then would describe the grade
    // rather than the scene -- which is no use to a tool whose whole job is to
    // decide what the grade should be. It has to precede DoF for the same
    // reason: a sample from a defocused pixel describes the blur, not the scene.
    // Nor from a cutout, which describes one subject on nothing.
    if (!cutout)
    {
        serviceScenePixelProbe(&mRT->screen);
    }

    // Depth of field, from mRT->screen into a target of the pool's, which the
    // rest of the chain reads in its place: the screen keeps the scene as it
    // was, for the reflections to trace next frame.
    //
    // Ahead of both the tonemapper and bloom. Pre-tonemap because gathering
    // over display-space values is gathering over already-compressed
    // highlights, which is why stock bokeh reads flat and why postDeferredF
    // carried a weighting hack to fake the pop back. Pre-bloom because that is
    // the optical order -- defocus happens at the aperture, veiling glare in
    // the glass after it -- so a defocused highlight blooms as a soft disc
    // instead of a sharp core floating on a blurred background.
    //
    // The residual, worth knowing before chasing it: the legacy alpha-tagged
    // glow term reads the sharp glow tag over blurred RGB, so a defocused
    // glowing prim's alpha-glow contribution hugs its sharp silhouette. The
    // RGB-threshold term, which dominates in HDR, follows the blur correctly.
    // If that reads badly in world, swapping this block with generateBloomHDR
    // below restores bloom-first, at the cost of sharp bloom cores on
    // defocused lights.
    //
    // SSR, luminance and exposure stay above deliberately: reflections keep
    // their detail, and a blur conserves mean energy so metering is unaffected.
    //
    // The defocused scene, what the bloom makes for colorCorrect and the post
    // chain's targets are all held until the frame is presented.
    ALTransientTargets post(mTransientTargets);
    LLRenderTarget*    scene = &mRT->screen;
    static LLCachedControl<bool> RenderDepthOfFieldInEditMode(gSavedSettings, "RenderDepthOfFieldInEditMode", false);
    if (RenderDepthOfField && (RenderDepthOfFieldInEditMode || !LLToolMgr::getInstance()->inBuildMode()) && !gCubeSnapshot && !cutout)
    {
        if (LLRenderTarget* defocused = renderDoF(post))
        {
            scene = defocused;
        }

        // renderDoF calls setup3DViewport and runs a reduced viewport
        // internally, so restore the world view before anything else draws.
        gGLViewport[0] = gViewerWindow->getWorldViewRectRaw().mLeft;
        gGLViewport[1] = gViewerWindow->getWorldViewRectRaw().mBottom;
        gGLViewport[2] = gViewerWindow->getWorldViewRectRaw().getWidth();
        gGLViewport[3] = gViewerWindow->getWorldViewRectRaw().getHeight();
        glViewport(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]);
    }

    Bloom bloom;
    if (hdr && !cutout)
    {
        // HDR bloom runs pre-tonemap against the linear scene buffer -- now the
        // defocused one. The pyramid is generated here; the additive composite
        // is folded into colorCorrect's tonemap variants (BLOOM_COMPOSITE
        // permutation) so we avoid a separate fullscreen pass over the scene
        // buffer. The legacy alpha-tagged prim-glow signal is carried into the
        // extract pass, so prim glow survives the migration.
        bloom = generateBloomHDR(scene, post);
    }

    // Handles tonemap, colorgrading, and gamma correction in one pass. In the HDR
    // path, this also applies eye adaptation and bloom. In the non-HDR path, this
    // is just a linear copy with color correction.
    // Ahead of colorCorrect, which samples the plate, and outside the bloom
    // block above because the dirt is lit by the lens flare as well as by bloom.
    generateLensDirt();

    // The post chain's first target, from the pool until the frame is presented, which colorCorrect writes whole.
    // Without it, the frame goes out as the scene stands rather than not at all.
    LLRenderTarget* const post_ping = acquireResource(post, ALRenderResourcePlan::Id::MainPostPing);
    const bool            chain     = post_ping != nullptr;
    if (chain)
    {
        colorCorrect(scene, post_ping, hdr, true, &bloom);
    }

    LLVertexBuffer::unbind();

    // Legacy glow for non-HDR path.  In the HDR path, glow is extracted as part of the
    // bloom process and composited back in after tonemapping.
    LLRenderTarget* glow = !hdr && !cutout && chain ? generateGlow(post_ping, post) : nullptr;

    // The chain's other target, from the pool once a pass after colorCorrect draws, each such pass writing it whole
    // before the two swap. A pass with no target to draw into is left out.
    LLRenderTarget* sourceBuffer = chain ? post_ping : scene;
    LLRenderTarget* targetBuffer = nullptr;
    const auto      other_target = [&]() -> LLRenderTarget*
    {
        if (!targetBuffer && chain)
        {
            targetBuffer = acquireResource(post, ALRenderResourcePlan::Id::MainPostPong);
        }
        return targetBuffer;
    };
    SMAATargets smaa;

    // Anti-aliasing and sharpening both spread past a pixel, so a cutout
    // frame skips them (see above). A pass that draws nothing leaves the frame
    // where it was, and the next reads it there.
    if (mResourceInputs.mFSAAType == 1 && !cutout && other_target())
    {
        if (applyFXAA(sourceBuffer, targetBuffer))
        {
            std::swap(sourceBuffer, targetBuffer);
        }
    }
    else if (mResourceInputs.mFSAAType == 2 && !cutout && other_target())
    {
        smaa = generateSMAABuffers(sourceBuffer, post);
        if (applySMAA(sourceBuffer, targetBuffer, smaa))
        {
            std::swap(sourceBuffer, targetBuffer);
        }
    }

    // The print effects had no snapshot gate at all, so "No post-processing"
    // produced an image that was still vignetted, still grainy, still
    // CVD-compensated and, with a preview mode on, still false-coloured.
    // They are the print effects; a clean plate is exactly the frame
    // without them.
    //
    // Dither is not in the list on purpose. It is a quantisation aid rather
    // than a look, and an 8-bit PNG wants it whether or not the rest is
    // wanted.
    const bool suppress_effects = suppressesLensAndPrintEffects();

    static LLCachedControl<F32> distort_amount(gSavedSettings, "RenderLensDistortionAmount", 0.0f, "[0, 1] default 0.");
    const F32 distort = suppress_effects ? 0.f : llclamp(distort_amount(), 0.f, 1.f);

    // Sharpening. Where the blit reads the frame next and draws it one to one, through no lens and in no side-by-side
    // pane, the blit sharpens as it samples (blitWithEffectsF.glsl): CAS's pass is left out, and the chain's other
    // target with it where nothing else draws into one. CAS's pass sharpens the frame instead where something reads or
    // draws over it before the blit -- the glow, RLVa's sphere, a buffer shown in its place, a scope sample or a
    // reference still taken this frame -- and where the blit resamples it, at another size or through the lens, which
    // costs the blit more to sharpen than the pass does.
    static LLCachedControl<F32> cas_sharpness(gSavedSettings, "RenderCASSharpness", 0.4f);
    static LLCachedControl<S32> ref_wipe_mode(gSavedSettings, "RenderReferenceWipeMode", 0);
    const LLRect& world_view    = gViewerWindow->getWorldViewRectRaw();
    const bool    rlv_sphere    = chain && RlvActions::hasBehaviour(RLV_BHVR_SETSPHERE);
    const bool    visualize     = RenderBufferVisualization > -1 && !cutout;
    const bool    side_by_side  = hasReferenceStill() && ref_wipe_mode == 2 && !suppress_effects;
    const bool    blit_sharpens = cas_sharpness > 0.f && !cutout && chain && gBlitWithEffectsSharpenProgram.isComplete() &&
                                  !glow && !rlv_sphere && !visualize && !sScopeCapture && !mReferenceStillWanted &&
                                  distort <= 0.f && !side_by_side && (S32)sourceBuffer->getWidth() == world_view.getWidth() &&
                                  (S32)sourceBuffer->getHeight() == world_view.getHeight();
    if (!blit_sharpens && cas_sharpness > 0.f && gCASProgram.isComplete() && !cutout && other_target() &&
        applyCAS(sourceBuffer, targetBuffer))
    {
        std::swap(sourceBuffer, targetBuffer);
    }

    // The other target is the pool's, its contents undefined until a pass writes it: a pass whose program did not build
    // draws nothing into it, and the frame stays where it is rather than going out as that target holds it.
    if (glow && gGlowCombineProgram.isComplete() && other_target())
    {
        combineGlow(sourceBuffer, targetBuffer, glow);
        std::swap(sourceBuffer, targetBuffer);
    }

    gGLViewport[0] = gViewerWindow->getWorldViewRectRaw().mLeft;
    gGLViewport[1] = gViewerWindow->getWorldViewRectRaw().mBottom;
    gGLViewport[2] = gViewerWindow->getWorldViewRectRaw().getWidth();
    gGLViewport[3] = gViewerWindow->getWorldViewRectRaw().getHeight();
    glViewport(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]);

// [RLVa:KB] - @setsphere
    if (rlv_sphere && gRlvSphereProgram.isComplete() && other_target())
    {
        LLShaderEffectParams params(sourceBuffer, targetBuffer, false);
        LLVfxManager::instance().runEffect(EVisualEffect::RlvSphere, &params);
        // The frame is wherever the last pass left it: still sourceBuffer if no sphere is
        // enabled or the passes were even in number (one per effect, two per blur).
        if (params.m_pDstBuffer == targetBuffer)
        {
            std::swap(sourceBuffer, targetBuffer);
        }
    }
// [/RLVa:KB]

    if (RenderBufferVisualization > -1 && !cutout)
    {
        switch (RenderBufferVisualization)
        {
        case 0:
        case 1:
        case 2:
        case 3:
            visualizeBuffers(&mRT->deferredScreen, sourceBuffer, RenderBufferVisualization);
            break;
        case 4:
            visualizeBuffers(&mLuminanceMap, sourceBuffer, 0);
            break;
        case 5:
        {
            if (smaa.mEdges)
            {
                visualizeBuffers(smaa.mEdges, sourceBuffer, 0);
            }
            break;
        }
        case 6:
        {
            if (smaa.mBlend)
            {
                visualizeBuffers(smaa.mBlend, sourceBuffer, 0);
            }
            break;
        }
        default:
            break;
        }
    }

    // Measure what is about to be presented. After every post pass, before
    // the vignette/grain/dither the final blit adds -- those are print
    // effects, deliberately outside the measurement. A cutout frame is not
    // what the scopes describe.
    if (!cutout)
    {
        captureScopeSample(sourceBuffer);

        // Same point, same reason: a still and the scopes must agree about what a
        // frame is, or the numbers will not describe the picture beside them.
        captureReferenceStill(sourceBuffer);
    }

    // Present the screen target.
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("renderFinalize - final blit");
        LL_PROFILE_GPU_ZONE("renderFinalize - final blit");

        LLGLSLShader& blit = blit_sharpens ? gBlitWithEffectsSharpenProgram : gBlitWithEffectsProgram;
        blit.bind();

        // Whatever is last in the above post processing chain should _always_ be rendered directly here.  If not, expect problems.
        blit.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, sourceBuffer);
        blit.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mRT->deferredScreen);

        // Setup Uniforms
        blit.uniform1ui(LLShaderMgr::FRAME_ID, LLFrameTimer::getFrameCount());
        blit.uniform2f(LLShaderMgr::SCREEN_RESOLUTION, (F32)gViewerWindow->getWorldViewRectRaw().getWidth(),
                       (F32)gViewerWindow->getWorldViewRectRaw().getHeight());

        // CAS, sharpening the frame at its own size as its pass does.
        if (blit_sharpens)
        {
            varAU4(const0);
            varAU4(const1);
            CasSetup(const0, const1, cas_sharpness(), (AF1)sourceBuffer->getWidth(), (AF1)sourceBuffer->getHeight(),
                     (AF1)sourceBuffer->getWidth(), (AF1)sourceBuffer->getHeight());
            blit.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, const0);
            blit.uniform4uiv(LLShaderMgr::CAS_PARAM_1, 1, const1);
        }

        // Lens distortion
        //
        // Precompute shader-friendly forms once on the CPU: fold the master
        // amount into every coefficient, pre-reciprocate the squeeze, and
        // solve the auto-fit scale. The shader is then one gate followed by
        // pure polynomial evaluation -- no per-pixel divides, no solve.
        //
        // Sign convention, worth stating because it is easy to get backwards:
        // this is a gather, so the shader asks "where does this output pixel
        // come from". Under that map a coefficient below 1 pulls the sample
        // toward the centre, which stretches the middle of the source out to
        // the frame edge -- barrel. So negative k1 reads as barrel and
        // positive as pincushion, matching a lens profile, and it is
        // *pincushion* that pushes samples off the source and would show
        // black corners without a fit.
        static LLCachedControl<F32> distort_k1(gSavedSettings, "RenderLensDistortionK1", -0.2f);
        static LLCachedControl<F32> distort_k2(gSavedSettings, "RenderLensDistortionK2", 0.0f);
        static LLCachedControl<F32> distort_squeeze(gSavedSettings, "RenderLensDistortionSqueeze", 1.0f);
        static LLCachedControl<S32> distort_fit(gSavedSettings, "RenderLensDistortionFit", 1);
        static LLCachedControl<LLVector3> distort_center(gSavedSettings, "RenderLensDistortionCenter", LLVector3(0.f, 0.f, 0.f));
        static LLCachedControl<LLVector3> distort_tangential(gSavedSettings, "RenderLensDistortionTangential", LLVector3(0.f, 0.f, 0.f));

        blit.uniform1f(LLShaderMgr::LENS_DISTORT_AMOUNT, distort);

        // Zeroing the master both hits the shader's early-out and skips the
        // whole solve below, exactly as the lens flare does with its strength.
        if (distort > 0.f)
        {
            // Every shape parameter fades with the master amount, each toward
            // its own neutral: the polynomial coefficients and decentering
            // toward 0, the squeeze toward 1. The squeeze included -- scaling
            // only the coefficients would have the radial bend fade in
            // smoothly while a non-neutral stretch snapped on in one frame.
            const F32 k1 = llclamp(distort_k1(), -0.5f, 0.5f) * distort;
            const F32 k2 = llclamp(distort_k2(), -0.25f, 0.25f) * distort;
            const F32 p1 = llclamp(distort_tangential().mV[0], -0.05f, 0.05f) * distort;
            const F32 p2 = llclamp(distort_tangential().mV[1], -0.05f, 0.05f) * distort;
            const F32 cx = llclamp(distort_center().mV[0], -0.5f, 0.5f) * distort;
            const F32 cy = llclamp(distort_center().mV[1], -0.5f, 0.5f) * distort;
            const F32 squeeze = 1.f + (llclamp(distort_squeeze(), 0.5f, 2.5f) - 1.f) * distort;

            // Same aspect basis the shader uses, and the same one the CA path
            // derives from uResolution -- one component stays 1.0 and the
            // other carries the ratio, so radial distance is measured in
            // physical units and the corners really are further out than the
            // edge midpoints.
            const F32 res_w  = (F32)gViewerWindow->getWorldViewRectRaw().getWidth();
            const F32 res_h  = (F32)gViewerWindow->getWorldViewRectRaw().getHeight();
            const F32 aspect = res_w / llmax(res_h, 1.f);
            const F32 axis_x = llmax(aspect, 1.f);
            const F32 axis_y = llmax(1.f / llmax(aspect, 1e-4f), 1.f);
            const F32 sq_x   = 1.f / squeeze;
            const F32 sq_y   = 1.f;

            // The shader's warp with the fit scale left at 1. Scale is a pure
            // multiplier on the result, so solving for it afterwards is exact
            // rather than iterative.
            auto base_offset = [&](F32 u, F32 v, F32& out_x, F32& out_y)
            {
                const F32 qx = (u - 0.5f - cx) * axis_x;
                const F32 qy = (v - 0.5f - cy) * axis_y;
                const F32 r2 = qx * qx + qy * qy;
                const F32 radial = 1.f + r2 * (k1 + r2 * k2);
                const F32 tx = 2.f * p1 * qx * qy + p2 * (r2 + 2.f * qx * qx);
                const F32 ty = p1 * (r2 + 2.f * qy * qy) + 2.f * p2 * qx * qy;
                out_x = (qx * radial + tx) / axis_x * sq_x;
                out_y = (qy * radial + ty) / axis_y * sq_y;
            };

            // Scale at which the ray from the optical axis along `base` leaves
            // the source frame -- a two-slab exit test. Below that scale the
            // sample is inside the image; above it, off the edge and black.
            const F32 origin_x = 0.5f + cx;
            const F32 origin_y = 0.5f + cy;
            auto exit_scale = [&](F32 bx, F32 by) -> F32
            {
                F32 best = 1e30f;
                if (bx > 1e-6f || bx < -1e-6f)
                {
                    best = llmin(best, ((bx > 0.f ? 1.f : 0.f) - origin_x) / bx);
                }
                if (by > 1e-6f || by < -1e-6f)
                {
                    best = llmin(best, ((by > 0.f ? 1.f : 0.f) - origin_y) / by);
                }
                return best;
            };

            F32 fit_scale = 1.f;
            const S32 fit_mode = llclamp(distort_fit(), 0, 2);
            const LensDistortFit::Inputs inputs = { k1, k2, p1, p2, cx, cy, axis_x, axis_y, sq_x, fit_mode };
            if (inputs == mLensDistortFit.inputs)
            {
                fit_scale = mLensDistortFit.scale;
            }
            else if (fit_mode != 0)
            {
                // Walk the frame boundary densely instead of probing only the
                // corners and edge midpoints.
                //
                // With a non-zero secondary coefficient the radial polynomial
                // 1 + k1*r^2 + k2*r^4 stops being monotonic in r -- that
                // non-monotonicity *is* the moustache bend -- so the largest
                // outward displacement along an edge can fall between two
                // sparse probes. Eight probes let that region escape the solve,
                // which showed up as curved black arcs along the edges at
                // extreme settings even in Fit mode. Dense sampling is also
                // what keeps this honest once the tangential terms are
                // non-zero, since those break the clean radial structure a
                // corners-dominate argument leans on.
                //
                // Sixty-four evaluations of a short polynomial, once per change
                // of the inputs above.
                const S32 probes_per_edge = 16;

                // Fit (1): the smallest exit scale over every probe with a
                // satisfiable constraint, so nothing that scaling can save
                // ever goes black. Not quite "nothing goes black anywhere":
                // with the optical axis pinned on the frame edge, fold-over
                // can fling a pixel straight off that edge, and no positive
                // scale brings it back -- Fit degrades to best-effort there
                // rather than collapsing the whole frame chasing an
                // impossible constraint.
                // Fill (2): the largest, so every probe is reachable -- the
                // whole source stays visible, at the cost of black corners.
                F32 solved = (fit_mode == 1) ? 1e30f : 0.f;

                auto consider_probe = [&](F32 u, F32 v)
                {
                    F32 bx, by;
                    base_offset(u, v, bx, by);
                    const F32 s = exit_scale(bx, by);
                    // >= 1e30 is no constraint at all (base ~ 0 near the
                    // optical axis); ~0 is the unsatisfiable case above.
                    if (s < 1e-4f || s >= 1e30f)
                    {
                        return;
                    }
                    solved = (fit_mode == 1) ? llmin(solved, s) : llmax(solved, s);
                };

                for (S32 edge = 0; edge < 4; ++edge)
                {
                    for (S32 i = 0; i < probes_per_edge; ++i)
                    {
                        // t == 0 lands exactly on a corner, so all four corners
                        // are still probed; the rest subdivide each edge.
                        const F32 t = (F32)i / (F32)probes_per_edge;
                        switch (edge)
                        {
                            case 0:  consider_probe(t, 0.f);        break;  // top
                            case 1:  consider_probe(1.f, t);        break;  // right
                            case 2:  consider_probe(1.f - t, 1.f);  break;  // bottom
                            default: consider_probe(0.f, 1.f - t);  break;  // left
                        }
                    }
                }

                // Fit also probes the interior. Boundary-only probing assumes
                // the binding constraint lies on the frame edge, which holds
                // while the polynomial is monotonic over the frame -- but
                // strong barrel folds it over (the radial factor goes negative
                // past its turning point), and then an interior pixel can be
                // flung further than any boundary pixel: at clamp-edge settings
                // a boundary-only solve passes all 64 probes while a mid-frame
                // island escapes. A 15x15 grid catches every satisfiable
                // interior bind for a few hundred cheap evaluations. Fit only:
                // Fill takes the max, which the
                // near-axis interior would poison with huge exit scales.
                if (fit_mode == 1)
                {
                    const S32 grid = 15;
                    for (S32 gy = 1; gy <= grid; ++gy)
                    {
                        for (S32 gx = 1; gx <= grid; ++gx)
                        {
                            consider_probe((F32)gx / (F32)(grid + 1),
                                           (F32)gy / (F32)(grid + 1));
                        }
                    }
                }

                if (solved > 0.f && solved < 1e30f)
                {
                    fit_scale = llclamp(solved, 0.1f, 10.f);
                }
            }
            mLensDistortFit.inputs = inputs;
            mLensDistortFit.scale = fit_scale;

            blit.uniform2f(LLShaderMgr::LENS_DISTORT_K, k1, k2);
            blit.uniform1f(LLShaderMgr::LENS_DISTORT_SCALE, fit_scale);
            blit.uniform2f(LLShaderMgr::LENS_DISTORT_SQUEEZE, sq_x, sq_y);
            blit.uniform2f(LLShaderMgr::LENS_DISTORT_CENTER, cx, cy);
            blit.uniform2f(LLShaderMgr::LENS_DISTORT_TANGENTIAL, p1, p2);
        }

        // Vignette
        static LLCachedControl<F32> vignette_amount(gSavedSettings, "RenderVignetteAmount", 0.0f, "[0, 1] default 0.");
        static LLCachedControl<F32> vignette_radius(gSavedSettings, "RenderVignetteRadius", 1.0f);
        static LLCachedControl<F32> vignette_soft(gSavedSettings, "RenderVignetteSoft", 0.5f);
        static LLCachedControl<F32> vignette_shape(gSavedSettings, "RenderVignetteShape", 0.0f, "[0, 1] 0 circular, 1 rounded square.");
        static LLCachedControl<LLColor3>  vignette_color(gSavedSettings, "RenderVignetteColor", LLColor3(0.0f, 0.0f, 0.0f));
        static LLCachedControl<LLColor3>  vignette_mid_color(gSavedSettings, "RenderVignetteMidColor", LLColor3(0.0f, 0.0f, 0.0f));
        static LLCachedControl<F32>       vignette_mid_point(gSavedSettings, "RenderVignetteMidPoint", 0.0f);
        static LLCachedControl<LLVector3> vignette_center(gSavedSettings, "RenderVignetteCenter", LLVector3(0.0f, 0.0f, 0.0f));
        static LLCachedControl<bool>      vignette_correct_aspect(gSavedSettings, "RenderVignetteCorrectAspect", false);
        static LLCachedControl<F32>       vignette_feather(gSavedSettings, "RenderVignetteFeather", 1.0f);
        blit.uniform1f(LLShaderMgr::VIGNETTE_AMOUNT, suppress_effects ? 0.0f : llclamp(vignette_amount(), 0.0f, 1.0f));
        blit.uniform1f(LLShaderMgr::VIGNETTE_RADIUS, llclamp(vignette_radius(), 0.25f, 1.5f));
        blit.uniform1f(LLShaderMgr::VIGNETTE_SOFT, llclamp(vignette_soft(), 0.05f, 1.0f));
        blit.uniform1f(LLShaderMgr::VIGNETTE_SHAPE, llclamp(vignette_shape(), 0.0f, 1.0f));
        blit.uniform3fv(LLShaderMgr::VIGNETTE_COLOR, 1, vignette_color().mV);
        blit.uniform3fv(LLShaderMgr::VIGNETTE_MID_COLOR, 1, vignette_mid_color().mV);
        blit.uniform1f(LLShaderMgr::VIGNETTE_MID_POINT, llclamp(vignette_mid_point(), 0.0f, 1.0f));
        if (vignette_correct_aspect)
        {
            blit.uniform2f(LLShaderMgr::VIGNETTE_ASPECT, (F32)sourceBuffer->getWidth(), (F32)sourceBuffer->getHeight());
        }
        else
        {
            blit.uniform2f(LLShaderMgr::VIGNETTE_ASPECT, 1.f, 1.f);
        }
        blit.uniform2fv(LLShaderMgr::VIGNETTE_CENTER, 1, vignette_center().mV);
        blit.uniform1f(LLShaderMgr::VIGNETTE_FEATHER, llclamp(vignette_feather(), 0.2f, 4.0f));

        // CVD Compensation
        static LLCachedControl<S32> cvd_mode(gSavedSettings, "RenderCVDMode", 0);
        static LLCachedControl<F32> cvd_amount(gSavedSettings, "RenderCVDAmount", 0.0f);
        blit.uniform1i(LLShaderMgr::CVD_MODE, suppress_effects ? 0 : llclamp(cvd_mode(), 0, 3));
        blit.uniform1f(LLShaderMgr::CVD_AMOUNT, suppress_effects ? 0.0f : llclamp(cvd_amount(), 0.0f, 1.0f));

        // Film Grain
        static LLCachedControl<bool>     film_grain_animated(gSavedSettings, "RenderFilmGrainAnimated", true);
        static LLCachedControl<F32>      film_grain_amount(gSavedSettings, "RenderFilmGrainAmount", 0.0f);
        static LLCachedControl<S32>      film_grain_style(gSavedSettings, "RenderFilmGrainStyle", 0);
        static LLCachedControl<F32>      film_grain_size(gSavedSettings, "RenderFilmGrainSize", 1.0f);
        static LLCachedControl<F32>      film_grain_range(gSavedSettings, "RenderFilmGrainRange", 0.5f);
        static LLCachedControl<LLColor3> film_grain_tint(gSavedSettings, "RenderFilmGrainTint", LLColor3(1.0f, 1.0f, 1.0f));
        blit.uniform1f(LLShaderMgr::GRAIN_AMOUNT, suppress_effects ? 0.0f : llclamp(film_grain_amount(), 0.0f, 1.0f));
        blit.uniform1i(LLShaderMgr::GRAIN_STYLE, llclamp(film_grain_style(), 0, 3));
        blit.uniform1f(LLShaderMgr::GRAIN_SIZE, llclamp(film_grain_size(), 1.0f, 8.0f));
        blit.uniform1f(LLShaderMgr::GRAIN_RANGE, llclamp(film_grain_range(), 0.0f, 1.0f));
        blit.uniform3fv(LLShaderMgr::GRAIN_TINT, 1, film_grain_tint().mV);
        blit.uniform1i(LLShaderMgr::GRAIN_ANIMATE, film_grain_animated ? 1 : 0);

        // Dithering
        static LLCachedControl<bool> dither_enabled(gSavedSettings, "RenderDitherEnabled", true);
        static LLCachedControl<bool> dither_animated(gSavedSettings, "RenderDitherAnimated", true);
        blit.uniform1f(LLShaderMgr::DITHER_AMOUNT, dither_enabled() ? 1.0f : 0.0f);
        blit.uniform1i(LLShaderMgr::DITHER_BITS, LLRender::s10bitBackBuffer ? 10 : 8);
        blit.uniform1i(LLShaderMgr::DITHER_ANIMATE, dither_animated ? 1 : 0);

        // Previews
        static LLCachedControl<S32> preview_mode(gSavedSettings, "RenderEffectPreviewMode", 0);
        blit.uniform1i(LLShaderMgr::PREVIEW_MODE, suppress_effects ? 0 : llclamp(preview_mode(), 0, 7));

        // Reference still. The mode is forced off unless a still actually
        // exists, which is what lets the shader read the sampler without
        // checking, and means a mode left set from before a resize (which
        // drops the still) shows the live frame rather than a stale or unbound
        // texture. Off for a clean plate too: a snapshot of a comparison is
        // not a snapshot of the image.
        static LLCachedControl<F32> ref_wipe_pos(gSavedSettings, "RenderReferenceWipePosition", 0.5f);
        const S32 wipe_mode = (hasReferenceStill() && !suppress_effects) ? llclamp(ref_wipe_mode(), 0, 2) : 0;
        blit.uniform1i(LLShaderMgr::REFERENCE_WIPE_MODE, wipe_mode);
        blit.uniform1f(LLShaderMgr::REFERENCE_WIPE_POS, llclamp(ref_wipe_pos(), 0.f, 1.f));
        if (wipe_mode != 0)
        {
            blit.bindTexture(LLShaderMgr::REFERENCE_STILL, &mReferenceStill);
        }

        {
            LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
        }

        blit.unbind();
    }

    gGL.setSceneBlendType(LLRender::BT_ALPHA);

    if (hasRenderDebugMask(LLPipeline::RENDER_DEBUG_PHYSICS_SHAPES) && !cutout)
    {
        renderPhysicsDisplay();
    }

    /*if (LLRenderTarget::sUseFBO && !gCubeSnapshot)
    { // copy depth buffer from mRT->screen to framebuffer
        LLRenderTarget::copyContentsToFramebuffer(mRT->screen, 0, 0, mRT->screen.getWidth(), mRT->screen.getHeight(), 0, 0,
                                                  mRT->screen.getWidth(), mRT->screen.getHeight(),
                                                  GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
    }*/

    LLVertexBuffer::unbind();

    LLGLState::checkStates();

    // flush calls made to "addTrianglesDrawn" so far to stats machinery
    recordTrianglesDrawn();
}

void LLPipeline::bindBrdfLut(LLGLSLShader& shader)
{
    S32 channel = shader.enableTexture(LLShaderMgr::DEFERRED_BRDF_LUT);
    if (channel > -1)
    {
        // Clamp, not the render target's default mirrored repeat. This is a lookup table over
        // (NdotV, roughness), and both axes are only meaningful on [0,1]; inside that range
        // mirroring and clamping agree, so the distinction is invisible until a coordinate
        // leaves it. Then they disagree in the worst direction -- a roughness past 1 mirrors
        // back to a SMALLER roughness and the surface reads glossier the rougher it gets,
        // where clamping saturates at the roughest entry the table holds.
        mPbrBrdfLut.bindTexture(0, channel, ALSamplers::BilinearClamp);
    }
}

void LLPipeline::bindShadowMaps(LLGLSLShader& shader)
{
    // Depth comparison is a sampler property, not a texture one. Every shader samples these
    // as sampler2DShadow (deferred/shadowUtil.glsl), so the compare sampler is the only way
    // they are ever read -- allocateShadowBuffer no longer writes GL_TEXTURE_COMPARE_MODE
    // onto the texture objects.
    //
    // Bilinear + clamp: the LINEAR filter is what gives a depth-compare tap its hardware 2x2
    // PCF; a gather-compare (the default filter path in shadowUtil.glsl) ignores the filter
    // and fetches the four texels directly. The depth targets are single-level, so anisotropy
    // had nothing to act on -- see ALSamplers::ShadowCompare.
    //
    // The Ultra tier (PCSS) is the exception: a blocker search needs the RAW depth, which a
    // compare sampler can never return, so shadowUtil.glsl declares the maps as plain
    // sampler2D there and does the compare in-shader. The sampler has to match the declared
    // type -- reading a depth texture through a compare sampler as sampler2D (or the reverse)
    // is undefined -- so this selects on the SAME input that injects SHADOW_PCSS in
    // LLViewerShaderMgr::loadBasicShaders(), as the programs were loaded with it. Point filtering
    // because that path filters manually; textureGather ignores the filter regardless.
    //
    // NOTHING ELSE MAY ENTER THIS EXPRESSION. The declared type is fixed when the shader is
    // compiled, so any runtime term here -- render pass, quality override, gCubeSnapshot --
    // can only make the sampler disagree with a declaration that cannot follow it. Probe
    // captures do run at the lowest tier, but they get there by taking a shorter path over this
    // same sampler (shadowUtil.glsl, filterShadow's cube_snapshot branch), which is the only
    // form that choice can take without a separate program.
    const bool pcss = shaderInputs().mGlobal.mShadowFilterQuality >= 3;
    const U32 shadow_sampler = gGL.getSampler(pcss ? ALSamplers::PointClamp : ALSamplers::ShadowCompare);

    const U32 sampler_binds_before = ALTextureSlot::sSamplerBinds;
    U32 bound_units = 0;

    // A map the pass has none of -- a cascade past a capture's two, or a map that could not be built -- the shader reads
    // lit without sampling (unbuilt_shadow_maps), and its unit takes one the pass has, so each shadow sampler still sits
    // on a depth texture under the sampler it was declared for while any was built.
    const U32 unbuilt = unbuiltShadowMaps();
    const auto shadow_map = [this](U32 i) { return i < 4 ? getSunShadowTarget(i) : getSpotShadowTarget(i - 4); };
    LLRenderTarget* stand_in = nullptr;
    for (U32 i = 0; i < 6 && !stand_in; i++)
    {
        if (!(unbuilt & (1u << i)))
        {
            stand_in = shadow_map(i);
        }
    }

    for (U32 i = 0; i < 6; i++)
    {
        LLRenderTarget* shadow_target = (unbuilt & (1u << i)) ? stand_in : shadow_map(i);
        S32 channel = shader.enableTexture(LLShaderMgr::DEFERRED_SHADOW0 + i);
        if (shadow_target && channel > -1)
        {
            gGL.getTextureSlot(channel)->bind(shadow_target, true, shadow_sampler);
            bound_units |= 1u << channel;
        }
    }

    shader.uniform1i(LLShaderMgr::UNBUILT_SHADOW_MAPS, (S32)unbuilt);

    // Drop any unit the PREVIOUS program had a shadow map on that this one does not reuse.
    // Those units are about to be handed to a program that maps them to ordinary material
    // samplers, and a depth texture left under the compare sampler there is undefined
    // behaviour. Only fires when the layout actually changes, so the common case of
    // rebinding the same channels costs nothing.
    //
    // The published mask is also what LLGLSLShader::bind() consults to release these units
    // before a program that declares no shadow samplers runs -- this relayout only handles
    // declaring programs, whose bind() deliberately leaves the units to us.
    U32 stale = LLGLSLShader::sCompareSamplerUnits & ~bound_units;
    for (S32 unit = 0; stale != 0; ++unit, stale >>= 1)
    {
        if (stale & 1u)
        {
            gGL.getTextureSlot(unit)->unbind();
            // And the compare sampler with it. unbind() leaves the sampler alone by design --
            // the next bind names its own, so a leftover is normally unreachable. A COMPARE
            // sampler is the exception: unbind puts the white placeholder on the unit, which
            // IS sampleable, and reading it through depth comparison is undefined. So the
            // release has to be explicit here, where the compare sampler was selected.
            gGL.getTextureSlot(unit)->bindSampler(0);
        }
    }

    LLGLSLShader::sCompareSamplerUnits = bound_units;
    ALTextureSlot::sSamplerBindsShadowCycle += ALTextureSlot::sSamplerBinds - sampler_binds_before;
}

void LLPipeline::unbindShadowMaps()
{
    // See bindShadowMaps: the compare sampler goes with the texture, or it survives onto
    // the white placeholder unbind() leaves behind and the next program to map the unit to
    // an ordinary sampler2D reads it through depth comparison.
    LLGLSLShader::releaseCompareSamplerUnits();
}

void LLPipeline::packDeferredUBO()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    static_assert(sizeof(DeferredUBOData) == 512, "DeferredUBOData must match std140 (512 bytes)");

    DeferredUBOData& d = mDeferredUBOData;

    // The rows as they lie are the columns std140 reads by default, so the shader computes
    // shadow_matrix[k] * spos exactly as the loose uniform did.
    for (U32 i = 0; i < 6; ++i)
    {
        memcpy(d.shadow_matrix[i], mSunShadowMatrix[i].getF32ptr(), sizeof(d.shadow_matrix[i]));
    }

    // ssao_effect_mat scales the projection of colour onto <1,1,1>/sqrt(3) by the value factor
    // and the remainder by the saturation factor. Expanded to std140's vec4-strided mat3 (the
    // 4th float of each column is padding); the matrix is symmetric, so orientation is moot.
    const LLVector3 ssao_effect = RenderSSAOEffect;
    const F32 matrix_diag    = (ssao_effect[0] + 2.0f * ssao_effect[1]) / 3.0f;
    const F32 matrix_nondiag = (ssao_effect[0] - ssao_effect[1]) / 3.0f;
    memset(d.ssao_effect_mat, 0, sizeof(d.ssao_effect_mat));
    d.ssao_effect_mat[0] = matrix_diag;    d.ssao_effect_mat[1] = matrix_nondiag; d.ssao_effect_mat[2]  = matrix_nondiag;
    d.ssao_effect_mat[4] = matrix_nondiag; d.ssao_effect_mat[5] = matrix_diag;    d.ssao_effect_mat[6]  = matrix_nondiag;
    d.ssao_effect_mat[8] = matrix_nondiag; d.ssao_effect_mat[9] = matrix_nondiag; d.ssao_effect_mat[10] = matrix_diag;

    memcpy(d.shadow_clip, mSunClipPlanes.mV, sizeof(d.shadow_clip));

    // A pack's cascades share one size, and the spot pair another. Each is read from a map that was built: one that was
    // not has no size, and the filters over the maps that were divide by it.
    const U32 unbuilt = unbuiltShadowMaps();
    const LLRenderTarget* sun_map  = &mRT->shadow[0];
    const LLRenderTarget* spot_map = &mSpotShadow[(unbuilt & (1u << 4)) ? 1 : 0];
    for (U32 i = 0; i < 4; ++i)
    {
        if (!(unbuilt & (1u << i)))
        {
            sun_map = &mRT->shadow[i];
            break;
        }
    }
    d.shadow_res[0]      = (F32)sun_map->getWidth();
    d.shadow_res[1]      = (F32)sun_map->getHeight();
    d.proj_shadow_res[0] = (F32)spot_map->getWidth();
    d.proj_shadow_res[1] = (F32)spot_map->getHeight();

    // Reverse-Z shadow maps store reversed depth and sample with GEQUAL, so the receiver
    // reference is pushed the other way: negate the bias (incl. the camera-error term) at
    // this single assembly site. shadowUtil.glsl stays byte-identical. Offsets keep sign.
    const F32 shadow_bias_error = RenderShadowBiasError * fabsf(LLViewerCamera::getInstance()->getOrigin().mV[2]) / 3000.f;
    const F32 bias_sign         = LLRender::sReverseZ ? -1.f : 1.f;
    d.shadow_bias        = bias_sign * (RenderShadowBias + shadow_bias_error);
    d.shadow_offset      = RenderShadowOffset;
    d.spot_shadow_bias   = bias_sign * RenderSpotShadowBias;
    d.spot_shadow_offset = RenderSpotShadowOffset;

    const F32 ssao_factor = RenderSSAOFactor;
    d.ssao_radius        = RenderSSAOScale;
    d.ssao_max_radius    = (F32)RenderSSAOMaxScale;
    d.ssao_factor        = ssao_factor;
    d.ssao_factor_inv    = 1.0f / ssao_factor;

    // Read by the programs as uniforms rather than compiled in, so that changing either reloads nothing.
    static LLCachedControl<F32> pcss_scale(gSavedSettings, "AlchemyRenderShadowPCSSScale", 200.f);
    static LLCachedControl<F32> specular_exponent(gSavedSettings, "RenderSpecularExponent", 128.f);
    d.shadow_pcss_scale = pcss_scale;
    d.specular_exponent = specular_exponent;

    mDeferredUBODirty = true;
}

void LLPipeline::bindDeferredUBO()
{
    // First use before any renderDeferredLighting this session: fill from current state so the
    // block is never uploaded zero-filled (e.g. the reflection-probe display debug path).
    if (!mDeferredUBO.allocated())
    {
        packDeferredUBO();
    }
    if (mDeferredUBODirty)
    {
        mDeferredUBO.update(&mDeferredUBOData, sizeof(DeferredUBOData));
        mDeferredUBODirty = false;
    }
    mDeferredUBO.bind(LLGLSLShader::UB_DEFERRED);
}

void LLPipeline::bindDeferredShaderFast(LLGLSLShader& shader)
{
    if (shader.mCanBindFast)
    { // was previously fully bound, use fast path
        shader.bind();
        // Not carried over from the full bind: a program fully bound outside a capture and fast
        // bound inside one would still read the main view's. shadowUtil.glsl selects its filter
        // tier and the cascades it reads on these, so a stale value is a pass rendering at the
        // wrong tier, or reading maps its pack never rendered, rather than a missing update.
        shader.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, gCubeSnapshot ? 1 : 0);
        shader.uniform1i(LLShaderMgr::SUN_SHADOW_CASCADES, (S32)sunCascades());
        bindBrdfLut(shader);
        bindShadowMaps(shader);
        bindReflectionProbes(shader);
        bindDeferredUBO();
    }
    else
    { //wasn't previously bound, use slow path
        bindDeferredShader(shader);
        shader.mCanBindFast = true;
    }
}

void LLPipeline::bindDeferredShader(LLGLSLShader& shader, LLRenderTarget* light_target, LLRenderTarget* depth_target)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LLRenderTarget* deferred_target       = &mRT->deferredScreen;

    shader.bind();

    // Bind the shared per-pass shadow/SSAO block (UB_DEFERRED); everything in it used to be
    // re-pushed as loose uniforms further down.
    bindDeferredUBO();

    S32 channel = 0;
    channel = shader.enableTexture(LLShaderMgr::DEFERRED_DIFFUSE);
    if (channel > -1)
    {
        // SRGBDecode: deferredScreen attachment 0 is GL_SRGB8_ALPHA8, and getGBuffer()
        // reads it straight into GBufferInfo::albedo with no conversion of its own -- the
        // hardware decode is what makes that value linear. Now that samplers skip the decode
        // by default, this pass has to ask for it. The alternative is srgb_to_linear() in
        // deferred/gbufferUtil.glsl, which would make the dependency shader-visible rather than
        // bind-visible; either is defensible, this one preserves the pixels exactly.
        deferred_target->bindTexture(0, channel, ALSamplers::PointClamp | ALSampler::SRGBDecode); // frag_data[0]
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_SPECULAR);
    if (channel > -1)
    {
        deferred_target->bindTexture(1, channel, ALSamplers::PointClamp); // frag_data[1]
    }

    channel = shader.enableTexture(LLShaderMgr::NORMAL_MAP);
    if (channel > -1)
    {
        deferred_target->bindTexture(2, channel, ALSamplers::PointClamp); // frag_data[2]
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_EMISSIVE);
    if (channel > -1)
    {
        deferred_target->bindTexture(3, channel, ALSamplers::PointClamp); // frag_data[3]
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_DEPTH);
    if (channel > -1)
    {
        if (depth_target)
        {
            gGL.getTextureSlot(channel)->bind(depth_target, true);
        }
        else
        {
            gGL.getTextureSlot(channel)->bind(deferred_target, true);
        }
        stop_glerror();
    }

    channel = shader.enableTexture(LLShaderMgr::EXPOSURE_MAP);
    if (channel > -1)
    {
        gGL.getTextureSlot(channel)->bind(&mExposureMap);
    }

    if (shader.hasUniform(LLShaderMgr::VIEWPORT))
    {
        shader.uniform4f(LLShaderMgr::VIEWPORT, (F32) gGLViewport[0],
                                    (F32) gGLViewport[1],
                                    (F32) gGLViewport[2],
                                    (F32) gGLViewport[3]);
    }

    channel = shader.enableTexture(LLShaderMgr::DEFERRED_NOISE);
    if (channel > -1)
    {
        // The noise map is tiled across the screen, so it keeps GL's GL_REPEAT default.
        gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, mNoiseMap,
                                            gGL.getSampler(ALSamplers::PointWrap));
    }

    bindBrdfLut(shader);

    stop_glerror();

    light_target = light_target ? light_target : mLightMap;
    channel = shader.enableTexture(LLShaderMgr::DEFERRED_LIGHT);
    if (channel > -1)
    {
        if (light_target && light_target->isComplete())
        {
            light_target->bindTexture(0, channel, ALSamplers::PointMirror);
        }
        else
        {
            gGL.getTextureSlot(channel)->bindFast(LLViewerFetchedTexture::sWhiteImagep, ALSamplers::AnisoWrap);
        }
    }

    stop_glerror();

    bindShadowMaps(shader);

    stop_glerror();

    if (!LLPipeline::sReflectionProbesEnabled)
    {
        channel = shader.enableTexture(LLShaderMgr::ENVIRONMENT_MAP);
        if (channel > -1)
        {
            LLCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : NULL;
            if (cube_map)
            {
                cube_map->enable(channel);
                cube_map->bind();
            }

            const F32* m = LLViewerCamera::getCurrent().getModelview().getF32ptr();

            F32 mat[] = { m[0], m[1], m[2],
                          m[4], m[5], m[6],
                          m[8], m[9], m[10] };

            shader.uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, true, mat);
        }
    }

    bindReflectionProbes(shader);

    // shadow_matrix, shadow_clip, shadow_res, proj_shadow_res, shadow_bias/offset,
    // spot_shadow_bias/offset, ssao_radius/max_radius/factor/factor_inv and ssao_effect_mat now
    // live in the shared UB_DEFERRED block (packed once per pass in packDeferredUBO, bound
    // above), so their per-bind loose setters -- and the per-bind rebuild of the 96-float
    // shadow-matrix array, the ssao_effect_mat derivation and the camera-relative shadow bias
    // -- are gone. Only uniforms that are NOT in the block are still pushed here.

    shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (GLfloat)deferred_target->getWidth(), (GLfloat)deferred_target->getHeight());
    shader.uniform1f(LLShaderMgr::DEFERRED_NEAR_CLIP, LLViewerCamera::getInstance()->getNear()*2.f);

    // sun_dir/moon_dir stay loose (entangled with atmospherics; not in UB_DEFERRED).
    shader.uniform3fv(LLShaderMgr::DEFERRED_SUN_DIR, 1, mTransformedSunDir.mV);
    shader.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, mTransformedMoonDir.mV);

    shader.uniformMatrix4fv(LLShaderMgr::MODELVIEW_DELTA_MATRIX, LLViewerCamera::getInstance()->getDeltaModelview());
    shader.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_DELTA_MATRIX, LLViewerCamera::getInstance()->getInverseDeltaModelview());

    shader.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, gCubeSnapshot ? 1 : 0);
    shader.uniform1i(LLShaderMgr::SUN_SHADOW_CASCADES, (S32)sunCascades());

    if (shader.hasUniform(LLShaderMgr::DEFERRED_NORM_MATRIX))
    {
        LLMatrix4a norm_mat;
        norm_mat.setInverse(LLViewerCamera::getCurrent().getModelview());
        norm_mat.transpose();
        shader.uniformMatrix4fv(LLShaderMgr::DEFERRED_NORM_MATRIX, norm_mat);
    }

    // The sun's, moon's and ambient light the atmospherics read are the Environment block's
    // (LLSettingsVOSky::applySpecial), the one place they are set for every program.

    shader.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mReflectionMapManager.mMaxProbeLOD);
}


LLColor3 pow3f(LLColor3 v, F32 f)
{
    v.mV[0] = powf(v.mV[0], f);
    v.mV[1] = powf(v.mV[1], f);
    v.mV[2] = powf(v.mV[2], f);
    return v;
}

LLVector4 pow4fsrgb(LLVector4 v, F32 f)
{
    v.mV[0] = powf(v.mV[0], f);
    v.mV[1] = powf(v.mV[1], f);
    v.mV[2] = powf(v.mV[2], f);
    return v;
}

void LLPipeline::renderDeferredLighting()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("renderDeferredLighting");

    // The main view's sky behind is this frame's: given back once the haze that reads it has run, however the lighting
    // ends.
    struct SkyBehindRelease
    {
        LLPipeline& mPipeline;
        bool        mMainView;
        ~SkyBehindRelease()
        {
            if (mMainView)
            {
                mPipeline.releaseSkyBehind();
            }
        }
    } sky_behind_release{ *this, !gCubeSnapshot && !sImpostorRender && mRT == &mMainRT };

    if (!sCull)
    {
        return;
    }

    llassert(!sRenderingHUDs);

    // Read here, ahead of every pass that holds to it: the lighting and, through renderGeomPostDeferred, the haze.
    static LLCachedControl<bool> depth_bounds(gSavedSettings, "AlchemyRenderDepthBoundsTest", true);
    ALDepthBounds::sEnabled = depth_bounds;

    F32 light_scale = 1.f;

    if (gCubeSnapshot)
    { //darken local lights when probe ambiance is above 1
        light_scale = mReflectionMapManager.mLightScale;
    }
    else
    {
        static LLCachedControl<F32> alchemy_light_scale(gSavedSettings, "AlchemyGlobalLightScale", 1.f);
        light_scale = alchemy_light_scale;
    }

    LLRenderTarget *screen_target         = &mRT->screen;

    // The light map, the pack's where it has one, for the lighting alone: nothing after the local lights reads it, and
    // where a later pass's program has the sampler, bindDeferredShader gives it white.
    ALTransientTargets lighting(mTransientTargets);
    LLRenderTarget* deferred_light_target =
        acquireResource(lighting, ALRenderResourcePlan::inPack(renderPack(), ALRenderResourcePlan::Slot::Light));
    LLRenderTarget* const outer_light_map = mLightMap;
    mLightMap = deferred_light_target;

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("deferred");
        LLViewerCamera *camera = LLViewerCamera::getInstance();

        if (gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_HUD))
        {
            gPipeline.toggleRenderType(LLPipeline::RENDER_TYPE_HUD);
        }

        gGL.setColorMask(true, true);

        // draw a cube around every light
        LLVertexBuffer::unbind();

        LLGLEnable cull(GL_CULL_FACE);
        LLGLEnable blend(GL_BLEND);

        const LLMatrix4a& mat = LLViewerCamera::getCurrent().getModelview();

        setupHWLights();  // to set mSun/MoonDir;

        LLVector4a dir, tc;
        dir.loadua(mSunDir.mV);
        mat.transform4(dir, tc);
        mTransformedSunDir.set(tc.getF32ptr());

        dir.loadua(mMoonDir.mV);
        mat.transform4(dir, tc);
        mTransformedMoonDir.set(tc.getF32ptr());

        // Repack the shared shadow/SSAO block once for this deferred pass (per pass, so the
        // cube-snapshot re-entry gets its own RT sizes and matrices). bindDeferredShader()
        // uploads it on the first bind, and every deferred-lighting program then reads these
        // ~13 constants from UB_DEFERRED instead of having them re-pushed on every bind.
        packDeferredUBO();

        // The light map's sun pass runs wherever the pack has a light map: for shadows, and in the main view for SSAO.
        // It clears the map, so whatever the pool's target held before is gone.
        if (deferred_light_target)
        {
            LL_PROFILE_GPU_ZONE("sun program");
            deferred_light_target->bindTarget();
            {  // paint shadow/SSAO light map (direct lighting lightmap)
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("renderDeferredLighting - sun shadow");

                LLGLSLShader& sun_shader = gCubeSnapshot ? gDeferredSunProbeProgram : gDeferredSunProgram;
                bindDeferredShader(sun_shader, deferred_light_target);
                mScreenTriangleVB->setBuffer();
                gGL.setClearColor(LLColor4::white);
                deferred_light_target->clear(GL_COLOR_BUFFER_BIT);
                gGL.setClearColor(LLColor4::transparent);

                sun_shader.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                                              (GLfloat)deferred_light_target->getWidth(),
                                              (GLfloat)deferred_light_target->getHeight());

                // A spot shadow channel only a shadowing spot light reads (setupSpotLight's
                // proj_shadow_idx): the program leaves the other's filter out.
                sun_shader.uniform2f(LLShaderMgr::DEFERRED_SPOT_SHADOW_LIVE,
                                     mShadowSpotLight[0].notNull() ? 1.f : 0.f,
                                     mShadowSpotLight[1].notNull() ? 1.f : 0.f);

                {
                    // The triangle lies on the far plane (sunLightV.glsl) and the target shares the scene's
                    // depth: only geometry is shadowed and occluded, and the sky keeps the clear's white, which
                    // nothing reads there (the soften pass's sky program lights nothing).
                    LLGLDisable   blend(GL_BLEND);
                    ALDrawnOnly   drawn;
                    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                }

                unbindDeferredShader(sun_shader);
            }
            deferred_light_target->flush();
        }

        // SSAO's blur of the light map's occlusion, in the main view (ALLightMapBlur): where the session computes, one
        // dispatch into the blurred light map, which the soften pass reads in place of the light map; otherwise across
        // into a scratch of the light map's description, cleared as the sun pass clears the map, and back down into the
        // map.
        LLRenderTarget* light_blurred = nullptr;
        if (mResourceInputs.mSSAO && !gCubeSnapshot && deferred_light_target)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("renderDeferredLighting - soften shadow");
            LL_PROFILE_GPU_ZONE("soften shadow");
            const ALLightMapBlur::Kernel kernel =
                ALLightMapBlur::kernel(RenderShadowGaussian, RenderShadowBlurSize, RenderShadowBlurDistFactor);

            if (LLRender::sCompute && gDeferredBlurLightComputeProgram.isComplete() && ALLightMapBlur::apron(kernel) >= 0)
            {
                light_blurred = acquireResource(lighting, ALRenderResourcePlan::Id::MainLightBlurred);
                if (light_blurred)
                {
                    bindDeferredShader(gDeferredBlurLightComputeProgram, deferred_light_target);
                    ALLightMapBlur::setKernel(gDeferredBlurLightComputeProgram, kernel);
                    if (!ALLightMapBlur::dispatch(gDeferredBlurLightComputeProgram, kernel, *light_blurred))
                    {
                        lighting.release(light_blurred);
                        light_blurred = nullptr;
                    }
                    unbindDeferredShader(gDeferredBlurLightComputeProgram);
                }
            }

            LLRenderTarget* blur_scratch = light_blurred
                ? nullptr
                : acquireResource(lighting, ALRenderResourcePlan::Id::MainLight, "main.light.scratch");
            if (blur_scratch)
            {
                blur_scratch->bindTarget();
                gGL.setClearColor(LLColor4::white);
                blur_scratch->clear(GL_COLOR_BUFFER_BIT);
                gGL.setClearColor(LLColor4::transparent);

                bindDeferredShader(gDeferredBlurLightProgram);
                ALLightMapBlur::setKernel(gDeferredBlurLightProgram, kernel);
                gDeferredBlurLightProgram.uniform2f(LLShaderMgr::DELTA, 1.f, 0.f);

                // Both blurs on the far plane (blurLightV.glsl), over geometry alone: the sky stays the clear's white
                // in either target.
                {
                    LLGLDisable   blend(GL_BLEND);
                    ALDrawnOnly   drawn;
                    mScreenTriangleVB->setBuffer();
                    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                }

                blur_scratch->flush();
                unbindDeferredShader(gDeferredBlurLightProgram);

                bindDeferredShader(gDeferredBlurLightProgram, blur_scratch);

                deferred_light_target->bindTarget();

                gDeferredBlurLightProgram.uniform2f(LLShaderMgr::DELTA, 0.f, 1.f);

                {
                    LLGLDisable   blend(GL_BLEND);
                    ALDrawnOnly   drawn;
                    mScreenTriangleVB->setBuffer();
                    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                }
                deferred_light_target->flush();
                unbindDeferredShader(gDeferredBlurLightProgram);
                lighting.release(blur_scratch);
            }
        }

        screen_target->bindTarget();
        // clear color buffer here - zeroing alpha (glow) is important or it will accumulate against sky
        gGL.setClearColor(LLColor4::transparent);
        screen_target->clear(GL_COLOR_BUFFER_BIT);

        if (RenderDeferredAtmospheric)
        {  // apply sunlight contribution
            LLGLSLShader &soften_shader = *gDeferredSoftenProgram.selectVariant();

            LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("renderDeferredLighting - atmospherics");
            LL_PROFILE_GPU_ZONE("atmospherics");
            // The sun's shadow and the occlusion, blurred where the dispatch blurred them.
            bindDeferredShader(soften_shader, light_blurred);

            static LLCachedControl<F32> ssao_scale(gSavedSettings, "RenderSSAOIrradianceScale", 0.5f);
            static LLCachedControl<F32> ssao_max(gSavedSettings, "RenderSSAOIrradianceMax", 0.25f);
            soften_shader.uniform1f(LLShaderMgr::DEFERRED_SSAO_IRRADIANCE_SCALE, ssao_scale);
            soften_shader.uniform1f(LLShaderMgr::DEFERRED_SSAO_IRRADIANCE_MAX, ssao_max);

            LLEnvironment &environment = LLEnvironment::instance();

            soften_shader.uniform1i(LLShaderMgr::SUN_UP_FACTOR, environment.getIsSunUp() ? 1 : 0);
            soften_shader.uniform3fv(LLShaderMgr::LIGHTNORM, 1, environment.getClampedLightNorm().mV);

            soften_shader.uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, LLDrawPoolAlpha::sWaterPlane.mV);

            // The triangle lies on the far plane (softenLightV.glsl), level with the depth the sky leaves cleared:
            // this draw passes only where the world drew something, and the sky's draw below only where it did
            // not, so no sky pixel pays for the G-buffer decode and atmospherics it would throw away.
            LLGLDisable blend(GL_BLEND);
            {
                ALDrawnOnly drawn;

                mScreenTriangleVB->setBuffer();
                mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            }

            unbindDeferredShader(soften_shader);

            LLGLSLShader& sky_shader = *gDeferredSoftenSkyProgram.selectVariant();
            bindDeferredShader(sky_shader);
            {
                LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_EQUAL);

                mScreenTriangleVB->setBuffer();
                mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            unbindDeferredShader(sky_shader);
        }

        static LLCachedControl<S32> local_light_count(gSavedSettings, "RenderLocalLightCount", 256);
        if (local_light_count > 0 && (!gCubeSnapshot || shaderInputs().mGlobal.mProbeLevel > 0))
        {
            // A projector's colour is worked out with its fade where it is sorted, and carried
            // to the pass that draws it.
            struct SpotLight
            {
                LLPointer<LLDrawable> drawable;
                LLColor3              col;
            };

            static std::vector<LLVector4>        fullscreen_lights;
            static std::vector<SpotLight>        spot_lights;
            static std::vector<SpotLight>        fullscreen_spot_lights;
            static std::vector<LLVector4>        light_colors;

            gGL.setSceneBlendType(LLRender::BT_ADD);
            LLSettingsSky::ptr_t        psky        = LLEnvironment::instance().getCurrentSky();

            if (!gCubeSnapshot)
            {
                for (U32 i = 0; i < 2; i++)
                {
                    mTargetShadowSpotLight[i] = NULL;
                }
            }

            LLVertexBuffer::unbind();

            // Each pass binds a deferred program, the G-buffer and the probes: none where it has no light to draw.
            if (!mNearbyLights.empty())
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("renderDeferredLighting - local lights");
                LL_PROFILE_GPU_ZONE("local lights");
                LLGLSLShader& light_shader = *gDeferredLightProgram.selectVariant();
                bindDeferredShader(light_shader);

                if (mCubeVB.isNull())
                {
                    mCubeVB = ll_create_cube_vb(LLVertexBuffer::MAP_VERTEX);
                }

                mCubeVB->setBuffer();

                LLGLDepthTest depth(GL_TRUE, GL_FALSE);
                // Each volume is held to the depths its light reaches as well: its depth test passes everything
                // behind it, which the light does not.
                const LLMatrix4a& proj = LLViewerCamera::getCurrent().getProjection();
                std::optional<ALDepthBounds> bounds;
                if (ALDepthBounds::available())
                {
                    F64 zmin, zmax;
                    ALDepthBounds::drawn(zmin, zmax);
                    bounds.emplace(zmin, zmax);
                }
                // mNearbyLights already includes distance calculation and excludes muted avatars.
                // It is calculated from mLights
                // mNearbyLights also provides fade value to gracefully fade-out out of range lights
                S32 count = 0;
                for (light_set_t::iterator iter = mNearbyLights.begin(); iter != mNearbyLights.end(); ++iter)
                {
                    count++;
                    if (count > local_light_count)
                    { //stop collecting lights once we hit the limit
                        break;
                    }

                    LLDrawable * drawablep = iter->drawable;
                    LLVOVolume * volume = drawablep->getVOVolume();
                    if (!volume)
                    {
                        continue;
                    }

                    if (volume->isAttachment())
                    {
                        if (!sRenderAttachedLights)
                        {
                            continue;
                        }
                    }

                    LLVector4a center;
                    center.load3(drawablep->getPositionAgent().mV);
                    const F32 *c = center.getF32ptr();
                    F32        s = volume->getLightRadius() * 1.5f;

                    // send light color to shader in linear space, faded in or out with the
                    // light's fade clock -- a light fading out past the draw distance dims to
                    // nothing before calcNearbyLights erases it, and a new one rises from nothing
                    LLColor3 col = volume->getLightLinearColor() * light_scale;
                    col *= light_fade_brightness(iter->fade);

                    if (col.magVecSquared() < 0.001f)
                    {
                        continue;
                    }

                    if (s <= 0.001f)
                    {
                        continue;
                    }

                    LLVector4a sa;
                    sa.splat(s);
                    if (camera->AABBInFrustumNoFarClip(center, sa) == 0)
                    {
                        continue;
                    }

                    sVisibleLightCount++;

                    if (camera->getOrigin().mV[0] > c[0] + s + 0.2f || camera->getOrigin().mV[0] < c[0] - s - 0.2f ||
                        camera->getOrigin().mV[1] > c[1] + s + 0.2f || camera->getOrigin().mV[1] < c[1] - s - 0.2f ||
                        camera->getOrigin().mV[2] > c[2] + s + 0.2f || camera->getOrigin().mV[2] < c[2] - s - 0.2f)
                    {  // draw box if camera is outside box
                        if (volume->isLightSpotlight())
                        {
                            drawablep->getVOVolume()->updateSpotLightPriority();
                            spot_lights.push_back({ drawablep, col });
                            continue;
                        }

                        light_shader.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, c);
                        light_shader.uniform1f(LLShaderMgr::LIGHT_SIZE, s);
                        light_shader.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, col.mV);
                        light_shader.uniform1f(LLShaderMgr::LIGHT_FALLOFF, volume->getLightFalloff(DEFERRED_LIGHT_FALLOFF));

                        if (bounds)
                        {
                            LLVector4a view;
                            mat.affineTransform(center, view);
                            bound_to_light(*bounds, proj, view[2], s);
                        }

                        gGL.syncMatrices();

                        mCubeVB->drawRange(LLRender::TRIANGLE_FAN, 0, 7, 8, get_box_fan_indices(camera, center));
                    }
                    else
                    {
                        if (volume->isLightSpotlight())
                        {
                            drawablep->getVOVolume()->updateSpotLightPriority();
                            fullscreen_spot_lights.push_back({ drawablep, col });
                            continue;
                        }

                        LLVector4a tc;
                        mat.affineTransform(center, tc);

                        fullscreen_lights.push_back(LLVector4(tc[0], tc[1], tc[2], s));
                        light_colors.push_back(LLVector4(col.mV[0], col.mV[1], col.mV[2], volume->getLightFalloff(DEFERRED_LIGHT_FALLOFF)));
                    }
                }

                // Bookmark comment to allow searching for mSpecialRenderMode == 3 (avatar edit mode),
                // prev site of appended deferred character light, removed by SL-13522 09/20

                unbindDeferredShader(light_shader);
            }

            if (!spot_lights.empty())
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("renderDeferredLighting - projectors");
                LL_PROFILE_GPU_ZONE("projectors");
                LLGLDepthTest depth(GL_TRUE, GL_FALSE);
                // Held to the depths each projector reaches, as the point lights are.
                const LLMatrix4a& proj = LLViewerCamera::getCurrent().getProjection();
                std::optional<ALDepthBounds> bounds;
                if (ALDepthBounds::available())
                {
                    F64 zmin, zmax;
                    ALDepthBounds::drawn(zmin, zmax);
                    bounds.emplace(zmin, zmax);
                }
                LLGLSLShader& spot_shader = *gDeferredSpotLightProgram.selectVariant();
                bindDeferredShader(spot_shader);

                mCubeVB->setBuffer();

                spot_shader.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);

                for (const SpotLight& spot : spot_lights)
                {
                    LLDrawable* drawablep = spot.drawable;
                    LLVOVolume *volume = drawablep->getVOVolume();

                    LLVector4a center;
                    center.load3(drawablep->getPositionAgent().mV);
                    const F32* c = center.getF32ptr();
                    F32        s = volume->getLightRadius() * 1.5f;

                    sVisibleLightCount++;

                    setupSpotLight(spot_shader, drawablep);

                    spot_shader.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, c);
                    spot_shader.uniform1f(LLShaderMgr::LIGHT_SIZE, s);
                    spot_shader.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, spot.col.mV);
                    spot_shader.uniform1f(LLShaderMgr::LIGHT_FALLOFF, volume->getLightFalloff(DEFERRED_LIGHT_FALLOFF));

                    if (bounds)
                    {
                        LLVector4a view;
                        mat.affineTransform(center, view);
                        bound_to_light(*bounds, proj, view[2], s);
                    }

                    gGL.syncMatrices();

                    mCubeVB->drawRange(LLRender::TRIANGLE_FAN, 0, 7, 8, get_box_fan_indices(camera, center));
                }
                spot_shader.disableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                unbindDeferredShader(spot_shader);
            }

            if (!fullscreen_lights.empty() || !fullscreen_spot_lights.empty())
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("renderDeferredLighting - fullscreen lights");
                // On the far plane (multiPointLightV.glsl): only where the world drew something, so no light is
                // shaded over the sky. Under the depth bounds test each draw is held as well to the depths its lights
                // reach, from the eye, which they surround, out to the farthest of them.
                ALDrawnOnly drawn;
                ALDepthBounds* bounds = drawn.bounds();
                const LLMatrix4a& proj = LLViewerCamera::getCurrent().getProjection();
                LL_PROFILE_GPU_ZONE("fullscreen lights");

                U32 count = 0;
                U32 total_count = 0;

                const U32 max_count = LL_DEFERRED_MULTI_LIGHT_COUNT;
                LLVector4 light[max_count];
                LLVector4 col[max_count];

                F32 far_z = 0.f;

                for (size_t i = 0, num_fullscreen_lights = fullscreen_lights.size(); i < num_fullscreen_lights; ++i)
                {
                    light[count] = fullscreen_lights[i];
                    col[count] = light_colors[i];

                    far_z = llmin(light[count].mV[2] - light[count].mV[3], far_z);
                    count++;
                    total_count++;
                    if (count == max_count || total_count == num_fullscreen_lights)
                    {
                        U32 idx = count - 1;
                        // The program is chosen by batch size, so its LIGHT_COUNT already is the
                        // count and both of its loops run to that; there is no light_count
                        // uniform to feed.
                        LLGLSLShader& multi_light_shader = *gDeferredMultiLightProgram[idx].selectVariant();
                        bindDeferredShader(multi_light_shader);
                        multi_light_shader.uniform4fv(LLShaderMgr::MULTI_LIGHT, count, light[0].mV);
                        multi_light_shader.uniform4fv(LLShaderMgr::MULTI_LIGHT_COL, count, col[0].mV);
                        multi_light_shader.uniform1f(LLShaderMgr::MULTI_LIGHT_FAR_Z, far_z);
                        if (bounds)
                        {   // far_z is the farthest of the lights' spheres, as the shader's cut is
                            F64 zmin, zmax;
                            if (ALDepthBounds::viewSpan(proj, 0.f, far_z * 1.01f - 0.01f, zmin, zmax))
                            {
                                widen_for_storage(zmin, zmax);
                            }
                            else
                            {
                                ALDepthBounds::drawn(zmin, zmax);
                            }
                            bounds->set(zmin, zmax);
                        }
                        far_z = 0.f;
                        count = 0;
                        mScreenTriangleVB->setBuffer();
                        mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                        unbindDeferredShader(multi_light_shader);
                    }
                }

                LLGLSLShader& multi_spot_shader = *gDeferredMultiSpotLightProgram.selectVariant();
                if (!fullscreen_spot_lights.empty())
                {
                    bindDeferredShader(multi_spot_shader);
                    multi_spot_shader.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                    mScreenTriangleVB->setBuffer();
                }

                for (const SpotLight& spot : fullscreen_spot_lights)
                {
                    LLDrawable* drawablep = spot.drawable;
                    LLVOVolume* volume = drawablep->getVOVolume();
                    LLVector3   center = drawablep->getPositionAgent();
                    F32         light_size_final = volume->getLightRadius() * 1.5f;
                    F32         light_falloff_final = volume->getLightFalloff(DEFERRED_LIGHT_FALLOFF);

                    sVisibleLightCount++;

                    LLVector4a tc;
                    mat.affineTransform(LLVector4a(center.mV[0], center.mV[1], center.mV[2], 1.f), tc);

                    if (bounds)
                    {
                        bound_to_light(*bounds, proj, tc[2], light_size_final);
                    }

                    setupSpotLight(multi_spot_shader, drawablep);

                    multi_spot_shader.uniform3fv(LLShaderMgr::LIGHT_CENTER, 1, tc.getF32ptr());
                    multi_spot_shader.uniform1f(LLShaderMgr::LIGHT_SIZE, light_size_final);
                    multi_spot_shader.uniform3fv(LLShaderMgr::DIFFUSE_COLOR, 1, spot.col.mV);
                    multi_spot_shader.uniform1f(LLShaderMgr::LIGHT_FALLOFF, light_falloff_final);

                    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
                }

                if (!fullscreen_spot_lights.empty())
                {
                    multi_spot_shader.disableTexture(LLShaderMgr::DEFERRED_PROJECTION);
                    unbindDeferredShader(multi_spot_shader);
                }
            }

            // Clear does not free internal vector storage, so this is more efficient than creating new vectors each frame
            fullscreen_lights.clear();
            spot_lights.clear();
            fullscreen_spot_lights.clear();
            light_colors.clear();
        }

        gGL.setColorMask(true, true);
    }

    // Back to the pool: GL draws the lighting before whatever acquires the map next.
    mLightMap = outer_light_map;
    lighting.releaseAll();

    {  // render non-deferred geometry (alpha, fullbright, glow)
        LLGLDisable blend(GL_BLEND);

        pushRenderTypeMask();
        andRenderTypeMask(LLPipeline::RENDER_TYPE_ALPHA,
                          LLPipeline::RENDER_TYPE_ALPHA_PRE_WATER,
                          LLPipeline::RENDER_TYPE_ALPHA_POST_WATER,
                          LLPipeline::RENDER_TYPE_FULLBRIGHT,
                          LLPipeline::RENDER_TYPE_VOLUME,
                          LLPipeline::RENDER_TYPE_GLOW,
                          LLPipeline::RENDER_TYPE_BUMP,
                          LLPipeline::RENDER_TYPE_GLTF_PBR,
                          LLPipeline::RENDER_TYPE_PASS_SIMPLE,
                          LLPipeline::RENDER_TYPE_PASS_ALPHA,
                          LLPipeline::RENDER_TYPE_PASS_ALPHA_MASK,
                          LLPipeline::RENDER_TYPE_PASS_BUMP,
                          LLPipeline::RENDER_TYPE_PASS_POST_BUMP,
                          LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT,
                          LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK,
                          LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_SHINY,
                          LLPipeline::RENDER_TYPE_PASS_GLOW,
                          LLPipeline::RENDER_TYPE_PASS_GLTF_GLOW,
                          LLPipeline::RENDER_TYPE_PASS_GRASS,
                          LLPipeline::RENDER_TYPE_PASS_SHINY,
                          LLPipeline::RENDER_TYPE_PASS_INVISIBLE,
                          LLPipeline::RENDER_TYPE_PASS_INVISI_SHINY,
                          LLPipeline::RENDER_TYPE_AVATAR,
                          LLPipeline::RENDER_TYPE_CONTROL_AV,
                          LLPipeline::RENDER_TYPE_ALPHA_MASK,
                          LLPipeline::RENDER_TYPE_FULLBRIGHT_ALPHA_MASK,
                          LLPipeline::RENDER_TYPE_TERRAIN,
                          LLPipeline::RENDER_TYPE_WATER,
                          LLPipeline::RENDER_TYPE_WATEREXCLUSION,
                          END_RENDER_TYPES);

        renderGeomPostDeferred(*LLViewerCamera::getInstance());
        popRenderTypeMask();
    }

    screen_target->flush();

    if (!gCubeSnapshot)
    {
        // this is the end of the 3D scene render, keep the modelview for the
        // off-by-one-frame effects in the next frame
        LLViewerCamera::getInstance()->rememberModelview();
    }
    gGL.setColorMask(true, true);
}

LLRenderTarget* LLPipeline::acquireSkyBehind()
{
    // What a frame whose lighting never ran left held goes back first.
    releaseSkyBehind();
    mSkyBehind = acquireResource(mSkyBehindTargets, ALRenderResourcePlan::Id::SkyBehind);
    return mSkyBehind;
}

void LLPipeline::releaseSkyBehind()
{
    mSkyBehind = nullptr;
    mSkyBehindTargets.releaseAll();
}

bool LLPipeline::skyBehindWanted() const
{
    // The haze pass converges only the water surface seen from above it (hazeF.glsl).
    if (gCubeSnapshot || sImpostorRender || mRT != &mMainRT || !RenderDeferredAtmospheric || sUnderWaterRender ||
        !mResourcePlan.wants(ALRenderResourcePlan::Id::SkyBehind))
    {
        return false;
    }
    // The water's slab, rim to rim, against the frustum, from where the water is drawn. What the frustum holds but the
    // world hides, as water under the land, the sky pool's query finds (LLDrawPoolWLSky::renderSkyHazeDeferred).
    return mWaterPool && hasRenderType(LLPipeline::RENDER_TYPE_WATER) && mWaterSurface.drawnFrom(*LLViewerCamera::getInstance());
}

void LLPipeline::doAtmospherics()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    if (sImpostorRender)
    { // do not attempt atmospherics on impostors
        return;
    }

    if (RenderDeferredAtmospheric)
    {
        LLGLEnable blend(GL_BLEND);
        // The haze dims the glow in the alpha as it dims what is lit. In a cutout frame the alpha is the subject's
        // coverage, seeded before this pass, and haze in front of the subject leaves it no less covered.
        gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_SOURCE_ALPHA, LLRender::BF_ZERO,
                      alphaWritesCoverage() ? LLRender::BF_ONE : LLRender::BF_SOURCE_ALPHA);
        // The caller's mask comes back for the pools drawn after the haze.
        LLGLSColorMask color_mask(true, true);

        LL_PROFILE_GPU_ZONE("haze");

        // The triangle lies on the far plane (hazeV.glsl), level with the depth the sky leaves cleared: it passes
        // only where the world drew something, so the sky is rejected before it is shaded. The screen target shares
        // the G-buffer's depth, which the shader reads where it is, as the lighting passes do: neither draw writes it.
        ALDrawnOnly drawn;

        const auto apply_haze = [this](LLGLSLShader& haze_shader, bool on_water)
        {
            bindDeferredShader(haze_shader);

            LLEnvironment& environment = LLEnvironment::instance();
            haze_shader.uniform1i(LLShaderMgr::SUN_UP_FACTOR, environment.getIsSunUp() ? 1 : 0);
            haze_shader.uniform3fv(LLShaderMgr::LIGHTNORM, 1, environment.getClampedLightNorm().mV);

            haze_shader.uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, LLDrawPoolAlpha::sWaterPlane.mV);

            if (on_water)
            {
                haze_shader.bindTexture(LLShaderMgr::SKY_BEHIND_MAP, mSkyBehind, ALSamplers::BilinearClamp);
                // From high above the water, the water fades into the sky behind it, wholly by the height it is no
                // longer drawn from.
                const F32 height = LLViewerCamera::getInstance()->getOrigin().mV[VZ] - environment.getWaterHeight();
                haze_shader.uniform1f(LLShaderMgr::WATER_GROUND_FADE, ALFarPlane::groundFade(height));
            }

            // full screen blit
            mScreenTriangleVB->setBuffer();
            mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

            if (on_water)
            {
                haze_shader.disableTexture(LLShaderMgr::SKY_BEHIND_MAP);
            }
            unbindDeferredShader(haze_shader);
        };

        // Far water converges on the sky drawn behind it when this view's is there: the main view's alone, once the
        // sky pool has drawn it this frame (HDRI skies and probe captures keep WindLight's own limit). The water
        // marked its own pixels in the stencil, so the two draws shade each pixel once. The water's draw runs only if
        // the sky pool's query saw water: its field covers every pixel the water marked, and inland none is.
        const bool sky_behind = !gCubeSnapshot && mRT == &mMainRT && mSkyBehind;
        if (sky_behind)
        {
            {
                ALWaterStencil::OffWater stencil;
                apply_haze(*gHazeProgram.selectVariant(), false);
            }
            {
                ALWaterStencil::OnWater stencil;
                std::optional<ALConditionalRender> if_water;
                if (mSkyBehindQueryFrame == gFrameCount)
                {
                    if_water.emplace(gGL.queries(), mSkyBehindQuery);
                }
                apply_haze(*gHazeOnWaterProgram.selectVariant(), true);
            }
        }
        else
        {
            apply_haze(*gHazeProgram.selectVariant(), false);
        }

        gGL.setSceneBlendType(LLRender::BT_ALPHA);
    }
}

void LLPipeline::doWaterHaze()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    if (sImpostorRender)
    { // do not attempt water haze on impostors
        return;
    }

    // From above, only where the water itself is drawn: from higher, the field would fog the sky under it as void water.
    if (!sUnderWaterRender && !mWaterSurface.drawnFrom(*LLViewerCamera::getInstance()))
    {
        return;
    }

    if (RenderDeferredAtmospheric)
    {
        LLGLEnable blend(GL_BLEND);
        gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_SOURCE_ALPHA, LLRender::BF_ZERO, LLRender::BF_SOURCE_ALPHA);

        // The caller's mask comes back for the pools drawn after the haze.
        LLGLSColorMask color_mask(true, true);

        // apply haze
        LLGLSLShader& haze_shader = gHazeWaterProgram;

        LL_PROFILE_GPU_ZONE("water haze");
        // The scene's depth where it is, the G-buffer's, which the screen shares: the haze tests against it and
        // writes none, from above or below.
        bindDeferredShader(haze_shader);

        haze_shader.uniform4fv(LLShaderMgr::WATER_WATERPLANE, 1, LLDrawPoolAlpha::sWaterPlane.mV);
        haze_shader.uniform1i(LLShaderMgr::WATER_ABOVE_WATER, sUnderWaterRender ? -1 : 1);

        if (LLPipeline::sUnderWaterRender)
        {
            LLGLDepthTest depth(GL_FALSE);

            // full screen blit, the triangle worked from the vertex id (waterHazeV.glsl)
            gGL.drawArraysInstanced(LLRender::TRIANGLES, 0, 3, 1);
        }
        else
        {
            // The field as LLDrawPoolWater draws it, flat.
            // Default LEQUAL auto-translates to GEQUAL under reverse-Z, mirroring the manual
            // above-water depth test in waterHazeF.glsl (which flips its compare direction).
            LLGLDepthTest depth(GL_TRUE, GL_FALSE);
            LLGLDisable   cull(GL_CULL_FACE);
            // Not where an exclusion prim removed the water.
            ALWaterStencil::NotExcluded stencil;

            gGLLastMatrix = NULL;
            gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());

            mWaterSurface.bind(haze_shader);
            mWaterSurface.draw(haze_shader, false);
        }

        unbindDeferredShader(haze_shader);


        gGL.setSceneBlendType(LLRender::BT_ALPHA);
    }
}

void LLPipeline::doWaterExclusion()
{
    if (sImpostorRender)
    {   // No water in an impostor bake -- generateImpostor clears every water render type --
        // so there is nothing to exclude. Rebuilding the mask here regenerated a full-screen
        // target from the impostor camera, once per post-deferred pass, and left the result
        // behind for the main render to find. doAtmospherics and doWaterHaze already bail for
        // the same reason; this one was simply missed.
        return;
    }

    LL_PROFILE_GPU_ZONE("water exclusion");
    // Made in init and kept to cleanup. Without it nothing marks the stencil, which excludes nothing.
    if (mWaterExclusionPool)
    {
        mWaterExclusionPool->render();
    }
}

// The map from clip space to a texture: x and y from [-1, 1] to [0, 1], z by
// the scale and bias given, so a forward projection's [-1, 1] depth lands in
// [0, 1] with 0.5 and 0.5 and a reversed one's [0, 1] passes through with 1
// and 0.
static LLMatrix4a clip_to_texture(F32 z_scale, F32 z_bias)
{
    LLMatrix4a m;
    m.mMatrix[0].set(0.5f, 0.f, 0.f, 0.f);
    m.mMatrix[1].set(0.f, 0.5f, 0.f, 0.f);
    m.mMatrix[2].set(0.f, 0.f, z_scale, 0.f);
    m.mMatrix[3].set(0.5f, 0.5f, z_bias, 1.f);
    return m;
}

void LLPipeline::setupSpotLight(LLGLSLShader& shader, LLDrawable* drawablep)
{
    //construct frustum
    LLVOVolume* volume = drawablep->getVOVolume();
    LLVector3 params = volume->getSpotLightParams();

    F32 fov = params.mV[0];
    F32 focus = params.mV[1];

    LLVector3 pos = drawablep->getPositionAgent();
    LLQuaternion quat = volume->getRenderRotation();
    LLVector3 scale = volume->getScale();

    //get near clip plane
    LLVector3 at_axis(0,0,-scale.mV[2]*0.5f);
    at_axis *= quat;

    LLVector3 np = pos+at_axis;
    at_axis.normVec();

    //get origin that has given fov for plane np, at_axis, and given scale
    F32 dist = (scale.mV[1]*0.5f)/tanf(fov*0.5f);

    LLVector3 origin = np - at_axis*dist;

    //matrix from volume space to agent space
    LLMatrix4 light_mat(quat, LLVector4(origin,1.f));

    LLMatrix4a light_to_screen;
    light_to_screen.setMul(LLMatrix4a(light_mat), LLViewerCamera::getCurrent().getModelview());

    LLMatrix4a screen_to_light;
    screen_to_light.setInverse(light_to_screen);

    F32 s = volume->getLightRadius()*1.5f;
    F32 near_clip = dist;
    F32 width = scale.mV[VX];
    F32 height = scale.mV[VY];
    F32 far_clip = s+dist-scale.mV[VZ];

    F32 fovy = fov; // radians
    F32 aspect = width/height;

    LLVector4a p1, p2, screen_origin;
    light_to_screen.affineTransform(LLVector4a(0.f, 0.f, -(near_clip+0.01f), 1.f), p1);
    light_to_screen.affineTransform(LLVector4a(0.f, 0.f, -(near_clip+1.f), 1.f), p2);
    light_to_screen.affineTransform(LLVector4a(0.f, 0.f, 0.f, 1.f), screen_origin);

    LLVector4a n;
    n.setSub(p2, p1);
    n.normalize3();

    F32 proj_range = far_clip - near_clip;
    // Deliberately the FORWARD perspective (not al_perspective), paired with the forward
    // [-1,1]->[0,1] clip_to_texture: this projector matrix produces cookie-texture UVs and a
    // self-consistent front/behind test (proj_tc.z > 0 in spotLightF/alphaF); it never
    // rasterizes into or samples the reverse-Z depth buffer. Spot SHADOWS use the separately
    // reversed shadow_matrix. Converting this to al_perspective would flip proj_tc.z against
    // the forward remap and silently break projector cookies under reverse-Z.
    screen_to_light.setMul(screen_to_light, LLMatrix4a::perspective(fovy, aspect, near_clip, far_clip));
    screen_to_light.setMul(screen_to_light, clip_to_texture(0.5f, 0.5f));
    shader.uniformMatrix4fv(LLShaderMgr::PROJECTOR_MATRIX, screen_to_light);
    shader.uniform1f(LLShaderMgr::PROJECTOR_NEAR, near_clip);
    shader.uniform3fv(LLShaderMgr::PROJECTOR_P, 1, p1.getF32ptr());
    shader.uniform3fv(LLShaderMgr::PROJECTOR_N, 1, n.getF32ptr());
    shader.uniform3fv(LLShaderMgr::PROJECTOR_ORIGIN, 1, screen_origin.getF32ptr());
    shader.uniform1f(LLShaderMgr::PROJECTOR_RANGE, proj_range);
    shader.uniform1f(LLShaderMgr::PROJECTOR_AMBIANCE, params.mV[2]);
    S32 s_idx = -1;

    for (U32 i = 0; i < 2; i++)
    {
        if (mShadowSpotLight[i] == drawablep)
        {
            s_idx = i;
        }
    }

    shader.uniform1i(LLShaderMgr::PROJECTOR_SHADOW_INDEX, s_idx);

    if (s_idx >= 0)
    {
        shader.uniform1f(LLShaderMgr::PROJECTOR_SHADOW_FADE, 1.f-mSpotLightFade[s_idx]);
    }
    else
    {
        shader.uniform1f(LLShaderMgr::PROJECTOR_SHADOW_FADE, 1.f);
    }

    // make sure we're not already targeting the same spot light with both shadow maps
    llassert(mTargetShadowSpotLight[0] != mTargetShadowSpotLight[1] || mTargetShadowSpotLight[0].isNull());

    if (!gCubeSnapshot)
    {
        LLDrawable* potential = drawablep;
        //determine if this light is higher priority than one of the existing spot shadows
        F32 m_pri = volume->getSpotLightPriority();

        for (U32 i = 0; i < 2; i++)
        {
            F32 pri = 0.f;

            if (mTargetShadowSpotLight[i].notNull())
            {
                pri = mTargetShadowSpotLight[i]->getVOVolume()->getSpotLightPriority();
            }

            if (m_pri > pri)
            {
                LLDrawable* temp = mTargetShadowSpotLight[i];
                mTargetShadowSpotLight[i] = potential;
                potential = temp;
                m_pri = pri;
            }
        }
    }

    // make sure we didn't end up targeting the same spot light with both shadow maps
    llassert(mTargetShadowSpotLight[0] != mTargetShadowSpotLight[1] || mTargetShadowSpotLight[0].isNull());

    LLViewerTexture* img = volume->getLightTexture();

    if (img == NULL)
    {
        img = LLViewerFetchedTexture::sWhiteImagep;
    }

    S32 channel = shader.enableTexture(LLShaderMgr::DEFERRED_PROJECTION);

    if (channel > -1)
    {
        if (img)
        {
            // Projector cookie: an sRGB colour texture, decoded on the sampler so the
            // projected light filters in linear -- deferredUtil no longer decodes it in-shader.
            gGL.getTextureSlot(channel)->bindSampled(img, ALSamplers::AnisoWrapSRGB);

            // At least 1: deferredUtil's cookie edge fade divides by it (lod / (proj_lod * 0.5)),
            // and a cookie 1 px wide (or fetched no further than its smallest level) would make
            // that 0 / 0. A one-texel image has no finer level for the extra range to select.
            F32 lod_range = llmax(logf((F32)img->getWidth())/logf(2.f), 1.f);

            shader.uniform1f(LLShaderMgr::PROJECTOR_FOCUS, focus);
            shader.uniform1f(LLShaderMgr::PROJECTOR_LOD, lod_range);
            shader.uniform1f(LLShaderMgr::PROJECTOR_AMBIENT_LOD, llclamp((proj_range-focus)/proj_range*lod_range, 0.f, 1.f));
        }
    }

}

void LLPipeline::unbindDeferredShader(LLGLSLShader &shader)
{
    LLRenderTarget* deferred_target       = &mRT->deferredScreen;

    stop_glerror();
    shader.disableTexture(LLShaderMgr::NORMAL_MAP, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_DIFFUSE, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_SPECULAR, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_EMISSIVE, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_BRDF_LUT);
    //shader.disableTexture(LLShaderMgr::DEFERRED_DEPTH, deferred_depth_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_DEPTH, deferred_target->getUsage());
    shader.disableTexture(LLShaderMgr::DEFERRED_LIGHT, ALTextureSlot::TT_TEXTURE);
    shader.disableTexture(LLShaderMgr::DIFFUSE_MAP);

    // Unbind the shadow channels this shader declares, then any unit bindShadowMaps
    // actually used -- the two are the same for the shader that bound them last, and differ
    // for every earlier program with another layout. unbind() drops the compare sampler
    // along with the texture.
    for (U32 i = 0; i < 6; i++)
    {
        shader.disableTexture(LLShaderMgr::DEFERRED_SHADOW0+i);
    }
    unbindShadowMaps();

    shader.disableTexture(LLShaderMgr::DEFERRED_NOISE);

    if (!LLPipeline::sReflectionProbesEnabled)
    {
        S32 channel = shader.disableTexture(LLShaderMgr::ENVIRONMENT_MAP, ALTextureSlot::TT_CUBE_MAP);
        if (channel > -1)
        {
            LLCubeMap* cube_map = gSky.mVOSkyp ? gSky.mVOSkyp->getCubeMap() : NULL;
            if (cube_map)
            {
                cube_map->disable();
            }
        }
    }

    unbindReflectionProbes(shader);

    gGL.getTextureSlot(0)->unbind();

    // mCanBindFast asserts that this shader's deferred textures are STILL BOUND from an
    // earlier full bind, which is what lets bindDeferredShaderFast skip re-binding them.
    // We have just released them, so that promise no longer holds -- leaving the flag set
    // sends the next bind down the fast path onto channels holding whatever came after,
    // which is how a depth texture ends up under a uniform declared sampler2D.
    shader.mCanBindFast = false;

    shader.unbind();
}

void LLPipeline::setEnvMat(LLGLSLShader& shader)
{
    const F32* m = LLViewerCamera::getCurrent().getModelview().getF32ptr();

    F32 mat[] = { m[0], m[1], m[2],
                    m[4], m[5], m[6],
                    m[8], m[9], m[10] };

    shader.uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, true, mat);
}

void LLPipeline::bindReflectionProbes(LLGLSLShader& shader)
{
    if (!sReflectionProbesEnabled)
    {
        return;
    }

    S32 channel = shader.enableTexture(LLShaderMgr::REFLECTION_PROBES);
    bool bound = false;
    if (channel > -1 && mReflectionMapManager.mTexture.notNull())
    {
        mReflectionMapManager.mTexture->bind(channel);
        bound = true;
    }

    channel = shader.enableTexture(LLShaderMgr::SH_COEFFS);
    if (channel > -1 && mReflectionMapManager.mSHCoeffs.isComplete())
    {
        // Point sampled: these are coefficients indexed by texelFetch, not a filterable image.
        mReflectionMapManager.mSHCoeffs.bindTexture(0, channel, ALSamplers::PointClamp);
        bound = true;
    }

    if (mResourcePlan.heroProbes())
    {
        channel = shader.enableTexture(LLShaderMgr::HERO_PROBE);
        if (channel > -1 && mHeroProbeManager.mTexture.notNull())
        {
            mHeroProbeManager.mTexture->bind(channel);
            bound = true;
        }
    }


    if (bound)
    {
        mReflectionMapManager.setUniforms();

        setEnvMat(shader);
    }

    // reflection probe shaders generally sample the scene map as well for SSR
    channel = shader.enableTexture(LLShaderMgr::SCENE_MAP);
    if (channel > -1)
    {
        gGL.getTextureSlot(channel)->bind(&mSceneMap);
    }


    // The march parameters (iteration count, ray step, biases, glossy samples, adaptive step)
    // are frame-constant and now ride in the ReflectionProbes block, uploaded once in
    // LLReflectionMapManager::updateUniforms(). noiseSine stays loose -- it advances per bind.
    mPoissonOffset++;

    if (mPoissonOffset > 128 - RenderScreenSpaceReflectionGlossySamples)
        mPoissonOffset = 0;

    shader.uniform1f(LLShaderMgr::DEFERRED_SSR_NOISE_SINE, (GLfloat)mPoissonOffset);

    channel = shader.enableTexture(LLShaderMgr::SCENE_DEPTH);
    if (channel > -1)
    {
        gGL.getTextureSlot(channel)->bind(&mSceneMap, true);
    }


}

void LLPipeline::unbindReflectionProbes(LLGLSLShader& shader)
{
    S32 channel = shader.disableTexture(LLShaderMgr::REFLECTION_PROBES, ALTextureSlot::TT_CUBE_MAP_ARRAY);
    if (channel > -1 && mReflectionMapManager.mTexture.notNull())
    {
        // Followed by re-enabling channel 0 for TT_TEXTURE when the probe array had been on
        // it, which restored a target nothing consulted.
        mReflectionMapManager.mTexture->unbind();
    }
}


inline float sgn(float a)
{
    if (a > 0.0F) return (1.0F);
    if (a < 0.0F) return (-1.0F);
    return (0.0F);
}

void LLPipeline::renderShadow(const LLMatrix4a& view, const LLMatrix4a& proj, LLCamera& shadow_cam, LLCullResult& result, bool depth_clamp, bool do_cull, S32 occlusion)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("renderShadow");

    LLPipeline::sShadowRender = true;

    // Occlusion culling as the caller asks, by the shadow camera's own queries: none unless it does.
    U32 saved_occlusion = sUseOcclusion;
    sUseOcclusion = occlusion;

    // List of render pass types that use the prim volume as the shadow,
    // ignoring textures.
    static const U32 types[] = {
        LLRenderPass::PASS_SIMPLE,
        LLRenderPass::PASS_FULLBRIGHT,
        LLRenderPass::PASS_SHINY,
        LLRenderPass::PASS_BUMP,
        LLRenderPass::PASS_FULLBRIGHT_SHINY,
        LLRenderPass::PASS_MATERIAL,
        LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE,
        LLRenderPass::PASS_SPECMAP,
        LLRenderPass::PASS_SPECMAP_EMISSIVE,
        LLRenderPass::PASS_NORMMAP,
        LLRenderPass::PASS_NORMMAP_EMISSIVE,
        LLRenderPass::PASS_NORMSPEC,
        LLRenderPass::PASS_NORMSPEC_EMISSIVE
    };

    LLGLEnable cull(GL_CULL_FACE);

    //enable depth clamping if available
    LLGLEnable clamp_depth(depth_clamp ? GL_DEPTH_CLAMP : 0);

    LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_LESS);

    // In RenderShadowCullMode 1, do_cull is false: generateSunShadow did the single union
    // octree cull and pre-filtered `result` to this cascade's frustum (bucketShadowCull),
    // so skip the per-cascade octree walk and only sort/build this cascade's render map.
    if (do_cull)
    {
        updateCull(shadow_cam, result);
    }

    stateSort(shadow_cam, result);

    //generate shadow map
    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.pushMatrix();
    gGL.loadMatrix(proj);
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();
    gGL.loadMatrix(view);

    stop_glerror();
    gGLLastMatrix = NULL;

    gGL.getTextureSlot(0)->unbind();

    stop_glerror();

    struct CompareVertexBuffer
    {
        bool operator()(const LLDrawInfo* const& lhs, const LLDrawInfo* const& rhs)
        {
            return lhs->mVertexBuffer > rhs->mVertexBuffer;
        }
    };


    LLVertexBuffer::unbind();
    for (int j = 0; j < 2; ++j) // 0 -- static, 1 -- rigged
    {
        bool rigged = j == 1;
        gDeferredShadowProgram.bind(rigged);

        gGL.diffuseColor4f(1, 1, 1, 1);

        S32 shadow_detail = mResourceInputs.mShadowDetail;

        // if not using VSM, disable color writes
        if (shadow_detail <= 2)
        {
            gGL.setColorMask(false, false);
        }

        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow simple");
        LL_PROFILE_GPU_ZONE("shadow simple");
        gGL.getTextureSlot(0)->unbind();

        for (U32 type : types)
        {
            renderObjects(type, false, false, rigged);
        }

        renderGLTFObjects(LLRenderPass::PASS_GLTF_PBR, false, rigged);

    }

    if (LLPipeline::sUseOcclusion > 1)
    { // do occlusion culling against non-masked only to take advantage of hierarchical Z
        doOcclusion(shadow_cam);
    }


    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow geom");
        renderGeomShadow(shadow_cam);
    }

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow alpha");
        LL_PROFILE_GPU_ZONE("shadow alpha");
        const S32 sun_up = LLEnvironment::instance().getIsSunUp() ? 1 : 0;
        U32 target_width = LLRenderTarget::sCurResX;

        for (int i = 0; i < 2; ++i)
        {
            bool rigged = i == 1;

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow alpha masked");
                LL_PROFILE_GPU_ZONE("shadow alpha masked");
                gDeferredShadowAlphaMaskProgram.bind(rigged);
                LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                // Masked: opaque wherever it draws, so solid where it casts. The blended pass below
                // shares the program and stipples.
                LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_STIPPLE, 0.f);
                renderMaskedObjects(LLRenderPass::PASS_ALPHA_MASK, true, true, rigged);
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow alpha blend");
                LL_PROFILE_GPU_ZONE("shadow alpha blend");
                renderAlphaObjects(rigged);
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow fullbright alpha masked");
                LL_PROFILE_GPU_ZONE("shadow alpha masked");
                gDeferredShadowFullbrightAlphaMaskProgram.bind(rigged);
                LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_STIPPLE, 0.f); // masked
                renderFullbrightMaskedObjects(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, true, true, rigged);
            }

            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow alpha grass");
                LL_PROFILE_GPU_ZONE("shadow alpha grass");
                gDeferredTreeShadowProgram.bind(rigged);
                // Grass's own cutoff, LLDrawPoolGrass::renderDeferred's, so what draws casts. The
                // masked passes below set each batch's.
                LLGLSLShader::sCurBoundShaderPtr->setMinimumAlpha(0.5f);

                if (i == 0)
                {
                    renderObjects(LLRenderPass::PASS_GRASS, true);
                }

                {
                    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("shadow alpha material");
                    LL_PROFILE_GPU_ZONE("shadow alpha material");
                    renderMaskedObjects(LLRenderPass::PASS_NORMSPEC_MASK, true, false, rigged);
                    renderMaskedObjects(LLRenderPass::PASS_MATERIAL_ALPHA_MASK, true, false, rigged);
                    renderMaskedObjects(LLRenderPass::PASS_SPECMAP_MASK, true, false, rigged);
                    renderMaskedObjects(LLRenderPass::PASS_NORMMAP_MASK, true, false, rigged);

                    // multi-material indexed legacy mask batches alpha-test per-slot
                    if (LLGLSLShader::sIndexedLegacyMaterials && gDeferredShadowMaterialIndexedProgram.isComplete())
                    {
                        gDeferredShadowMaterialIndexedProgram.bind(rigged);
                        LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                        U32 off = rigged ? 1 : 0;
                        mAlphaMaskPool->pushMaskBatchesIndexed(LLRenderPass::PASS_NORMSPEC_MASK + off, rigged);
                        mAlphaMaskPool->pushMaskBatchesIndexed(LLRenderPass::PASS_MATERIAL_ALPHA_MASK + off, rigged);
                        mAlphaMaskPool->pushMaskBatchesIndexed(LLRenderPass::PASS_SPECMAP_MASK + off, rigged);
                        mAlphaMaskPool->pushMaskBatchesIndexed(LLRenderPass::PASS_NORMMAP_MASK + off, rigged);
                    }
                }
            }
        }

        for (int i = 0; i < 2; ++i)
        {
            bool rigged = i == 1;
            gDeferredShadowGLTFAlphaMaskProgram.bind(rigged);
            LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
            LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);

            gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
            gGLLastMatrix = NULL;

            U32 type = LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK;

            // multi-material batches alpha-test per-slot; render them with the
            // indexed shadow program so batched cutouts stay correct
            bool gltf_indexed = LLGLSLShader::sIndexedGLTFChannels >= 2 && gDeferredShadowGLTFAlphaMaskIndexedProgram.isComplete();

            if (rigged)
            {
                if (gltf_indexed)
                {
                    mAlphaMaskPool->pushRiggedGLTFBatchesScalar(type + 1);

                    gDeferredShadowGLTFAlphaMaskIndexedProgram.bind(true);
                    LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                    LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                    mAlphaMaskPool->pushRiggedGLTFBatchesIndexed(type + 1, LLRenderPass::GLTF_MAPS_BASE_COLOR); // shadow samples base color only
                }
                else
                {
                    mAlphaMaskPool->pushRiggedGLTFBatches(type + 1);
                }
            }
            else
            {
                if (gltf_indexed)
                {
                    mAlphaMaskPool->pushGLTFBatchesScalar(type);

                    gDeferredShadowGLTFAlphaMaskIndexedProgram.bind();
                    LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up);
                    LLGLSLShader::sCurBoundShaderPtr->uniform1f(LLShaderMgr::DEFERRED_SHADOW_TARGET_WIDTH, (float)target_width);
                    mAlphaMaskPool->pushGLTFBatchesIndexed(type, LLRenderPass::GLTF_MAPS_BASE_COLOR); // shadow samples base color only
                }
                else
                {
                    mAlphaMaskPool->pushGLTFBatches(type);
                }
            }

            gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
            gGLLastMatrix = NULL;
        }
    }

    gDeferredShadowCubeProgram.bind();
    gGLLastMatrix = NULL;
    gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());

    gGL.setColorMask(true, true);

    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.popMatrix();
    gGLLastMatrix = NULL;

    // reset occlusion culling flag
    sUseOcclusion = saved_occlusion;
    LLPipeline::sShadowRender = false;
}

bool LLPipeline::getVisiblePointCloud(LLCamera& camera, LLVector3& min, LLVector3& max, std::vector<LLVector3>& fp, LLVector3 light_dir)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    //get point cloud of intersection of frust and min, max

    if (getVisibleExtents(camera, min, max))
    {
        return false;
    }

    //get set of planes on bounding box
    LLPlane bp[] = {
        LLPlane(min, LLVector3(-1,0,0)),
        LLPlane(min, LLVector3(0,-1,0)),
        LLPlane(min, LLVector3(0,0,-1)),
        LLPlane(max, LLVector3(1,0,0)),
        LLPlane(max, LLVector3(0,1,0)),
        LLPlane(max, LLVector3(0,0,1))};

    //potential points
    static std::vector<LLVector3> pp;
    pp.clear();

    //add corners of AABB
    pp.push_back(LLVector3(min.mV[0], min.mV[1], min.mV[2]));
    pp.push_back(LLVector3(max.mV[0], min.mV[1], min.mV[2]));
    pp.push_back(LLVector3(min.mV[0], max.mV[1], min.mV[2]));
    pp.push_back(LLVector3(max.mV[0], max.mV[1], min.mV[2]));
    pp.push_back(LLVector3(min.mV[0], min.mV[1], max.mV[2]));
    pp.push_back(LLVector3(max.mV[0], min.mV[1], max.mV[2]));
    pp.push_back(LLVector3(min.mV[0], max.mV[1], max.mV[2]));
    pp.push_back(LLVector3(max.mV[0], max.mV[1], max.mV[2]));

    //add corners of camera frustum
    for (U32 i = 0; i < LLCamera::AGENT_FRUSTRUM_NUM; i++)
    {
        pp.push_back(camera.mAgentFrustum[i]);
    }


    //bounding box line segments
    U32 bs[] =
            {
        0,1,
        1,3,
        3,2,
        2,0,

        4,5,
        5,7,
        7,6,
        6,4,

        0,4,
        1,5,
        3,7,
        2,6
    };

    for (U32 i = 0; i < 12; i++)
    { //for each line segment in bounding box
        for (U32 j = 0; j < LLCamera::AGENT_PLANE_NO_USER_CLIP_NUM; j++)
        { //for each plane in camera frustum
            const LLPlane& cp = camera.getAgentPlane(j);
            const LLVector3& v1 = pp[bs[i*2+0]];
            const LLVector3& v2 = pp[bs[i*2+1]];
            LLVector3 n;
            cp.getVector3(n);

            LLVector3 line = v1-v2;

            F32 d1 = line*n;
            F32 d2 = -cp.dist(v2);

            F32 t = d2/d1;

            if (t > 0.f && t < 1.f)
            {
                LLVector3 intersect = v2+line*t;
                pp.push_back(intersect);
            }
        }
    }

    //camera frustum line segments
    const U32 fs[] =
    {
        0,1,
        1,2,
        2,3,
        3,0,

        4,5,
        5,6,
        6,7,
        7,4,

        0,4,
        1,5,
        2,6,
        3,7
    };

    for (U32 i = 0; i < 12; i++)
    {
        for (U32 j = 0; j < 6; ++j)
        {
            const LLVector3& v1 = pp[fs[i*2+0]+8];
            const LLVector3& v2 = pp[fs[i*2+1]+8];
            const LLPlane& cp = bp[j];
            LLVector3 n;
            cp.getVector3(n);

            LLVector3 line = v1-v2;

            F32 d1 = line*n;
            F32 d2 = -cp.dist(v2);

            F32 t = d2/d1;

            if (t > 0.f && t < 1.f)
            {
                LLVector3 intersect = v2+line*t;
                pp.push_back(intersect);
            }
        }
    }

    LLVector3 ext[] = { min-LLVector3(0.05f,0.05f,0.05f),
        max+LLVector3(0.05f,0.05f,0.05f) };

    for (U32 i = 0; i < pp.size(); ++i)
    {
        bool found = true;

        const F32* p = pp[i].mV;

        for (U32 j = 0; j < 3; ++j)
        {
            if (p[j] < ext[0].mV[j] ||
                p[j] > ext[1].mV[j])
            {
                found = false;
                break;
            }
        }

        for (U32 j = 0; j < LLCamera::AGENT_PLANE_NO_USER_CLIP_NUM; ++j)
        {
            const LLPlane& cp = camera.getAgentPlane(j);
            F32 dist = cp.dist(pp[i]);
            if (dist > 0.05f) //point is above some plane, not contained
            {
                found = false;
                break;
            }
        }

        if (found)
        {
            fp.push_back(pp[i]);
        }
    }

    if (fp.empty())
    {
        return false;
    }

    return true;
}

void LLPipeline::renderHighlight(const LLViewerObject* obj, F32 fade)
{
    if (obj && obj->getVolume())
    {
        for (LLViewerObject::child_list_t::const_iterator iter = obj->getChildren().begin(); iter != obj->getChildren().end(); ++iter)
        {
            renderHighlight(*iter, fade);
        }

        LLDrawable* drawable = obj->mDrawable;
        if (drawable)
        {
            for (S32 i = 0; i < drawable->getNumFaces(); ++i)
            {
                LLFace* face = drawable->getFace(i);
                if (face)
                {
                    face->renderSelected(LLViewerTexture::sNullImagep, LLColor4(1,1,1,fade));
                }
            }
        }
    }
}


ALRenderResourcePlan::Pack LLPipeline::renderPack() const
{
    using Pack = ALRenderResourcePlan::Pack;
    return mRT == &mMainRT ? Pack::Main : (mRT == &mAuxillaryRT ? Pack::Probe : Pack::Hero);
}

U32 LLPipeline::sunCascades() const
{
    return mResourcePlan.sunCascades(renderPack());
}

U32 LLPipeline::unbuiltShadowMaps() const
{
    const U32 cascades = sunCascades();
    U32 unbuilt = 0;
    for (U32 i = 0; i < 4; ++i)
    {
        if (i >= cascades || !mRT->shadow[i].isComplete())
        {
            unbuilt |= 1u << i;
        }
    }
    for (U32 i = 0; i < 2; ++i)
    {
        if (!mSpotShadow[i].isComplete())
        {
            unbuilt |= 1u << (4 + i);
        }
    }
    return unbuilt;
}

LLRenderTarget* LLPipeline::getSunShadowTarget(U32 i)
{
    llassert(i < 4);
    return &mRT->shadow[i];
}

LLRenderTarget* LLPipeline::getSpotShadowTarget(U32 i)
{
    llassert(i < 2);
    return &mSpotShadow[i];
}

// Re-bucket a shared sun-shadow cull (produced by a single union octree walk) down to one
// cascade: copy the union's visible/drawable groups whose object bounds intersect this
// cascade's frustum into `dst` (the same AABBInFrustumObjectBounds test the per-cascade
// cull uses, so the geometry matches mode 0 exactly), then pass the small individual-
// drawable and bridge lists through unfiltered. stateSort then builds the cascade's render
// map from `dst`. Lets RenderShadowCullMode 1 share one octree walk across all cascades.
static void bucketShadowCull(LLCullResult& src, LLCamera& cam, LLCullResult& dst)
{
    dst.clear();

    for (LLCullResult::sg_iterator i = src.beginVisibleGroups(), end = src.endVisibleGroups(); i != end; ++i)
    {
        LLSpatialGroup* group = *i;
        if (!group->isDead() &&
            cam.AABBInFrustum(group->getObjectBounds()[0], group->getObjectBounds()[1]) > 0)
        {
            dst.pushVisibleGroup(group);
        }
    }

    for (LLCullResult::sg_iterator i = src.beginDrawableGroups(), end = src.endDrawableGroups(); i != end; ++i)
    {
        LLSpatialGroup* group = *i;
        if (!group->isDead() &&
            cam.AABBInFrustum(group->getObjectBounds()[0], group->getObjectBounds()[1]) > 0)
        {
            dst.pushDrawableGroup(group);
        }
    }

    // Individual drawables and spatial bridges (attachments/animesh) are few; pass them
    // through unfiltered -- conservative (they render into every cascade) but correct.
    for (LLCullResult::drawable_iterator i = src.beginVisibleList(), end = src.endVisibleList(); i != end; ++i)
    {
        dst.pushDrawable(*i);
    }

    for (LLCullResult::bridge_iterator i = src.beginVisibleBridge(), end = src.endVisibleBridge(); i != end; ++i)
    {
        dst.pushBridge(*i);
    }
}

void LLPipeline::generateSunShadow(LLCamera& camera)
{
    if (!sRenderDeferred || sunCascades() == 0)
    {
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("generateSunShadow");

    LLDisableOcclusionCulling no_occlusion;

    bool skip_avatar_update = false;
    if (!isAgentAvatarValid() || gAgentCamera.getCameraAnimating() || gAgentCamera.getCameraMode() != CAMERA_MODE_MOUSELOOK || !LLVOAvatar::sVisibleInFirstPerson)
    {
        skip_avatar_update = true;
    }

    if (!skip_avatar_update)
    {
        gAgentAvatarp->updateAttachmentVisibility(CAMERA_MODE_THIRD_PERSON);
    }

    pushRenderTypeMask();
    andRenderTypeMask(LLPipeline::RENDER_TYPE_SIMPLE,
                    LLPipeline::RENDER_TYPE_ALPHA,
                    LLPipeline::RENDER_TYPE_ALPHA_PRE_WATER,
                    LLPipeline::RENDER_TYPE_ALPHA_POST_WATER,
                    LLPipeline::RENDER_TYPE_GRASS,
                    LLPipeline::RENDER_TYPE_GLTF_PBR,
                    LLPipeline::RENDER_TYPE_FULLBRIGHT,
                    LLPipeline::RENDER_TYPE_BUMP,
                    LLPipeline::RENDER_TYPE_VOLUME,
                    LLPipeline::RENDER_TYPE_AVATAR,
                    LLPipeline::RENDER_TYPE_CONTROL_AV,
                    LLPipeline::RENDER_TYPE_TREE,
                    LLPipeline::RENDER_TYPE_TERRAIN,
                    LLPipeline::RENDER_TYPE_WATER,
                    LLPipeline::RENDER_TYPE_VOIDWATER,
                    LLPipeline::RENDER_TYPE_PASS_ALPHA,
                    LLPipeline::RENDER_TYPE_PASS_ALPHA_MASK,
                    LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK,
                    LLPipeline::RENDER_TYPE_PASS_GRASS,
                    LLPipeline::RENDER_TYPE_PASS_SIMPLE,
                    LLPipeline::RENDER_TYPE_PASS_BUMP,
                    LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT,
                    LLPipeline::RENDER_TYPE_PASS_SHINY,
                    LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_SHINY,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP_BLEND,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP_MASK,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP_EMISSIVE,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP_BLEND,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP_MASK,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP_EMISSIVE,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC_BLEND,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC_MASK,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC_EMISSIVE,
                    LLPipeline::RENDER_TYPE_PASS_ALPHA_MASK_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_SIMPLE_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_BUMP_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_SHINY_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_FULLBRIGHT_SHINY_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP_BLEND_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP_MASK_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_SPECMAP_EMISSIVE_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP_BLEND_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP_MASK_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMMAP_EMISSIVE_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC_BLEND_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC_MASK_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_NORMSPEC_EMISSIVE_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_GLTF_PBR,
                    LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_RIGGED,
                    LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK,
                    LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK_RIGGED,
                    END_RENDER_TYPES);

    // The caller's mask comes back on every exit, the early one for an empty point cloud
    // included.
    LLGLSColorMask color_mask(false, false);

    LLEnvironment& environment = LLEnvironment::instance();

    //get sun view matrix

    //store the camera this pass renders for
    const LLCamera saved_camera = LLViewerCamera::getCurrent();
    const LLMatrix4a& saved_view = saved_camera.getModelview();
    LLMatrix4a inv_view;
    inv_view.setInverse(saved_view);

    // gGLViewport is saved alongside them because the cascade and spot loops below overwrite
    // it with each shadow map's rect (getViewport(gGLViewport)) and never put it back. It is
    // not a scratch variable: LLRenderTarget::flush() restores glViewport FROM it whenever it
    // unbinds to the default framebuffer, and stamps sCurResX/Y from it too. So a stale value
    // here silently gives the next such flush the shadow map's viewport and resolution --
    // which is how the impostor bake, later in the same frame, was handing the default
    // framebuffer a shadow-sized viewport on its way out.
    S32 saved_gl_viewport[4] = { gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3] };

    LLMatrix4a view[6];
    LLMatrix4a proj[6];

    LLVector3 caster_dir(environment.getIsSunUp() ? mSunDir : mMoonDir);

    //put together a universal "near clip" plane for shadow frusta
    LLPlane shadow_near_clip;
    {
        LLVector3 p = camera.getOrigin(); // gAgent.getPositionAgent();
        p += caster_dir * gAgentCamera.mDrawDistance*2.f;
        shadow_near_clip.setVec(p, caster_dir);
    }

    LLVector3 lightDir = -caster_dir;
    lightDir.normVec();

    //create light space camera matrix
    LLVector3 at = lightDir;

    LLVector3 up = camera.getAtAxis();

    if (fabsf(up*lightDir) > 0.75f)
    {
        up = camera.getUpAxis();
    }

    up.normVec();
    at.normVec();


    LLCamera main_camera = camera;

    F32 near_clip = 0.f;
    {
        //get visible point cloud
        static std::vector<LLVector3> fp;
        fp.clear();

        main_camera.calcAgentFrustumPlanes(main_camera.mAgentFrustum);

        LLVector3 min,max;
        getVisiblePointCloud(main_camera,min,max,fp);

        if (fp.empty())
        {
            if (!hasRenderDebugMask(RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                mShadowCamera[0] = main_camera;
                mShadowExtents[0][0] = min;
                mShadowExtents[0][1] = max;

                mShadowFrustPoints[0].clear();
                mShadowFrustPoints[1].clear();
                mShadowFrustPoints[2].clear();
                mShadowFrustPoints[3].clear();
            }
            popRenderTypeMask();

            if (!skip_avatar_update)
            {
                gAgentAvatarp->updateAttachmentVisibility(gAgentCamera.getCameraMode());
            }

            return;
        }

        //get good split distances for frustum
        for (U32 i = 0; i < fp.size(); ++i)
        {
            LLVector4a v;
            saved_view.affineTransform(LLVector4a(fp[i].mV[0], fp[i].mV[1], fp[i].mV[2], 1.f), v);
            fp[i].set(v.getF32ptr());
        }

        min = fp[0];
        max = fp[0];

        //get camera space bounding box
        for (U32 i = 1; i < fp.size(); ++i)
        {
            update_min_max(min, max, fp[i]);
        }

        near_clip    = llclamp(-max.mV[2], 0.01f, 4.0f);
        F32 far_clip = llclamp(-min.mV[2]*2.f, 16.0f, 512.0f);

        //far_clip = llmin(far_clip, 128.f);
        far_clip = llmin(far_clip, camera.getFar());

        F32 range = far_clip-near_clip;

        LLVector3 split_exp = RenderShadowSplitExponent;

        F32 da = 1.f-llmax( fabsf(lightDir*up), fabsf(lightDir*camera.getLeftAxis()) );

        da = powf(da, split_exp.mV[2]);

        F32 sxp = split_exp.mV[1] + (split_exp.mV[0]-split_exp.mV[1])*da;

        for (U32 i = 0; i < 4; ++i)
        {
            F32 x = (F32)(i+1)/4.f;
            x = powf(x, sxp);
            mSunClipPlanes.mV[i] = near_clip+range*x;
        }

        mSunClipPlanes.mV[0] *= 1.25f; //bump back first split for transition padding
    }

    // The cascades this pass renders, which are those its pack has maps for.
    const U32 cascades = sunCascades();

    if (gCubeSnapshot)
    {
        if (cascades < ALRenderResourcePlan::SUN_CASCADES)
        {
            // Two cascades rather than four, so the shadow ends with cascade 1, which reaches the last split: twice the
            // depth of what the capture sees, at least 16 m and at most its draw distance. Past it the shadow fades to lit over a fifth
            // of that distance, where clip.z and clip.w place the fade (sampleDirectionalShadow), and is lit beyond,
            // all inside cascade 1's map, which covers a quarter past its split.
            const F32 last = mSunClipPlanes.mV[3];
            mSunClipPlanes.mV[1] = last;
            mSunClipPlanes.mV[2] = last * 0.8f;
            mSunClipPlanes.mV[3] = last * 1.2f;
        }
        else
        {
            // Four, stretched so that the fade to lit lies past what the capture draws, as two cascades' does: cascade 1
            // to the third split, 2 to the last, and 3 half as far again.
            mSunClipPlanes.mV[1] = mSunClipPlanes.mV[2];
            mSunClipPlanes.mV[2] = mSunClipPlanes.mV[3];
            mSunClipPlanes.mV[3] *= 1.5f;
        }
    }


    // convenience array of 4 near clip plane distances
    F32 dist[] = { near_clip, mSunClipPlanes.mV[0], mSunClipPlanes.mV[1], mSunClipPlanes.mV[2], mSunClipPlanes.mV[3] };

    if (mSunDiffuse == LLColor4::black)
    { //sun diffuse is totally black shadows don't matter
        skipRenderingShadows();
    }
    else
    {
        // RenderShadowCullMode 1: do the expensive octree cull ONCE against a frustum
        // spanning every sun cascade, then have each cascade cheaply re-bucket the union's
        // visible groups by its own frustum (bucketShadowCull) and build its own render
        // map. Saves 3 of 4 octree walks per frame while each cascade still renders only
        // its own slice -- GPU-neutral vs. per-cascade culling, so it helps CPU-bound
        // targets without regressing GPU-bound ones. Disabled in cube snapshots. Default 0.
        static LLCachedControl<S32> sShadowCullMode(gSavedSettings, "RenderShadowCullMode", 0);
        bool have_union_cull = false;
        static LLCullResult sUnionShadowResult;

        // Occlusion culling of the cascades' casters, where it is asked for: a caster the sun cannot see past the ones
        // nearer it leaves no depth in a map. Not with the union cull, whose groups each cascade takes without their
        // occlusion state, nor in probe captures, nor in a frame whose depth the main view does not trust either.
        static LLCachedControl<bool> shadow_occlusion(gSavedSettings, "AlchemyRenderShadowOcclusion", false);
        const S32 sun_occlusion =
            shadow_occlusion() && no_occlusion.mUseOcclusion > 1 && sShadowCullMode() == 0 && !gCubeSnapshot ? 2 : 0;
        if (!gCubeSnapshot)
        {
            // What the cascades' queries found holds along the direction they were asked: it is forgotten once that has
            // turned by a degree, as it does at once from the sun to the moon or under an edit, and when the culling
            // comes back on.
            constexpr F32    TURNED = 0.99985f; // cos 1 degree
            static bool      occluding = false;
            static LLVector3 occluding_along;
            if (sun_occlusion && (!occluding || occluding_along * lightDir < TURNED))
            {
                for (U32 j = 0; j < 4; ++j)
                {
                    LLOcclusionCullingGroup::forgetOcclusion(LLViewerCamera::CAMERA_SUN_SHADOW0 + j);
                }
                occluding_along = lightDir;
            }
            occluding = sun_occlusion != 0;
        }

        if (sShadowCullMode() == 1 && !gCubeSnapshot)
        {
            LLCamera ucam = camera;
            ucam.setFar(16.f);
            LLViewerCamera::updateFrustumPlanes(ucam, false, false, true);

            LLVector3 ueye = camera.getOrigin();
            LLVector3* ufrust = ucam.mAgentFrustum;
            LLVector3 upn = ucam.getAtAxis();
            for (U32 i = 0; i < 4; i++)
            {
                LLVector3 delta = ufrust[i+4]-ueye;
                delta += (ufrust[i+4]-ufrust[(i+2)%4+4])*0.05f;
                delta.normVec();
                F32 dp = delta*upn;
                ufrust[i]   = ueye + (delta*dist[0]*0.75f)/dp;
                ufrust[i+4] = ueye + (delta*dist[4]*1.25f)/dp;
            }

            {
                const LLMatrix4a uview = LLMatrix4a::lookDir(LLVector4a(camera.getOrigin().mV[0], camera.getOrigin().mV[1], camera.getOrigin().mV[2]), LLVector4a(lightDir.mV[0], lightDir.mV[1], lightDir.mV[2]), LLVector4a(-up.mV[0], -up.mV[1], -up.mV[2]));

                // AABB the 8 full-range frustum corners directly in light space. ufrust
                // spans [dist[0], dist[4]] (built above), so this box is a guaranteed
                // superset of every cascade. getVisiblePointCloud is NOT usable here: the
                // far corners sit past the view far plane, so it clips the cloud down to
                // the 4 near corners and the union collapses to a dot at the camera.
                LLVector4a corner;
                uview.affineTransform(LLVector4a(ufrust[0].mV[0], ufrust[0].mV[1], ufrust[0].mV[2], 1.f), corner);
                LLVector3 mn(corner.getF32ptr());
                LLVector3 mx = mn;
                for (U32 i = 1; i < 8; i++)
                {
                    uview.affineTransform(LLVector4a(ufrust[i].mV[0], ufrust[i].mV[1], ufrust[i].mV[2], 1.f), corner);
                    update_min_max(mn, mx, LLVector3(corner.getF32ptr()));
                }

                LLVector3 ucenter = (mn+mx)*0.5f;

                // Conservative ortho light-space projection bounding the whole point
                // cloud, which updateFrustumPlanes derives the cull frustum from. Ortho
                // is looser than the per-cascade perspective fit, so the result is a
                // superset of every cascade frustum -- no dropped casters.
                //
                // Pad the depth range: with the sun near-overhead the light-space
                // footprint is nearly planar (znear ~= zfar), which makes the ortho
                // singular and updateFrustumPlanes unproject to NaN frustum corners --
                // shadows then drop and flip with camera angle. The near plane is
                // replaced by shadow_near_clip below and the far only needs to clear the
                // receivers, so widening the depth range is always safe.
                F32 zpad = llmax(mx.mV[0] - mn.mV[0], mx.mV[1] - mn.mV[1]) * 0.5f + 1.f;
                // al_ortho so the cull frustum matches the active convention (this feeds
                // updateFrustumPlanes, which unprojects with the same convention).
                const LLMatrix4a uproj = al_ortho(mn.mV[0], mx.mV[0], mn.mV[1], mx.mV[1], -mx.mV[2] - zpad, -mn.mV[2] + zpad);

                ucam.lookAt(ueye, ucenter, up);
                ucam.setOrigin(0, 0, 0);

                LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_SUN_SHADOW0;
                ucam.setModelview(uview);
                ucam.setProjection(uproj);
                LLViewerCamera::updateFrustumPlanes(ucam, false, false, true);
                LLViewerCamera::setCurrent(ucam);
                ucam.setAgentPlane(LLCamera::AGENT_PLANE_NEAR, shadow_near_clip);

                bool saved_shadow_render = LLPipeline::sShadowRender;
                U32 saved_occlusion = sUseOcclusion;
                LLPipeline::sShadowRender = true;
                // Disable occlusion culling for the shadow cull exactly as renderShadow
                // does: occlusion queries are main-camera and previous-frame based, so
                // leaving them on wrongly culls casters hidden from the main view (their
                // shadows still show) and flickers as the queries resolve frame to frame.
                sUseOcclusion = 0;
                // One octree walk for the whole sun shadow. No stateSort here -- each
                // cascade re-buckets these visible groups and sorts its own render map.
                updateCull(ucam, sUnionShadowResult);
                sUseOcclusion = saved_occlusion;
                LLPipeline::sShadowRender = saved_shadow_render;

                // restore the main camera (the cascade loop sets its own each iteration)
                LLViewerCamera::setCurrent(saved_camera);

                have_union_cull = true;
            }
        }

        for (S32 j = 0; j < (S32)cascades; j++)
        {
            if (!hasRenderDebugMask(RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                mShadowFrustPoints[j].clear();
            }

            LLViewerCamera::sCurCameraID = (LLViewerCamera::eCameraID)(LLViewerCamera::CAMERA_SUN_SHADOW0+j);

            //restore the main camera
            LLViewerCamera::setCurrent(saved_camera);

            LLVector3 eye = camera.getOrigin();
            llassert(eye.isFinite());

            //camera used for shadow cull/render
            LLCamera shadow_cam;

            //create world space camera frustum for this split
            shadow_cam = camera;
            shadow_cam.setFar(16.f);

            LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

            LLVector3* frust = shadow_cam.mAgentFrustum;

            LLVector3 pn = shadow_cam.getAtAxis();

            LLVector3 min, max;

            //construct 8 corners of split frustum section
            for (U32 i = 0; i < 4; i++)
            {
                LLVector3 delta = frust[i+4]-eye;
                delta += (frust[i+4]-frust[(i+2)%4+4])*0.05f;
                delta.normVec();
                F32 dp = delta*pn;
                frust[i] = eye + (delta*dist[j]*0.75f)/dp;
                frust[i+4] = eye + (delta*dist[j+1]*1.25f)/dp;
            }

            shadow_cam.calcAgentFrustumPlanes(frust);
            shadow_cam.mFrustumCornerDist = 0.f;

            if (!gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                mShadowCamera[j] = shadow_cam;
            }

            static std::vector<LLVector3> fp;
            fp.clear();

            if (!gPipeline.getVisiblePointCloud(shadow_cam, min, max, fp, lightDir)
                || j > RenderShadowSplits)
            {
                //no possible shadow receivers
                if (!gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
                {
                    mShadowExtents[j][0] = LLVector3();
                    mShadowExtents[j][1] = LLVector3();
                    mShadowCamera[j+4] = shadow_cam;
                }

                if (mRT->shadow[j].isComplete())
                {
                    mRT->shadow[j].bindTarget();
                    {
                        LLGLDepthTest depth(GL_TRUE);
                        mRT->shadow[j].clear();
                    }
                    mRT->shadow[j].flush();
                }

                mShadowError.mV[j] = 0.f;
                mShadowFOV.mV[j] = 0.f;

                continue;
            }

            if (!gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                mShadowExtents[j][0] = min;
                mShadowExtents[j][1] = max;
                mShadowFrustPoints[j] = fp;
            }


            //find a good origin for shadow projection
            LLVector3 origin;

            //get a temporary view projection
            view[j] = LLMatrix4a::lookDir(LLVector4a(camera.getOrigin().mV[0], camera.getOrigin().mV[1], camera.getOrigin().mV[2]), LLVector4a(lightDir.mV[0], lightDir.mV[1], lightDir.mV[2]), LLVector4a(-up.mV[0], -up.mV[1], -up.mV[2]));

            std::vector<LLVector3> wpf;

            for (U32 i = 0; i < fp.size(); i++)
            {
                LLVector4a p;
                view[j].affineTransform(LLVector4a(fp[i].mV[0], fp[i].mV[1], fp[i].mV[2], 1.f), p);
                wpf.push_back(LLVector3(p.getF32ptr()));
            }

            min = wpf[0];
            max = wpf[0];

            for (U32 i = 0; i < fp.size(); ++i)
            { //get AABB in camera space
                update_min_max(min, max, wpf[i]);
            }

            // Construct a perspective transform with perspective along y-axis that contains
            // points in wpf
            //Known:
            // - far clip plane
            // - near clip plane
            // - points in frustum
            //Find:
            // - origin

            //get some "interesting" points of reference
            LLVector3 center = (min+max)*0.5f;
            LLVector3 size = (max-min)*0.5f;
            LLVector3 near_center = center;
            near_center.mV[1] += size.mV[1]*2.f;


            //put all points in wpf in quadrant 0, reletive to center of min/max
            //get the best fit line using least squares
            F32 bfm = 0.f;
            F32 bfb = 0.f;

            for (U32 i = 0; i < wpf.size(); ++i)
            {
                wpf[i] -= center;
                wpf[i].mV[0] = fabsf(wpf[i].mV[0]);
                wpf[i].mV[2] = fabsf(wpf[i].mV[2]);
            }

            if (!wpf.empty())
            {
                F32 sx = 0.f;
                F32 sx2 = 0.f;
                F32 sy = 0.f;
                F32 sxy = 0.f;

                for (U32 i = 0; i < wpf.size(); ++i)
                {
                    sx += wpf[i].mV[0];
                    sx2 += wpf[i].mV[0]*wpf[i].mV[0];
                    sy += wpf[i].mV[1];
                    sxy += wpf[i].mV[0]*wpf[i].mV[1];
                }

                bfm = (sy*sx-wpf.size()*sxy)/(sx*sx-wpf.size()*sx2);
                bfb = (sx*sxy-sy*sx2)/(sx*sx-bfm*sx2);
            }

            {
                // best fit line is y=bfm*x+bfb

                //find point that is furthest to the right of line
                F32 off_x = -1.f;
                LLVector3 lp;

                for (U32 i = 0; i < wpf.size(); ++i)
                {
                    //y = bfm*x+bfb
                    //x = (y-bfb)/bfm
                    F32 lx = (wpf[i].mV[1]-bfb)/bfm;

                    lx = wpf[i].mV[0]-lx;

                    if (off_x < lx)
                    {
                        off_x = lx;
                        lp = wpf[i];
                    }
                }

                //get line with slope bfm through lp
                // bfb = y-bfm*x
                bfb = lp.mV[1]-bfm*lp.mV[0];

                //calculate error
                mShadowError.mV[j] = 0.f;

                for (U32 i = 0; i < wpf.size(); ++i)
                {
                    F32 lx = (wpf[i].mV[1]-bfb)/bfm;
                    mShadowError.mV[j] += fabsf(wpf[i].mV[0]-lx);
                }

                mShadowError.mV[j] /= wpf.size();
                mShadowError.mV[j] /= size.mV[0];

                if (mShadowError.mV[j] > RenderShadowErrorCutoff)
                { //just use ortho projection
                    mShadowFOV.mV[j] = -1.f;
                    origin.clearVec();
                    proj[j] = al_ortho(min.mV[0], max.mV[0],
                                        min.mV[1], max.mV[1],
                                        -max.mV[2], -min.mV[2]);
                }
                else
                {
                    //origin is where line x = 0;
                    origin.setVec(0,bfb,0);

                    F32 fovz = 1.f;
                    F32 fovx = 1.f;

                    LLVector3 zp;
                    LLVector3 xp;

                    for (U32 i = 0; i < wpf.size(); ++i)
                    {
                        LLVector3 atz = wpf[i]-origin;
                        atz.mV[0] = 0.f;
                        atz.normVec();
                        if (fovz > -atz.mV[1])
                        {
                            zp = wpf[i];
                            fovz = -atz.mV[1];
                        }

                        LLVector3 atx = wpf[i]-origin;
                        atx.mV[2] = 0.f;
                        atx.normVec();
                        if (fovx > -atx.mV[1])
                        {
                            fovx = -atx.mV[1];
                            xp = wpf[i];
                        }
                    }

                    fovx = acos(fovx);
                    fovz = acos(fovz);

                    F32 cutoff = llmin((F32) RenderShadowFOVCutoff, 1.4f);

                    mShadowFOV.mV[j] = fovx;

                    if (fovx < cutoff && fovz > cutoff)
                    {
                        //x is a good fit, but z is too big, move away from zp enough so that fovz matches cutoff
                        F32 d = zp.mV[2]/tan(cutoff);
                        F32 ny = zp.mV[1] + fabsf(d);

                        origin.mV[1] = ny;

                        fovz = 1.f;
                        fovx = 1.f;

                        for (U32 i = 0; i < wpf.size(); ++i)
                        {
                            LLVector3 atz = wpf[i]-origin;
                            atz.mV[0] = 0.f;
                            atz.normVec();
                            fovz = llmin(fovz, -atz.mV[1]);

                            LLVector3 atx = wpf[i]-origin;
                            atx.mV[2] = 0.f;
                            atx.normVec();
                            fovx = llmin(fovx, -atx.mV[1]);
                        }

                        fovx = acos(fovx);
                        fovz = acos(fovz);

                        mShadowFOV.mV[j] = cutoff;
                    }


                    origin += center;

                    F32 ynear = -(max.mV[1]-origin.mV[1]);
                    F32 yfar = -(min.mV[1]-origin.mV[1]);

                    if (ynear < 0.1f) //keep a sensible near clip plane
                    {
                        F32 diff = 0.1f-ynear;
                        origin.mV[1] += diff;
                        ynear += diff;
                        yfar += diff;
                    }

                    if (fovx > cutoff)
                    { //just use ortho projection
                        origin.clearVec();
                        mShadowError.mV[j] = -1.f;
                        proj[j] = al_ortho(min.mV[0], max.mV[0],
                                min.mV[1], max.mV[1],
                                -max.mV[2], -min.mV[2]);
                    }
                    else
                    {
                        //get perspective projection
                        view[j].invert();
                        //llassert(origin.isFinite());

                        //translate view to origin
                        LLVector4a origin_agent;
                        view[j].affineTransform(LLVector4a(origin.mV[0], origin.mV[1], origin.mV[2], 1.f), origin_agent);

                        eye.set(origin_agent.getF32ptr());
                        //llassert(eye.isFinite());
                        if (!hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
                        {
                            mShadowFrustOrigin[j] = eye;
                        }

                        view[j] = LLMatrix4a::lookDir(origin_agent, LLVector4a(lightDir.mV[0], lightDir.mV[1], lightDir.mV[2]), LLVector4a(-up.mV[0], -up.mV[1], -up.mV[2]));

                        F32 fx = 1.f/tanf(fovx);
                        F32 fz = 1.f/tanf(fovz);

                        proj[j].mMatrix[0].set(-fx, 0.f, 0.f, 0.f);
                        proj[j].mMatrix[1].set(0.f, (yfar + ynear) / (ynear - yfar), 0.f, -1.f);
                        proj[j].mMatrix[2].set(0.f, 0.f, -fz, 0.f);
                        proj[j].mMatrix[3].set(0.f, (2.f * yfar * ynear) / (ynear - yfar), 0.f, 0.f);
                        // Depth rides clip.w (= -view.y, the w column here); al_reverse_z_transform
                        // rewrites the depth column to (1 - ndc_z)/2 using that same w column, so
                        // it reverses this non-standard perspective correctly. Do not hand-derive.
                        if (LLRender::sReverseZ)
                        {
                            proj[j] = al_reverse_z_transform(proj[j]);
                        }
                    }
                }
            }

            //shadow_cam.setFar(128.f);
            shadow_cam.lookAt(eye, center, up);

            shadow_cam.setOrigin(0,0,0);

            shadow_cam.setModelview(view[j]);
            shadow_cam.setProjection(proj[j]);

            LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);
            LLViewerCamera::setCurrent(shadow_cam);

            //shadow_cam.ignoreAgentFrustumPlane(LLCamera::AGENT_PLANE_NEAR);
            shadow_cam.setAgentPlane(LLCamera::AGENT_PLANE_NEAR, shadow_near_clip);

            // translate and scale from [-1,1] to [0,1]. Under reverse-Z the projection
            // already yields [0,1] shadow-map z, so z passes through (no 0.5*z+0.5); only
            // xy are remapped. shadow_matrix then lands the receiver in the reversed depth.
            const LLMatrix4a trans = LLRender::sReverseZ ? clip_to_texture(1.f, 0.f) : clip_to_texture(0.5f, 0.5f);

            mShadowModelview[j] = view[j];
            mShadowProjection[j] = proj[j];
            // from the main camera's eye space: back to world, into the light's view,
            // through its projection, into the map
            mSunShadowMatrix[j].setMul(inv_view, view[j]);
            mSunShadowMatrix[j].setMul(mSunShadowMatrix[j], proj[j]);
            mSunShadowMatrix[j].setMul(mSunShadowMatrix[j], trans);

            stop_glerror();

            // A cascade whose map could not be built is not drawn, and its shadows are read lit (unbuiltShadowMaps).
            if (mRT->shadow[j].isComplete())
            {
                mRT->shadow[j].bindTarget();
                mRT->shadow[j].getViewport(gGLViewport);
                mRT->shadow[j].clear();

                {
                    static LLCullResult result[4];
                    if (have_union_cull)
                    {   // re-bucket the shared union cull down to this cascade's frustum
                        bucketShadowCull(sUnionShadowResult, shadow_cam, result[j]);
                    }
                    renderShadow(view[j], proj[j], shadow_cam, result[j], true, !have_union_cull, sun_occlusion);
                }

                mRT->shadow[j].flush();
            }

            if (!gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_SHADOW_FRUSTA) && !gCubeSnapshot)
            {
                mShadowCamera[j+4] = shadow_cam;
            }
        }
    }

    //hack to disable projector shadows
    bool gen_shadow = mResourcePlan.wants(ALRenderResourcePlan::Id::SpotShadow0);

    if (gen_shadow)
    {
        if (!gCubeSnapshot) //skip updating spot shadow maps during cubemap updates
        {
            LLTrace::CountStatHandle<>* velocity_stat = LLViewerCamera::getVelocityStat();
            F32 fade_amt = gFrameIntervalSeconds.value()
                * (F32)llmax(LLTrace::get_frame_recording().getLastRecording().getSum(*velocity_stat) / LLTrace::get_frame_recording().getLastRecording().getDuration().value(), 1.0);

            // should never happen
            llassert(mTargetShadowSpotLight[0] != mTargetShadowSpotLight[1] || mTargetShadowSpotLight[0].isNull());

            //update shadow targets
            for (U32 i = 0; i < 2; i++)
            { //for each current shadow
                LLViewerCamera::sCurCameraID = (LLViewerCamera::eCameraID)(LLViewerCamera::CAMERA_SPOT_SHADOW0 + i);

                if (mShadowSpotLight[i].notNull() &&
                    (mShadowSpotLight[i] == mTargetShadowSpotLight[0] ||
                        mShadowSpotLight[i] == mTargetShadowSpotLight[1]))
                { //keep this spotlight
                    mSpotLightFade[i] = llmin(mSpotLightFade[i] + fade_amt, 1.f);
                }
                else
                { //fade out this light
                    mSpotLightFade[i] = llmax(mSpotLightFade[i] - fade_amt, 0.f);

                    if (mSpotLightFade[i] == 0.f || mShadowSpotLight[i].isNull())
                    { //faded out, grab one of the pending spots (whichever one isn't already taken)
                        if (mTargetShadowSpotLight[0] != mShadowSpotLight[(i + 1) % 2])
                        {
                            mShadowSpotLight[i] = mTargetShadowSpotLight[0];
                        }
                        else
                        {
                            mShadowSpotLight[i] = mTargetShadowSpotLight[1];
                        }
                    }
                }
            }
        }

        // this should never happen
        llassert(mShadowSpotLight[0] != mShadowSpotLight[1] || mShadowSpotLight[0].isNull());

        for (S32 i = 0; i < 2; i++)
        {
            LLViewerCamera::setCurrent(saved_camera);

            if (mShadowSpotLight[i].isNull())
            {
                continue;
            }

            LLVOVolume* volume = mShadowSpotLight[i]->getVOVolume();

            if (!volume)
            {
                mShadowSpotLight[i] = NULL;
                continue;
            }

            LLDrawable* drawable = mShadowSpotLight[i];

            LLVector3 params = volume->getSpotLightParams();
            F32 fov = params.mV[0];

            //get agent->light space matrix (modelview)
            LLVector3 center = drawable->getPositionAgent();
            LLQuaternion quat = volume->getRenderRotation();

            //get near clip plane
            LLVector3 scale = volume->getScale();
            LLVector3 at_axis(0, 0, -scale.mV[2] * 0.5f);
            at_axis *= quat;

            LLVector3 np = center + at_axis;
            at_axis.normVec();

            //get origin that has given fov for plane np, at_axis, and given scale
            F32 dist = (scale.mV[1] * 0.5f) / tanf(fov * 0.5f);

            LLVector3 origin = np - at_axis * dist;

            LLMatrix4 mat(quat, LLVector4(origin, 1.f));

            view[i + 4].setInverse(LLMatrix4a(mat));

            //get perspective matrix
            F32 near_clip = dist + 0.01f;
            F32 width = scale.mV[VX];
            F32 height = scale.mV[VY];
            F32 far_clip = dist + volume->getLightRadius() * 1.5f;

            F32 fovy = fov; // radians
            F32 aspect = width / height;

            proj[i + 4] = al_perspective(fovy, aspect, near_clip, far_clip);

            // translate and scale from [-1,1] to [0,1]. Under reverse-Z the projection
            // already yields [0,1] shadow-map z, so z passes through (no 0.5*z+0.5); only
            // xy are remapped. shadow_matrix then lands the receiver in the reversed depth.
            const LLMatrix4a trans = LLRender::sReverseZ ? clip_to_texture(1.f, 0.f) : clip_to_texture(0.5f, 0.5f);

            mSunShadowMatrix[i + 4].setMul(inv_view, view[i + 4]);
            mSunShadowMatrix[i + 4].setMul(mSunShadowMatrix[i + 4], proj[i + 4]);
            mSunShadowMatrix[i + 4].setMul(mSunShadowMatrix[i + 4], trans);

            mShadowModelview[i + 4] = view[i + 4];
            mShadowProjection[i + 4] = proj[i + 4];

            // Skipped during cubemap updates, and for a map that could not be built, whose shadows are read lit
            // (unbuiltShadowMaps).
            if (!gCubeSnapshot && mSpotShadow[i].isComplete())
            {
                LLCamera shadow_cam = camera;
                shadow_cam.setFar(far_clip);
                shadow_cam.setOrigin(origin);
                shadow_cam.setModelview(view[i + 4]);
                shadow_cam.setProjection(proj[i + 4]);

                LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);
                LLViewerCamera::setCurrent(shadow_cam);

                //

                mSpotShadow[i].bindTarget();
                mSpotShadow[i].getViewport(gGLViewport);
                mSpotShadow[i].clear();

                static LLCullResult result[2];

                LLViewerCamera::sCurCameraID = (LLViewerCamera::eCameraID)(LLViewerCamera::CAMERA_SPOT_SHADOW0 + i);

                RenderSpotLight = drawable;

                renderShadow(view[i + 4], proj[i + 4], shadow_cam, result[i], false);

                RenderSpotLight = nullptr;

                mSpotShadow[i].flush();
            }
        }
    }
    else
    { //no spotlight shadows
        mShadowSpotLight[0] = mShadowSpotLight[1] = NULL;
    }


    if (!CameraOffset)
    {
        LLViewerCamera::setCurrent(saved_camera);
    }
    else
    {
        LLCamera offset_cam = camera;
        offset_cam.setModelview(view[1]);
        offset_cam.setProjection(proj[1]);
        LLViewerCamera::setCurrent(offset_cam);
        gGL.loadMatrix(view[1]);
        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.loadMatrix(proj[1]);
        gGL.matrixMode(LLRender::MM_MODELVIEW);
    }

    gGLViewport[0] = saved_gl_viewport[0];
    gGLViewport[1] = saved_gl_viewport[1];
    gGLViewport[2] = saved_gl_viewport[2];
    gGLViewport[3] = saved_gl_viewport[3];

    popRenderTypeMask();

    if (!skip_avatar_update)
    {
        gAgentAvatarp->updateAttachmentVisibility(gAgentCamera.getCameraMode());
    }
}

void LLPipeline::renderGroups(LLRenderPass* pass, U32 type, bool texture)
{
    for (LLCullResult::sg_iterator i = sCull->beginVisibleGroups(); i != sCull->endVisibleGroups(); ++i)
    {
        LLSpatialGroup* group = *i;
        if (!group->isDead() &&
            (!sUseOcclusion || !group->isOcclusionState(LLSpatialGroup::OCCLUDED)) &&
            gPipeline.hasRenderType(group->getSpatialPartition()->mDrawableType) &&
            group->mDrawMap.find(type) != group->mDrawMap.end())
        {
            pass->renderGroup(group,type,texture);
        }
    }
}

void LLPipeline::renderRiggedGroups(LLRenderPass* pass, U32 type, bool texture)
{
    for (LLCullResult::sg_iterator i = sCull->beginVisibleGroups(); i != sCull->endVisibleGroups(); ++i)
    {
        LLSpatialGroup* group = *i;
        if (!group->isDead() &&
            (!sUseOcclusion || !group->isOcclusionState(LLSpatialGroup::OCCLUDED)) &&
            gPipeline.hasRenderType(group->getSpatialPartition()->mDrawableType) &&
            group->mDrawMap.find(type) != group->mDrawMap.end())
        {
            pass->renderRiggedGroup(group, type, texture);
        }
    }
}

void LLPipeline::profileAvatar(LLVOAvatar* avatar, bool profile_attachments)
{
    if (profile_attachments ? !mAttachmentSpans.empty() : avatar->isProfilePending())
    { // the last profile's times are still on their way; another would throw them away
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;

    // don't continue to profile an avatar that is known to be too slow
    llassert(!avatar->isTooSlow());

    LLGLSLShader* cur_shader = LLGLSLShader::sCurBoundShaderPtr;

    // Scissoring OFF for the duration. This is reached from LLFloaterPerformance::draw(),
    // i.e. from inside gViewerWindow->draw(), where the UI has the scissor test enabled and
    // clipped to its dirty rectangle. Inherited, that rectangle clips the avatar render AND
    // the target clear below -- so the GPU timing this function exists to measure was taken
    // on a partially clipped draw, which is the one thing it must not be.
    LLGLDisable no_scissor(GL_SCISSOR_TEST);

    // The depth and colour state display() gives a world render. No caller has it: the UI's
    // LLGLSUIDefault turns depth testing and depth writes off, and the autotune caller runs
    // from the main loop's work queue, outside display(), where neither is on. Without it
    // the clear below leaves depth alone and the opaque pools draw untested, so the time
    // measured is not the avatar's.
    LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LEQUAL);
    LLGLSColorMask color_mask(true, true);

    // The globals generateImpostor overwrites are saved here rather than there, because the
    // display() path repairs them itself immediately afterwards and this path does not: it
    // returns into the middle of a UI draw. sCull in particular is left pointing at
    // generateImpostor's function-static cull result, which the next updateCull would
    // otherwise be the first thing to notice.
    LLCullResult* saved_cull = sCull;
    const LLCamera saved_camera = LLViewerCamera::getCurrent();

    mRT->deferredScreen.bindTarget();
    mRT->deferredScreen.clear();

    if (!profile_attachments)
    {
        // profile entire avatar all at once and readback asynchronously
        avatar->placeProfileQuery();

        LLTimer cpu_timer;

        generateImpostor(avatar, false, true);

        avatar->mCPURenderTime = (F32)cpu_timer.getElapsedTimeF32() * 1000.f;

        // read by the avatar's idleUpdate() once its frame has been
        avatar->endProfileQuery();
    }
    else
    {
        // profile attachments one at a time, each in a span of gGL's pool, read by readAttachmentTimes() once their
        // frame has been
        ALQueryPool& queries = gGL.queries();
        LLVOAvatar::attachment_map_t::iterator iter;
        LLVOAvatar::attachment_map_t::iterator begin = avatar->mAttachmentPoints.begin();
        LLVOAvatar::attachment_map_t::iterator end = avatar->mAttachmentPoints.end();

        for (iter = begin;
            iter != end;
            ++iter)
        {
            LLViewerJointAttachment* attachment = iter->second;
            for (LLViewerJointAttachment::attachedobjs_vec_t::iterator attachment_iter = attachment->mAttachedObjects.begin();
                attachment_iter != attachment->mAttachedObjects.end();
                ++attachment_iter)
            {
                LLViewerObject* attached_object = attachment_iter->get();
                if (attached_object)
                {
                    ALQuerySpan span = queries.beginSpan();
                    generateImpostor(avatar, false, true, attached_object);
                    queries.endSpan(span);
                    mAttachmentSpans.emplace_back(attached_object, span);
                }
            }
        }
    }

    mRT->deferredScreen.flush();

    sCull = saved_cull;
    LLViewerCamera::setCurrent(saved_camera);

    // generateImpostor's clear colour is its own business everywhere else, because display()
    // sets one before each clear it cares about. Nothing does that on the way back into a UI
    // draw, so put it back to the ambient here.
    gGL.setClearColor(LLColor4::transparent);

    if (cur_shader)
    {
        cur_shader->bind();
    }
}

void LLPipeline::drawFarPlaneQuad(U32 depth_func, const LLColor4* color)
{
    gGL.getTextureSlot(0)->unbind();

    // Both directions auto-translate under reverse-Z (via LLGLDepthTest::remap),
    // and the quad sits at the reverse-Z far plane (~0) instead of ~1.
    LLGLDepthTest depth(GL_TRUE, GL_FALSE, depth_func);

    gGL.flush();

    gGL.pushMatrix();
    gGL.loadIdentity();
    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.pushMatrix();
    gGL.loadIdentity();

    // Mirror the forward WINDOW depth (0.99999 ndc -> 0.999995 window -> 0.000005 under ZO),
    // consistent with the sunDisc/moon far pins.
    const F32 clip_plane = LLRender::sReverseZ ? 0.000005f : 0.99999f;

    gDebugProgram.bind();

    if (color)
    {
        gGL.diffuseColor4fv(color->mV);
    }

    gGL.begin(LLRender::TRIANGLES);
    {
        gGL.vertex3f(-1.f, -1.f, clip_plane);
        gGL.vertex3f(1.f, -1.f, clip_plane);
        gGL.vertex3f(1.f, 1.f, clip_plane);

        gGL.vertex3f(-1.f, -1.f, clip_plane);
        gGL.vertex3f(1.f, 1.f, clip_plane);
        gGL.vertex3f(-1.f, 1.f, clip_plane);
    }
    gGL.end();
    gGL.flush();

    gDebugProgram.unbind();

    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.popMatrix();
}

void LLPipeline::seedCutoutCoverageFromDepth()
{
    LL_PROFILE_GPU_ZONE("cutout coverage seed");

    LLGLDisable blend(GL_BLEND);

    // Nothing covers the background, so it is cleared to nothing, colour and
    // alpha both. Lighting left it the sky's emissive copy (magenta, with the
    // emissive buffer on) or black; the blended geometry drawn over it next has
    // to be composited over black for the colour to come out premultiplied by
    // the coverage accumulated alongside it.
    {
        LLGLSColorMask mask(true, true);
        drawFarPlaneQuad(GL_LEQUAL, &LLColor4::transparent);
    }

    // Opaque geometry covers its texel whole. The alpha pool's blend adds to
    // this (alphaWritesCoverage), so a blended surface over opaque stays at 1
    // and over the background becomes its own coverage.
    {
        LLGLSColorMask mask(false, true);
        drawFarPlaneQuad(GL_GREATER, &LLColor4::white);
    }
}

void LLPipeline::copyCutoutCoverageToAlpha()
{
    LL_PROFILE_GPU_ZONE("cutout coverage copy");

    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLDisable blend(GL_BLEND);
    LLGLSColorMask mask(false, true);

    // Across the whole frame, sampled as the final blit sampled the colour, so
    // the two agree texel for texel whatever the scene target's resolution.
    const LLRect& rect = gViewerWindow->getWorldViewRectRaw();
    glViewport(rect.mLeft, rect.mBottom, rect.getWidth(), rect.getHeight());

    gCopyProgram.bind();
    gCopyProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mRT->screen);
    mScreenTriangleVB->setBuffer();
    mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    gCopyProgram.unbind();
}

void LLPipeline::clearSceneMap()
{
    if (mSceneMap.isComplete())
    {
        // The depth clear value is the reverse-Z latch's far plane. Clears obey
        // the write masks, so both are opened for it.
        LLGLSColorMask mask(true, true);
        LLGLDepthTest depth(GL_TRUE, GL_TRUE);
        mSceneMap.bindTarget();
        glClearColor(0.f, 0.f, 0.f, 0.f);
        mSceneMap.clear();
        mSceneMap.flush();
    }
}

void LLPipeline::readAttachmentTimes()
{
    if (mAttachmentSpans.empty())
    {
        return;
    }
    const ALQueryPool& queries = gGL.queries();
    std::erase_if(mAttachmentSpans, [&queries](const auto& timed)
    {
        U64 nanoseconds = 0;
        switch (queries.result(timed.second, nanoseconds))
        {
        case ALQueryPool::Status::Ready:
            timed.first->mGPURenderTime = nanoseconds / 1000000.f;
            return true;
        case ALQueryPool::Status::Expired:
            return true;
        default:
            return false;
        }
    });
}

void LLPipeline::generateImpostor(LLVOAvatar* avatar, bool preview_avatar, bool for_profile, LLViewerObject* specific_attachment)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("generateImpostor");
    LLGLState::checkStates();

    static LLCullResult result;
    result.clear();
    grabReferences(result);

    if (!avatar || avatar->isDead() || !avatar->mDrawable)
    {
        LL_WARNS_ONCE("AvatarRenderPipeline") << "Avatar is " << (avatar ? "not drawable" : "null") << LL_ENDL;
        return;
    }
    LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID() << " is drawable" << LL_ENDL;

    assertInitialized();

    // previews can't be muted or impostered
    bool visually_muted = !for_profile && !preview_avatar && avatar->isVisuallyMuted();
    LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID()
                              << " is " << ( visually_muted ? "" : "not ") << "visually muted"
                              << LL_ENDL;
    bool too_complex = !for_profile && !preview_avatar && avatar->isTooComplex();
    LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID()
                              << " is " << ( too_complex ? "" : "not ") << "too complex"
                              << LL_ENDL;

    pushRenderTypeMask();

    if (visually_muted || too_complex)
    {
        // only show jelly doll geometry
        andRenderTypeMask(LLPipeline::RENDER_TYPE_AVATAR,
                            LLPipeline::RENDER_TYPE_CONTROL_AV,
                            END_RENDER_TYPES);
    }
    else
    {
        //hide world geometry
        clearRenderTypeMask(
            RENDER_TYPE_SKY,
            RENDER_TYPE_WL_SKY,
            RENDER_TYPE_TERRAIN,
            RENDER_TYPE_GRASS,
            RENDER_TYPE_CONTROL_AV, // Animesh
            RENDER_TYPE_TREE,
            RENDER_TYPE_VOIDWATER,
            RENDER_TYPE_WATER,
            RENDER_TYPE_ALPHA_PRE_WATER,
            RENDER_TYPE_PASS_GRASS,
            RENDER_TYPE_HUD,
            RENDER_TYPE_PARTICLES,
            RENDER_TYPE_CLOUDS,
            RENDER_TYPE_HUD_PARTICLES,
            END_RENDER_TYPES
         );
    }

    if (specific_attachment && specific_attachment->isHUDAttachment())
    { //enable HUD rendering
        setRenderTypeMask(RENDER_TYPE_HUD, END_RENDER_TYPES);
    }

    S32 occlusion = sUseOcclusion;
    sUseOcclusion = 0;

    // Saved, not assumed. These were put back as literal `false`, which is only correct while
    // no caller has them set -- true today, but it makes the function silently non-reentrant
    // into any shadow or probe context, and it is the same hardcoded-restore habit the rest of
    // this work has been removing. sUseOcclusion right above already does it properly.
    const bool saved_shadow_render   = sShadowRender;
    const bool saved_impostor_render = sImpostorRender;

    sShadowRender = true;
    sImpostorRender = true;

    LLViewerCamera* viewer_camera = LLViewerCamera::getInstance();

    {
        markVisible(avatar->mDrawable, *viewer_camera);

        if (preview_avatar)
        {
            // Only show rigged attachments for preview
            // For the sake of performance and so that static
            // objects won't obstruct previewing changes
            LLVOAvatar::attachment_map_t::iterator iter;
            for (iter = avatar->mAttachmentPoints.begin();
                iter != avatar->mAttachmentPoints.end();
                ++iter)
            {
                LLViewerJointAttachment *attachment = iter->second;
                for (LLViewerJointAttachment::attachedobjs_vec_t::iterator attachment_iter = attachment->mAttachedObjects.begin();
                    attachment_iter != attachment->mAttachedObjects.end();
                    ++attachment_iter)
                {
                    LLViewerObject* attached_object = attachment_iter->get();
                    if (attached_object)
                    {
                        if (attached_object->isRiggedMesh())
                        {
                            markVisible(attached_object->mDrawable->getSpatialBridge(), *viewer_camera);
                        }
                        else
                        {
                            // sometimes object is a linkset and rigged mesh is a child
                            LLViewerObject::const_child_list_t& child_list = attached_object->getChildren();
                            for (LLViewerObject::child_list_t::const_iterator iter = child_list.begin();
                                iter != child_list.end(); iter++)
                            {
                                LLViewerObject* child = *iter;
                                if (child->isRiggedMesh())
                                {
                                    markVisible(attached_object->mDrawable->getSpatialBridge(), *viewer_camera);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            if (specific_attachment)
            {
                markVisible(specific_attachment->mDrawable->getSpatialBridge(), *viewer_camera);
            }
            else
            {
                LLVOAvatar::attachment_map_t::iterator iter;
                LLVOAvatar::attachment_map_t::iterator begin = avatar->mAttachmentPoints.begin();
                LLVOAvatar::attachment_map_t::iterator end = avatar->mAttachmentPoints.end();

                for (iter = begin;
                    iter != end;
                    ++iter)
                {
                    LLViewerJointAttachment* attachment = iter->second;
                    for (LLViewerJointAttachment::attachedobjs_vec_t::iterator attachment_iter = attachment->mAttachedObjects.begin();
                        attachment_iter != attachment->mAttachedObjects.end();
                        ++attachment_iter)
                    {
                        LLViewerObject* attached_object = attachment_iter->get();
                        if (attached_object)
                        {
                            markVisible(attached_object->mDrawable->getSpatialBridge(), *viewer_camera);
                        }
                    }
                }
            }
        }
    }

    stateSort(*LLViewerCamera::getInstance(), result);

    LLCamera camera = *viewer_camera;
    LLVector2 tdim;
    U32 resY = 0;
    U32 resX = 0;

    if (!preview_avatar)
    {
        const LLVector4a* ext = avatar->mDrawable->getSpatialExtents();
        LLVector3 pos(avatar->getRenderPosition()+avatar->getImpostorOffset());

        camera.lookAt(viewer_camera->getOrigin(), pos, viewer_camera->getUpAxis());

        LLVector4a half_height;
        half_height.setSub(ext[1], ext[0]);
        half_height.mul(0.5f);

        // The half-extent of a box projected onto a direction is sum(half_i * |d_i|) -- the
        // component-wise ABSOLUTE value of the axis, dotted with the half extents.
        //
        // This used to square the axis and renormalize, which computes
        // sum(half_i * d_i^2) / sqrt(sum(d_i^4)). That agrees with the real answer only when
        // the axis is aligned to a box axis or has equal components, and is otherwise too
        // small -- so the captured frame was tighter than the avatar at oblique camera
        // angles, clipping outstretched arms, tall hair and wide attachments at the edge of
        // the impostor. The axes are already unit length, and taking absolute values does not
        // change that, so the renormalize goes with it.
        LLVector4a left;
        left.load3(camera.getLeftAxis().mV);
        left.setAbs(left);

        LLVector4a up;
        up.load3(camera.getUpAxis().mV);
        up.setAbs(up);

        // No fabsf needed now: both operands are non-negative by construction.
        tdim.mV[0] = half_height.dot3(left).getF32();
        tdim.mV[1] = half_height.dot3(up).getF32();

        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.pushMatrix();

        F32 distance = (pos-camera.getOrigin()).length();
        F32 fov = atanf(tdim.mV[1]/distance)*2.f*RAD_TO_DEG;
        F32 aspect = tdim.mV[0]/tdim.mV[1];

        // Clip planes bracketing the subject instead of a hardcoded 1..256.
        //
        // Impostoring is chosen by rank, not distance (LLVOAvatar::shouldImpostor), so an
        // avatar beyond 256 m is a normal thing to be asked to bake -- and the old far plane
        // clipped it out of its own capture, leaving a fully transparent billboard. The near
        // plane was equally arbitrary in the other direction: at any real impostor distance
        // it threw away most of the depth range, which is what the coverage stamp's depth test
        // and any depth-sorted content inside the bake have to resolve against.
        //
        // The bounding sphere of the extents is what has to fit, and 10cm of slack keeps a
        // caster exactly on the plane from being clipped by rounding.
        const F32 radius = half_height.getLength3().getF32() + 0.1f;
        const F32 near_clip = llmax(0.1f, distance - radius);
        const F32 far_clip  = distance + radius;

        const LLMatrix4a persp = al_perspective(fov * DEG_TO_RAD, aspect, near_clip, far_clip);
        camera.setProjection(persp);
        gGL.loadMatrix(persp);

        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.pushMatrix();

        const LLMatrix4a mat = camera.frameModelview();
        camera.setModelview(mat);
        gGL.loadMatrix(mat);
        LLViewerCamera::setCurrent(camera);

        // Remember the basis the G-buffer normals about to be captured are encoded in. The
        // billboard replays them into the scene G-buffer verbatim, where the lighting pass
        // reads normals as MAIN-view-space -- and this camera is aimed at the avatar, not
        // along the main camera's forward axis, so the two differ by the avatar's angular
        // offset from screen centre. Left unrebased, an impostor is lit from a direction that
        // slides as it crosses the screen.
        {
            // the basis transposed: the rotation back out of this view
            LLMatrix3 bake_rot;
            for (U32 r = 0; r < 3; ++r)
            {
                for (U32 c = 0; c < 3; ++c)
                {
                    bake_rot.mMatrix[r][c] = mat.mMatrix[c][r];
                }
            }
            avatar->setImpostorViewRotation(bake_rot);
        }

        gGL.setClearColor(LLColor4::transparent);
        gGL.setColorMask(true, true);

        // get the number of pixels per angle
        F32 pa = gViewerWindow->getWindowHeightRaw() / (RAD_TO_DEG * viewer_camera->getView());

        //get resolution based on angle width and height of impostor (double desired resolution to prevent aliasing)
        resY = llmin(nhpo2((U32) (fov*pa)), (U32) 512);
        resX = llmin(nhpo2((U32) (atanf(tdim.mV[0]/distance)*2.f*RAD_TO_DEG*pa)), (U32) 512);

        if (!for_profile)
        {
            // A resolution change rebuilds rather than resizing in place. allocate()
            // releases first, so this is the same path as first creation and keeps the
            // deferred attachments and TFO_POINT filtering that a resize would not have
            // re-applied to fresh textures.
            if (!avatar->mImpostor.isComplete()
                || resX != avatar->mImpostor.getWidth()
                || resY != avatar->mImpostor.getHeight())
            {
                // sRGB, matching deferredScreen. renderGeomDeferred() runs the ordinary
                // G-buffer pools into this target for the avatar's attachments, and those
                // passes shade in linear and rely on the pass's hoisted GL_FRAMEBUFFER_SRGB
                // (renderGeomDeferred) to encode on store. A plain GL_RGBA8 attachment gives
                // that enable nothing to act on, so the linear result would be stored raw
                // and then decoded a second time when the impostor writer feeds it back into
                // the real G-buffer -- attachments on impostored avatars rendering too dark.
                avatar->mImpostor.allocate(resX, resY, GL_SRGB8_ALPHA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, mainDepthFormat());

                if (LLPipeline::sRenderDeferred)
                {
                    addDeferredAttachments(avatar->mImpostor);
                }

                // Point filtering is applied where the impostor is sampled
                // (LLVOAvatar::renderImpostor), not written onto the texture here.
            }

            avatar->mImpostor.bindTarget();
        }
    }

    F32 old_alpha = LLDrawPoolAvatar::sMinimumAlpha;

    if (visually_muted || too_complex)
    { //disable alpha masking for muted avatars (get whole skin silhouette)
        LLDrawPoolAvatar::sMinimumAlpha = 0.f;
    }

    // The bake target is sRGB (GL_SRGB8_ALPHA8, see the allocate above), so its contract is
    // "store display-encoded". renderGeomDeferred hoists this enable for the G-buffer pools
    // itself; renderGeomPostDeferred never did, because in an ordinary frame it targets the
    // RGBA16F screen where linear output is right. Into the impostor that left every forward
    // writer -- fullbright alpha, PBR alpha, the material programs, the avatar-alpha eyeball
    // pass -- storing linear values that the composite then decoded a second time, so those
    // parts of an impostored avatar came out roughly v^2.2 too dark while the opaque parts
    // were correct. Every writer into this target shades in linear now, including the
    // impostor alpha program, which was the last one still sampling encoded and writing raw.
    LLGLEnable impostor_srgb(GL_FRAMEBUFFER_SRGB);

    // The forward passes declare a single fragment output, and here they run into a
    // multi-attachment G-buffer -- unlike an ordinary frame, where renderGeomPostDeferred
    // draws into the single-attachment screen target. Leaving every attachment selected lets
    // the driver blend undefined values into the normal and ORM buffers wherever alpha drew,
    // and those are exactly what light the billboard afterwards. Narrowed AFTER the G-buffer
    // pass, which needs all of them, and restored at the end of the function because the
    // preview path draws into a target its caller still owns.
    LLRenderTarget* bake_target = LLRenderTarget::getCurrentBoundTarget();
    auto forward_only_attachments = [bake_target]()
    {
        if (bake_target)
        {
            bake_target->setDrawBuffers(1);
        }
    };

    // Stamp full coverage over everything the OPAQUE pass laid down, BEFORE the forward
    // passes run.
    //
    // Order is load-bearing. This marks every texel the depth buffer covers as fully opaque,
    // so it has to see only opaque geometry -- run after the alpha pass, it flattened blended
    // coverage to 1 and threw away the very thing the coverage scheme exists to keep. That is
    // why blended geometry was excluded from the impostor's depth buffer, and excluding it is
    // what cost alpha its self-occlusion: layers that used to depth-reject each other all
    // blended together instead, so a mostly-opaque blended surface showed itself through
    // itself. Stamping first lets alpha write depth again and occlude normally, while the
    // coverage accumulation behind it still starts from 1 where opaque geometry sits (a' =
    // src.a + (1-src.a)*1 == 1) and from 0 over background.
    auto stamp_opaque_coverage = [&]()
    {
        if (!for_profile)
        { //create alpha mask based on depth buffer (grey out if muted)
            if (LLPipeline::sRenderDeferred)
            {
                // This pass writes one output too, and it now runs BEFORE the forward
                // passes narrow the draw buffers, so it has to state the requirement for
                // itself. Through the target that owns the attachment list rather than by
                // hand, so the restore at the end can put back however many it actually has.
                forward_only_attachments();
            }

            LLGLDisable blend(GL_BLEND);

            // Scoped, like the blend and depth guards around it. Set loose, this was the one bit
            // of state generateImpostor never put back: the function returned with colour writes
            // DISABLED on the ordinary path, and the frame survived only because display()
            // re-asserts a mask before the world render. The dynamic-texture path has no such
            // caller, so every morph preview after the first in a frame lost its background.
            LLGLSColorMask mask(visually_muted || too_complex, true);

            LLColor4 fill_color;
            const LLColor4* fill = nullptr;
            if (visually_muted)
            {   // Visually muted avatar
                fill_color = LLColor4(avatar->getMutedAVColor());
                fill = &fill_color;
                LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID() << " MUTED set solid color " << fill_color << LL_ENDL;
            }
            else if (!preview_avatar)
            { //grey muted avatar
                fill = &LLColor4::pink;
                LL_DEBUGS_ONCE("AvatarRenderPipeline") << "Avatar " << avatar->getID() << " MUTED set grey" << LL_ENDL;
            }

            drawFarPlaneQuad(GL_GREATER, fill);
        }
    };

    if (preview_avatar || for_profile)
    {
        // previews and profiles don't care about imposters
        renderGeomDeferred(camera);
        stamp_opaque_coverage();
        forward_only_attachments();
        renderGeomPostDeferred(camera);
    }
    else
    {
        avatar->mImpostor.clear();
        renderGeomDeferred(camera);

        stamp_opaque_coverage();
        forward_only_attachments();
        renderGeomPostDeferred(camera);

        // The scene used to be rendered a THIRD time here, colour-masked off, purely so
        // that blended geometry would lay down depth for a coverage mask that ran afterwards
        // -- the "shameless hack" the old comment named. Both are gone: the mask stamps
        // opaque coverage before the forward passes instead, and the alpha channel carries
        // real coverage from there (LLDrawPoolAlpha::forwardRender). One fewer full scene
        // render per impostor, and partial coverage survives to the composite instead of
        // being flattened to opaque, which is what produced the dark halo around hair.
    }

    LLDrawPoolAvatar::sMinimumAlpha = old_alpha;

    if (!preview_avatar && !for_profile)
    {
        avatar->mImpostor.flush();
        avatar->setImpostorDim(tdim);
    }

    // Put the attachment selection back. On the impostor path the flush above has already
    // rebound whatever was underneath (bindTarget re-issues its own draw buffers), but the
    // PREVIEW path never bound a target of its own -- it has been drawing into one the
    // dynamic-texture system owns and will keep using, so leaving it narrowed dropped
    // attachments 1..3 for every preview rendered after this one in the same frame.
    if (bake_target && bake_target == LLRenderTarget::getCurrentBoundTarget())
    {
        bake_target->setDrawBuffers();
    }

    sUseOcclusion = occlusion;
    sImpostorRender = saved_impostor_render;
    sShadowRender = saved_shadow_render;
    popRenderTypeMask();

    if (!preview_avatar)
    {
        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.popMatrix();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.popMatrix();
    }

    if (!preview_avatar && !for_profile)
    {
        avatar->mNeedsImpostorUpdate = false;
        avatar->cacheImpostorValues();
        avatar->mLastImpostorUpdateFrameTime = gFrameTimeSeconds;
    }

    LLVertexBuffer::unbind();
    LLGLState::checkStates();
}

bool LLPipeline::hasRenderBatches(const U32 type) const
{
    return sCull->getRenderMapSize(type) > 0;
}

LLCullResult::drawinfo_iterator LLPipeline::beginRenderMap(U32 type)
{
    return sCull->beginRenderMap(type);
}

LLCullResult::drawinfo_iterator LLPipeline::endRenderMap(U32 type)
{
    return sCull->endRenderMap(type);
}

LLCullResult::sg_iterator LLPipeline::beginAlphaGroups()
{
    return sCull->beginAlphaGroups();
}

LLCullResult::sg_iterator LLPipeline::endAlphaGroups()
{
    return sCull->endAlphaGroups();
}

LLCullResult::sg_iterator LLPipeline::beginRiggedAlphaGroups()
{
    return sCull->beginRiggedAlphaGroups();
}

LLCullResult::sg_iterator LLPipeline::endRiggedAlphaGroups()
{
    return sCull->endRiggedAlphaGroups();
}

bool LLPipeline::hasRenderType(const U32 type) const
{
    // STORM-365 : LLViewerJointAttachment::setAttachmentVisibility() is setting type to 0 to actually mean "do not render"
    // We then need to test that value here and return false to prevent attachment to render (in mouselook for instance)
    // TODO: reintroduce RENDER_TYPE_NONE in LLRenderTypeMask and initialize its mRenderTypeEnabled[RENDER_TYPE_NONE] to false explicitely
    return (type == 0 ? false : mRenderTypeEnabled[type]);
}

void LLPipeline::setRenderTypeMask(U32 type, ...)
{
    va_list args;

    va_start(args, type);
    while (type < END_RENDER_TYPES)
    {
        mRenderTypeEnabled[type] = true;
        type = va_arg(args, U32);
    }
    va_end(args);

    if (type > END_RENDER_TYPES)
    {
        LL_ERRS() << "Invalid render type." << LL_ENDL;
    }
}

bool LLPipeline::hasAnyRenderType(U32 type, ...) const
{
    va_list args;

    va_start(args, type);
    while (type < END_RENDER_TYPES)
    {
        if (mRenderTypeEnabled[type])
        {
            va_end(args);
            return true;
        }
        type = va_arg(args, U32);
    }
    va_end(args);

    if (type > END_RENDER_TYPES)
    {
        LL_ERRS() << "Invalid render type." << LL_ENDL;
    }

    return false;
}

void LLPipeline::pushRenderTypeMask()
{
    std::string cur_mask;
    cur_mask.assign((const char*) mRenderTypeEnabled, sizeof(mRenderTypeEnabled));
    mRenderTypeEnableStack.push(cur_mask);
}

void LLPipeline::popRenderTypeMask()
{
    if (mRenderTypeEnableStack.empty())
    {
        LL_ERRS() << "Depleted render type stack." << LL_ENDL;
    }

    memcpy(mRenderTypeEnabled, mRenderTypeEnableStack.top().data(), sizeof(mRenderTypeEnabled));
    mRenderTypeEnableStack.pop();
}

void LLPipeline::andRenderTypeMask(U32 type, ...)
{
    va_list args;

    bool tmp[NUM_RENDER_TYPES];
    for (U32 i = 0; i < NUM_RENDER_TYPES; ++i)
    {
        tmp[i] = false;
    }

    va_start(args, type);
    while (type < END_RENDER_TYPES)
    {
        if (mRenderTypeEnabled[type])
        {
            tmp[type] = true;
        }

        type = va_arg(args, U32);
    }
    va_end(args);

    if (type > END_RENDER_TYPES)
    {
        LL_ERRS() << "Invalid render type." << LL_ENDL;
    }

    for (U32 i = 0; i < LLPipeline::NUM_RENDER_TYPES; ++i)
    {
        mRenderTypeEnabled[i] = tmp[i];
    }

}

void LLPipeline::clearRenderTypeMask(U32 type, ...)
{
    va_list args;

    va_start(args, type);
    while (type < END_RENDER_TYPES)
    {
        mRenderTypeEnabled[type] = false;

        type = va_arg(args, U32);
    }
    va_end(args);

    if (type > END_RENDER_TYPES)
    {
        LL_ERRS() << "Invalid render type." << LL_ENDL;
    }
}

void LLPipeline::setAllRenderTypes()
{
    for (U32 i = 0; i < NUM_RENDER_TYPES; ++i)
    {
        mRenderTypeEnabled[i] = true;
    }
}

void LLPipeline::clearAllRenderTypes()
{
    for (U32 i = 0; i < NUM_RENDER_TYPES; ++i)
    {
        mRenderTypeEnabled[i] = false;
    }
}

void LLPipeline::addDebugBlip(const LLVector3& position, const LLColor4& color)
{
    DebugBlip blip(position, color);
    mDebugBlips.push_back(blip);
}

void LLPipeline::hidePermanentObjects( std::vector<U32>& restoreList )
{
    //This method is used to hide any vo's from the object list that may have
    //the permanent flag set.

    U32 objCnt = gObjectList.getNumObjects();
    for (U32 i = 0; i < objCnt; ++i)
    {
        LLViewerObject* pObject = gObjectList.getObject(i);
        if ( pObject && pObject->flagObjectPermanent() )
        {
            LLDrawable *pDrawable = pObject->mDrawable;

            if ( pDrawable )
            {
                restoreList.push_back( i );
                hideDrawable( pDrawable );
            }
        }
    }

    skipRenderingOfTerrain( true );
}

void LLPipeline::restorePermanentObjects( const std::vector<U32>& restoreList )
{
    //This method is used to restore(unhide) any vo's from the object list that may have
    //been hidden because their permanency flag was set.

    std::vector<U32>::const_iterator itCurrent  = restoreList.begin();
    std::vector<U32>::const_iterator itEnd      = restoreList.end();

    U32 objCnt = gObjectList.getNumObjects();

    while ( itCurrent != itEnd )
    {
        U32 index = *itCurrent;
        LLViewerObject* pObject = NULL;
        if ( index < objCnt )
        {
            pObject = gObjectList.getObject( index );
        }
        if ( pObject )
        {
            LLDrawable *pDrawable = pObject->mDrawable;
            if ( pDrawable )
            {
                pDrawable->clearState( LLDrawable::FORCE_INVISIBLE );
                unhideDrawable( pDrawable );
            }
        }
        ++itCurrent;
    }

    skipRenderingOfTerrain( false );
}

void LLPipeline::skipRenderingOfTerrain( bool flag )
{
    pool_set_t::iterator iter = mPools.begin();
    while ( iter != mPools.end() )
    {
        LLDrawPool* pPool = *iter;
        U32 poolType = pPool->getType();
        if ( hasRenderType( pPool->getType() ) && poolType == LLDrawPool::POOL_TERRAIN )
        {
            pPool->setSkipRenderFlag( flag );
        }
        ++iter;
    }
}

void LLPipeline::hideObject( const LLUUID& id )
{
    LLViewerObject *pVO = gObjectList.findObject( id );

    if ( pVO )
    {
        LLDrawable *pDrawable = pVO->mDrawable;

        if ( pDrawable )
        {
            hideDrawable( pDrawable );
        }
    }
}

void LLPipeline::hideDrawable( LLDrawable *pDrawable )
{
    pDrawable->setState( LLDrawable::FORCE_INVISIBLE );
    markRebuild( pDrawable, LLDrawable::REBUILD_ALL);
    //hide the children
    LLViewerObject::const_child_list_t& child_list = pDrawable->getVObj()->getChildren();
    for ( LLViewerObject::child_list_t::const_iterator iter = child_list.begin();
          iter != child_list.end(); iter++ )
    {
        LLViewerObject* child = *iter;
        LLDrawable* drawable = child->mDrawable;
        if ( drawable )
        {
            drawable->setState( LLDrawable::FORCE_INVISIBLE );
            markRebuild( drawable, LLDrawable::REBUILD_ALL);
        }
    }
}
void LLPipeline::unhideDrawable( LLDrawable *pDrawable )
{
    pDrawable->clearState( LLDrawable::FORCE_INVISIBLE );
    markRebuild( pDrawable, LLDrawable::REBUILD_ALL);
    //restore children
    LLViewerObject::const_child_list_t& child_list = pDrawable->getVObj()->getChildren();
    for ( LLViewerObject::child_list_t::const_iterator iter = child_list.begin();
          iter != child_list.end(); iter++)
    {
        LLViewerObject* child = *iter;
        LLDrawable* drawable = child->mDrawable;
        if ( drawable )
        {
            drawable->clearState( LLDrawable::FORCE_INVISIBLE );
            markRebuild( drawable, LLDrawable::REBUILD_ALL);
        }
    }
}
void LLPipeline::restoreHiddenObject( const LLUUID& id )
{
    LLViewerObject *pVO = gObjectList.findObject( id );

    if ( pVO )
    {
        LLDrawable *pDrawable = pVO->mDrawable;
        if ( pDrawable )
        {
            unhideDrawable( pDrawable );
        }
    }
}

void LLPipeline::skipRenderingShadows()
{
    LLGLDepthTest depth(GL_TRUE);

    for (U32 j = 0, cascades = sunCascades(); j < cascades; j++)
    {
        if (mRT->shadow[j].isComplete())
        {
            mRT->shadow[j].bindTarget();
            mRT->shadow[j].clear();
            mRT->shadow[j].flush();
        }
    }
}

class LLOctreeDirty : public OctreeTraveler
{
public:
    virtual void visit(const OctreeNode* state)
    {
        LLSpatialGroup* group = (LLSpatialGroup*)state->getListener(0);

        if (group->getSpatialPartition()->mRenderByGroup)
        {
            group->setState(LLSpatialGroup::GEOM_DIRTY);
            gPipeline.markRebuild(group);
        }

        for (LLSpatialGroup::bridge_list_t::iterator i = group->mBridgeList.begin(); i != group->mBridgeList.end(); ++i)
        {
            LLSpatialBridge* bridge = *i;
            traverse(bridge->mOctree);
        }
    }
};

// Called from LLViewHighlightTransparent when "Highlight Transparent" is toggled
void LLPipeline::rebuildDrawInfo()
{
    const U32 types_to_traverse[] =
    {
        LLViewerRegion::PARTITION_VOLUME,
        LLViewerRegion::PARTITION_BRIDGE,
        LLViewerRegion::PARTITION_AVATAR
    };

    LLOctreeDirty dirty;
    for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
    {
        for (U32 type : types_to_traverse)
        {
            LLSpatialPartition* part = region->getSpatialPartition(type);
            dirty.traverse(part->mOctree);
        }
    }
}

void LLPipeline::rebuildTerrain()
{
    for (LLWorld::region_list_t::const_iterator iter = LLWorld::getInstance()->getRegionList().begin();
        iter != LLWorld::getInstance()->getRegionList().end(); ++iter)
    {
        LLViewerRegion* region = *iter;
        region->dirtyAllPatches();
    }
}
