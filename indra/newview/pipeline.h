/**
 * @file pipeline.h
 * @brief Rendering pipeline definitions
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

#ifndef LL_PIPELINE_H
#define LL_PIPELINE_H

#include "llcamera.h"
#include "llerror.h"
#include "lldrawpool.h"
#include "llspatialpartition.h"
#include "m4math.h"
#include "llpointer.h"
#include "lldrawpoolalpha.h"
#include "lldrawpoolmaterials.h"
#include "llgl.h"
#include "lldrawable.h"
#include "aldepthpyramid.h"
#include "alprimitiveplot.h"
#include "algpubuffer.h"
#include "alquerypool.h"
#include "alrenderinputrequests.h"
#include "alrenderresourceplan.h"
#include "alshaderinputs.h"
#include "altransienttargetpool.h"
#include "altransienttargets.h"
#include "alwatersurface.h"
#include "altexture3d.h"
#include "aluniformbuffer.h"
#include "llrendertarget.h"
#include "llreflectionmapmanager.h"
#include "llheroprobemanager.h"
#include "alscopedata.h"

#include <array>
#include <stack>

class LLViewerTexture;
class LLFace;
class LLViewerObject;
class LLTextureEntry;
class LLCullResult;
class LLVOAvatar;
class LLVOPartGroup;
class LLGLSLShader;
class LLDrawPoolAlpha;
class LLSettingsSky;

typedef enum e_avatar_skinning_method
{
    SKIN_METHOD_SOFTWARE,
    SKIN_METHOD_VERTEX_PROGRAM
} EAvatarSkinningMethod;

bool compute_min_max(LLMatrix4& box, LLVector2& min, LLVector2& max); // Shouldn't be defined here!
bool LLRayAABB(const LLVector3 &center, const LLVector3 &size, const LLVector3& origin, const LLVector3& dir, LLVector3 &coord, F32 epsilon = 0);
bool setup_hud_matrices(); // use whole screen to render hud
bool setup_hud_matrices(const LLRect& screen_region); // specify portion of screen (in pixels) to render hud attachments from (for picking)

class LLPipeline
{
public:
    static constexpr U32 BLOOM_MAX_MIPS = ALRenderResourcePlan::BLOOM_MAX_MIPS;

    LLPipeline();
    ~LLPipeline();

    void destroyGL();
    void restoreGL();

    /// Marks a change to the render inputs for the next application (applyRenderInputs), and does nothing else.
    void markRenderInputsDirty(ALRenderInputRequests::Change change = ALRenderInputRequests::Change::Inputs);
    /// Whether the window has been resized since the inputs were last applied: display() draws nothing that frame.
    bool windowResizePending() const { return mRenderInputRequests.windowResized(); }
    /// Applies every change marked since the last application, once, at the top of each frame, through the one path
    /// into a reload or a reconcile: the inputs latched (latchRenderInputs), the programs loaded for the shader inputs
    /// where they moved (LLViewerShaderMgr::setShaders), the resources reconciled where theirs moved
    /// (rebuildGLBuffers), and what derives from either invalidated. Nothing while a snapshot holds the render size.
    void applyRenderInputs();
    /// The same application for the pipeline's first load and a restored context: every program built and every
    /// resource reconciled, with whatever was marked until then.
    void loadRenderInputs();

    /// A snapshot's render size, before the resolution scale, until endSizeOverride (ALRenderSizeOverride): the
    /// resources reconciled to the latched inputs at that size, which rebuilds only what the size sizes, and no
    /// application runs meanwhile. Whether the size was built in full rather than halved for want of memory.
    bool beginSizeOverride(U32 width, U32 height);
    /// The snapshot ends: the next application reconciles to the inputs as the settings say, the window's size among
    /// them, so snapshots taken back to back at one size build that size once.
    void endSizeOverride();

    /// The inputs as they were last latched: the resources' and the programs'.
    const ALRenderResourceInputs& latchedInputs() const { return mLatchedInputs; }
    const ALShaderInputs&         latchedShaderInputs() const { return mLatchedShaderInputs; }
    /// The inputs the programs were last loaded for (LLViewerShaderMgr::setShaders), which a pass drawing with them
    /// reads rather than the settings.
    const ALShaderInputs& shaderInputs() const;

    /// Every GL resource the pipeline holds, and the state derived from them: on context teardown and at shutdown.
    void releaseAllGLResources();

    /// What a rebuildGLBuffers runs for, as its log line says.
    enum class RebuildCause : U8
    {
        Setting,
        ShaderReload, ///< the programs were reloaded just before
        Resize,
        Snapshot, ///< an ALRenderSizeOverride began
    };


    /// What reconcileResources did, and what it changed that state derived from the resources depends on.
    struct ResourceChanges
    {
        ALRenderResourcePlan::IdSet mBuilt;
        ALRenderResourcePlan::IdSet mKept;
        ALRenderResourcePlan::IdSet mFreed;
        ALRenderResourcePlan::IdSet mFailed;
        /// The transient resources the pool holds one of, or built one of, for the frames to acquire.
        ALRenderResourcePlan::IdSet mPooled;
        bool mMainSize       = false; ///< the main G-buffer's size
        bool mGBufferFormats = false; ///< the main G-buffer's colour formats
        bool mDepthFormat    = false; ///< the main G-buffer's depth format
        bool mHDR            = false; ///< RenderHDREnabled
        bool mProbePack      = false; ///< a target the reflection probes' captures render through
        bool mHeroPack       = false; ///< a target the mirrors' captures render through
        bool mReflectionProbesFreed = false; ///< the reflection probe manager's own resources were freed
        bool mHeroProbesFreed       = false; ///< the hero probe manager's
        bool mFullSize       = true;  ///< the render size was built in full, not halved for want of memory
    };

    /// Plans the render resources from @a requested (ALRenderResourcePlan::make) and makes each what the plan says:
    /// built where it differs from its description, kept without a GL call where it matches, freed where the plan no
    /// longer wants it. Depth owners are built before the targets sharing their depth. Where a target the render size
    /// sizes cannot be built, the size is halved and those targets planned again.
    ResourceChanges reconcileResources(const ALRenderResourceInputs& requested);

    /// The inputs and the plan the resources were last reconciled to.
    const ALRenderResourceInputs& resourceInputs() const { return mResourceInputs; }
    const ALRenderResourcePlan& resourcePlan() const { return mResourcePlan; }

    /// Draws the BRDF LUT with gDeferredGenBrdfLutProgram into its target.
    void createBrdfLut();
    /// The auto-exposure's adapted state and the lens flare's history, as a first frame finds them.
    void resetExposureHistory();
    void setupGradingLUT();
    void generateLensDirt();
    /// Re-bake the tone curve lookup row from the four RenderColorGradeCurve*
    /// settings. Runs from colorCorrect when mToneCurveLutDirty is set: once
    /// per change, never per frame.
    void bakeToneCurveLut();

    /// The size the main view renders a view of @a resX by @a resY at: RenderResolutionDivisor applied, or else
    /// RenderResolutionMultiplier, as the inputs were last latched.
    static void scaleToRenderResolution(U32& resX, U32& resY);

private:
    // The one application, which applyRenderInputs and loadRenderInputs run.
    void applyRenderInputs(const ALRenderInputRequests::Application& taken);

    // Reads the render inputs from the settings, once for everything an application applies: the programs a reload
    // builds, the resources' plan and the passes, which read the latched statics set here.
    void latchRenderInputs();

    // Makes the render resources what @a requested describes (reconcileResources). State derived from them, or from
    // the programs where @a cause says they were reloaded, is dropped only where what it derives from changed, and one
    // log line names both. Whether the render size was built in full rather than halved for want of memory.
    bool rebuildGLBuffers(const ALRenderResourceInputs& requested, RebuildCause cause);
    // What the programs' reload alone invalidates, where no reconcile follows it, and its log line.
    void programsReloaded();
    // Whether the BRDF LUT was drawn with a gDeferredGenBrdfLutProgram that has since linked again.
    bool brdfLutStale() const;

    // What the settings say the programs' and the resources' inputs are now, read by latchRenderInputs alone, so every
    // program, resource and pass is planned from the same reading of the settings. The resources take the G-buffer's
    // normal layout and the depth convention from the programs' inputs, which compile for them.
    static ALShaderInputs         shaderInputsFromSettings();
    static ALRenderResourceInputs resourceInputsFromSettings(const ALShaderInputs& shaders);

    // The render target, or the GL texture name, that each of the plan's resources is.
    LLRenderTarget* resourceTarget(ALRenderResourcePlan::Id id);
    U32*            resourceTexture(ALRenderResourcePlan::Id id);

    // Builds, keeps or frees each of @a ids as @a plan says, depth owners first, and records which in @a changes. The
    // transient resources are the pool's, which prewarmTransients readies.
    void ensureResources(const ALRenderResourcePlan& plan, const ALRenderResourcePlan::IdSet& ids,
                         ResourceChanges& changes);
    // Readies the pool for @a plan's transient resources among @a ids that it prewarms (isPrewarmed): everything it
    // holds dropped where the main size changed, and what shares a depth built again since, and one of each
    // description built where it holds none, so that a description that cannot be built drives the render size down
    // as a persistent target's does and the next frame finds them. Records which in @a changes.
    void prewarmTransients(const ALRenderResourcePlan& plan, const ALRenderResourcePlan::IdSet& ids,
                           ResourceChanges& changes);
    // One of the pipeline's own GL textures, with its contents.
    void createResourceTexture(ALRenderResourcePlan::Id id);
    // Every resource the plan names, and what the pipeline keeps about them.
    void releaseRenderTargets();

    // What is built against the main depth format and rebuilt where it is next used: the depth pyramid, the water
    // surface's maps, the selection outline and the far-plane overlay.
    void releaseDepthDependents();

    // What the resources were last reconciled to, and whether they have been since releaseAllGLResources last ran.
    ALRenderResourceInputs mResourceInputs;
    ALRenderResourcePlan   mResourcePlan;
    bool                   mGLResourcesLive = false;
    // What the settings asked of that reconcile: mResourceInputs at a lower size where it was halved for want of
    // memory. A change is applied only where the settings move from this, so a halved size is not retried in full for
    // every change that leaves the inputs as they were.
    ALRenderResourceInputs mRequestedInputs;
    // The inputs as the last latch read them, which the next reconcile and reload are asked for.
    ALRenderResourceInputs mLatchedInputs;
    ALShaderInputs         mLatchedShaderInputs;
    // Whether the last reconcile built the render size in full, which a snapshot at the size it was built for finds.
    bool                   mResourcesFullSize = true;
    // The link of gDeferredGenBrdfLutProgram (LLGLSLShader::linkCount) the BRDF LUT was last drawn with.
    U32                    mBrdfLutLinks = 0;

    // The changes marked since the last application, and how many the one running applies, for the log line.
    ALRenderInputRequests mRenderInputRequests;
    U32                   mApplyingChanges = 0;
public:

    // rebuild all LLVOVolume render batches
    void rebuildDrawInfo();
    // Rebuild all terrain
    void rebuildTerrain();

    // Clear LLFace mVertexBuffer pointers
    void resetVertexBuffers(LLDrawable* drawable);

    // perform a profile of the given avatar
    // if profile_attachments is true, run a profile for each attachment
    void profileAvatar(LLVOAvatar* avatar, bool profile_attachments = false);
    // Gives each attachment a profile timed its GPU time once its frame has been read, waiting for nothing. Once a
    // frame, after the pool retires.
    void readAttachmentTimes();

    // generate an impostor for the given avatar
    //  preview_avatar - if true, a preview window render is being performed
    //  for_profile - if true, a profile is being performed, do not update actual impostor
    //  specific_attachment - specific attachment to profile, or nullptr to profile entire avatar
    void generateImpostor(LLVOAvatar* avatar, bool preview_avatar = false, bool for_profile = false, LLViewerObject* specific_attachment = nullptr);

    // A full-screen quad at the far plane, depth-tested against the bound
    // target's depth and never writing it: GL_GREATER reaches every texel that
    // geometry covers, GL_LEQUAL every texel that nothing does. Drawn with
    // gDebugProgram in `color`, or in the current diffuse colour where it is
    // null. Blend and colour mask are the caller's to set.
    void drawFarPlaneQuad(U32 depth_func, const LLColor4* color = nullptr);

    // A cutout frame's coverage, laid into the scene target's alpha once the
    // opaque forward pools have drawn and before the blended ones, as an
    // impostor bake lays its own: nothing over the background, colour and
    // alpha both, and full coverage wherever opaque geometry sits.
    void seedCutoutCoverageFromDepth();
    // After the frame is finished: the scene target's alpha -- the cutout's
    // coverage -- into the alpha of whatever is bound, colour untouched. The
    // post chain carries no alpha a cutout could use (glow tags, FXAA luma,
    // 2-bit ping-pong buffers), so it comes from the scene target directly.
    void copyCutoutCoverageToAlpha();
    // The screen-space reflection source emptied: no colour, depth at the far
    // plane, so a march through it finds nothing.
    void clearSceneMap();

    void bindScreenToTexture();
    void renderFinalize();
    void copyScreenSpaceReflections(LLRenderTarget* src, LLRenderTarget* dst);
    // The current pack's scene depth (mRT->deferredScreen's, which mRT->screen shares) over the whole of `dst`'s,
    // stretched as a nearest blit would, through gCopyDepthProgram: the water's depth copy and mSceneMap keep depth
    // without the screen's stencil, which a blit's matching formats would make them carry.
    void copySceneDepth(LLRenderTarget& dst);
    void generateLuminance(LLRenderTarget* src, LLRenderTarget* dst);
    void generateExposure(LLRenderTarget* src, LLRenderTarget* dst, bool use_history = true);
    void generateLensFlareState(LLRenderTarget* src);
    void clearLensFlareState();
    // What generateBloomHDR makes for colorCorrect to composite, each held by the scope it was given until that ends.
    struct Bloom
    {
        // The HDR pyramid (RGB = bloom, A = halation intensity): mMips[0] the
        // extract at its base, each level after it half the one before, and
        // mMips[0] the bloom once the upsample is done. None where none was made.
        std::array<LLRenderTarget*, ALRenderResourcePlan::BLOOM_MAX_MIPS> mMips{};
        U32                                                         mMipCount = 0;
        // The cross-screen filter's arms summed, where they were drawn this frame.
        LLRenderTarget* mStreaks = nullptr;
    };
    void colorCorrect(LLRenderTarget* src, LLRenderTarget* dst, bool tonemap, bool colorgrade,
                      const Bloom* bloom = nullptr);
    // The legacy glow extracted from @a src and blurred, held by @a scope until combineGlow has added it; null where
    // there is none to draw, glow being off or its blur running no iterations, or none could be had.
    LLRenderTarget* generateGlow(LLRenderTarget* src, ALTransientTargets& scope);
    // Whether generateBloomHDR can run: the pyramid is deep enough and every
    // shader it needs built. It makes none at zero bloom and halation strength.
    bool bloomHDRReady() const;
    Bloom generateBloomHDR(LLRenderTarget* src, ALTransientTargets& scope);
    void compositeBloomHDR(LLRenderTarget* scene, const Bloom& bloom);
    // The post chain's passes after colorCorrect: each draws @a src into @a dst and returns true, or draws nothing and
    // returns false where it has nothing to draw with, so the caller goes on reading @a src.
    bool applyCAS(LLRenderTarget* src, LLRenderTarget* dst);
    bool applyFXAA(LLRenderTarget* src, LLRenderTarget* dst);
    // SMAA's edges and blend weights for one frame, held by the scope generateSMAABuffers was given; neither where it
    // drew none.
    struct SMAATargets
    {
        LLRenderTarget* mEdges = nullptr;
        LLRenderTarget* mBlend = nullptr;
    };
    SMAATargets generateSMAABuffers(LLRenderTarget* src, ALTransientTargets& scope);
    bool applySMAA(LLRenderTarget* src, LLRenderTarget* dst, const SMAATargets& smaa);
    // Depth of field from mRT->screen into a target of the screen's description
    // from @a scope, which the post chain reads in the screen's place; null where
    // it does not run. The screen keeps the sharp scene.
    LLRenderTarget* renderDoF(ALTransientTargets& scope);
    // The description depth of field's blur was last taken at, freed from the pool when its scale moves on.
    std::optional<ALRenderTargetDesc> mDoFBlurDesc;
    void combineGlow(LLRenderTarget* src, LLRenderTarget* dst, LLRenderTarget* glow);
    void visualizeBuffers(LLRenderTarget* src, LLRenderTarget* dst, U32 bufferIndex);

    /// Take a point-sampled copy of the presented frame for the scopes floater
    /// and read it back asynchronously. Does nothing unless sScopeCapture is
    /// set and the sample interval has elapsed. See alfloaterscopes.h.
    void captureScopeSample(LLRenderTarget* src);
    /// Service a pending grab, and drop a still whose resolution no longer
    /// matches the frame. Called from the same place as captureScopeSample.
    void captureReferenceStill(LLRenderTarget* src);
    const ALScopeData& getScopeData() const { return mScopeData; }

    /// Where a point in *scaled* window coordinates -- which is what
    /// LLViewerWindow::getCurrentMouse and the tool mouse handlers hand you --
    /// falls inside the world view, as 0..1 across and up. False when it is
    /// outside the view.
    ///
    /// Everything stays in scaled space, which is why this is the only place
    /// that should do it: measuring a scaled point against the world view's
    /// *raw* rect mixes two scalings and leaves an error proportional to the
    /// distance from the origin -- dead on in one corner, adrift in the far
    /// one, and invisible on a machine where the two spaces happen to match.
    static bool worldViewUV(S32 scaled_x, S32 scaled_y, F32& u, F32& v);

    /// Colour of the last sampled frame under a point in *scaled* window
    /// coordinates, false if nothing has been sampled there.
    ///
    /// Costs nothing: it reads the copy captureScopeSample already took, so
    /// there is no second readback and no stall. The price is that it is only
    /// as fresh and as fine as that sample -- one sample interval old, and one
    /// cell of a point decimation of the view, which for "what level is this"
    /// is what you want anyway.
    bool getScopePixel(S32 scaled_x, S32 scaled_y, LLColor4U& out) const;
    void releaseScopeBuffers();

    /// @name Reference still
    /// Resolve's still store, in miniature: freeze the frame, then wipe the
    /// live image against it. This is how a colourist judges a change that
    /// took longer to make than hold-to-compare can span -- that one only ever
    /// shows you *no* grade, whereas this shows you the grade you had.
    ///
    /// The still is taken from the same point the scopes sample, so the two
    /// agree about what a frame is: after every post pass, before the print
    /// effects the final blit adds. Those are then applied to both sides of
    /// the seam, which is what makes the comparison about the grade rather
    /// than about the vignette.
    /// @{

    /// Grab the next presented frame. Honoured on that frame's blit, so the
    /// still is of what the user is looking at now, not of a frame already
    /// gone.
    void requestReferenceStill() { mReferenceStillWanted = true; }
    void clearReferenceStill();
    bool hasReferenceStill() const { return mReferenceStill.getWidth() > 0; }
    /// @}

    /// Ask for the linear scene colour under a screen pixel.
    ///
    /// @a x and @a y are scaled window coordinates, as a mouse handler
    /// receives them. The answer arrives on the next rendered frame, read out
    /// of the scene buffer before white balance, grading or tonemapping have
    /// touched it -- so it describes the light in the scene rather than the
    /// look currently laid over it, which is what a tool deciding on the look
    /// needs. Colour components are linear radiance and may exceed 1.
    ///
    /// One request is held at a time; asking again replaces it. The callback
    /// does not fire if the click was outside the 3D view, or under a graphics
    /// debugger that cannot tolerate a read back.
    typedef std::function<void(const LLColor3&)> scene_pixel_cb_t;
    void requestScenePixel(S32 x, S32 y, scene_pixel_cb_t callback);

    void init();
    void cleanup();
    bool isInit() { return mInitialized; };

    /// @brief Get a draw pool from pool type (POOL_SIMPLE, POOL_MEDIA) and texture.
    /// @return Draw pool, or NULL if not found.
    LLDrawPool *findPool(const U32 pool_type, LLViewerTexture *tex0 = NULL);

    /// @brief Get a draw pool for faces of the appropriate type and texture.  Create if necessary.
    /// @return Always returns a draw pool.
    LLDrawPool *getPool(const U32 pool_type, LLViewerTexture *tex0 = NULL);

    /// @brief Figures out draw pool type from texture entry. Creates pool if necessary.
    static LLDrawPool* getPoolFromTE(const LLTextureEntry* te, LLViewerTexture* te_image);
    static U32 getPoolTypeFromTE(const LLTextureEntry* te, LLViewerTexture* imagep);

    void         addPool(LLDrawPool *poolp);    // Only to be used by LLDrawPool classes for splitting pools!
    void         removePool( LLDrawPool* poolp );

    void         allocDrawable(LLViewerObject *obj);

    void         unlinkDrawable(LLDrawable*);

    static void removeMutedAVsLights(LLVOAvatar*);

    // Object related methods
    void        markVisible(LLDrawable *drawablep, LLCamera& camera);
    void        markOccluder(LLSpatialGroup* group);

    void        doOcclusion(LLCamera& camera);
    void        markNotCulled(LLSpatialGroup* group, LLCamera &camera);
    void        markMoved(LLDrawable *drawablep, bool damped_motion = false);
    void        markShift(LLDrawable *drawablep);
    void        markTextured(LLDrawable *drawablep);
    void        markGLRebuild(LLGLUpdate* glu);
    void        markRebuild(LLSpatialGroup* group);
    void        markRebuild(LLDrawable *drawablep, LLDrawable::EDrawableFlags flag = LLDrawable::REBUILD_ALL);
    void        markPartitionMove(LLDrawable* drawablep);
    void        markMeshDirty(LLSpatialGroup* group);

    //get the object between start and end that's closest to start.
    LLViewerObject* lineSegmentIntersectInWorld(const LLVector4a& start, const LLVector4a& end,
                                                bool pick_transparent,
                                                bool pick_rigged,
                                                bool pick_unselectable,
                                                bool pick_reflection_probe,
                                                S32* face_hit,                          // return the face hit
                                                LLVector4a* intersection = NULL,         // return the intersection point
                                                LLVector2* tex_coord = NULL,            // return the texture coordinates of the intersection point
                                                LLVector4a* normal = NULL,               // return the surface normal at the intersection point
                                                LLVector4a* tangent = NULL             // return the surface tangent at the intersection point
        );

    //get the closest particle to start between start and end, returns the LLVOPartGroup and particle index
    LLVOPartGroup* lineSegmentIntersectParticle(const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection,
                                                        S32* face_hit);


    LLViewerObject* lineSegmentIntersectInHUD(const LLVector4a& start, const LLVector4a& end,
                                              bool pick_transparent,
                                              S32* face_hit,                          // return the face hit
                                              LLVector4a* intersection = NULL,         // return the intersection point
                                              LLVector2* tex_coord = NULL,            // return the texture coordinates of the intersection point
                                              LLVector4a* normal = NULL,               // return the surface normal at the intersection point
                                              LLVector4a* tangent = NULL             // return the surface tangent at the intersection point
        );

    // Something about these textures has changed.  Dirty them.
    void        dirtyPoolObjectTextures(const std::set<LLViewerFetchedTexture*>& textures);

    void        resetDrawOrders();

    U32         addObject(LLViewerObject *obj);

    void        enableShadows(const bool enable_shadows);

    bool        shadersLoaded();
    bool        canUseWindLightShaders() const;
    bool        canUseAntiAliasing() const;

    // phases
    void resetFrameStats();

    void updateMoveDampedAsync(LLDrawable* drawablep);
    void updateMoveNormalAsync(LLDrawable* drawablep);
    void updateMovedList(LLDrawable::drawable_vector_t& move_list);
    void updateMove();
    bool visibleObjectsInFrustum(LLCamera& camera);
    bool getVisibleExtents(LLCamera& camera, LLVector3 &min, LLVector3& max);
    bool getVisiblePointCloud(LLCamera& camera, LLVector3 &min, LLVector3& max, std::vector<LLVector3>& fp, LLVector3 light_dir = LLVector3(0,0,0));

    // Populate given LLCullResult with results of a frustum cull of the entire scene against the given LLCamera
    void updateCull(LLCamera& camera, LLCullResult& result);
    void createObjects(F32 max_dtime);
    void createObject(LLViewerObject* vobj);
    void processPartitionQ();
    void updateGeom(F32 max_dtime);
    void updateGL();
    void rebuildPriorityGroups();
    void rebuildGroups();
    void clearRebuildGroups();
    void clearRebuildDrawables();

    //calculate pixel area of given box from vantage point of given camera
    static F32 calcPixelArea(LLVector3 center, LLVector3 size, LLCamera& camera);
    static F32 calcPixelArea(const LLVector4a& center, const LLVector4a& size, LLCamera &camera);

    void stateSort(LLCamera& camera, LLCullResult& result);
    void stateSort(LLSpatialGroup* group, LLCamera& camera);
    void stateSort(LLSpatialBridge* bridge, LLCamera& camera, bool fov_changed = false);
    void stateSort(LLDrawable* drawablep, LLCamera& camera);
    void postSort(LLCamera& camera);

    void forAllVisibleDrawables(void (*func)(LLDrawable*));

    void renderObjects(U32 type, bool texture = true, bool batch_texture = false, bool rigged = false);
    void renderGLTFObjects(U32 type, bool texture = true, bool rigged = false);

    void renderAlphaObjects(bool rigged = false);
    void renderMaskedObjects(U32 type, bool texture = true, bool batch_texture = false, bool rigged = false);
    void renderFullbrightMaskedObjects(U32 type, bool texture = true, bool batch_texture = false, bool rigged = false);

    void renderGroups(LLRenderPass* pass, U32 type, bool texture);
    void renderRiggedGroups(LLRenderPass* pass, U32 type, bool texture);

    void grabReferences(LLCullResult& result);
    void clearReferences();

    //check references will assert that there are no references in sCullResult to the provided data
    void checkReferences(LLFace* face);
    void checkReferences(LLDrawable* drawable);
    void checkReferences(LLDrawInfo* draw_info);
    void checkReferences(LLSpatialGroup* group);

    void renderGeomDeferred(LLCamera& camera, bool do_occlusion = false);
    void renderGeomPostDeferred(LLCamera& camera);
    void renderGeomShadow(LLCamera& camera);
    void bindBrdfLut(LLGLSLShader& shader);

    // bind shadow maps
    // if setup is true, wil lset texture compare mode function and filtering options
    void bindShadowMaps(LLGLSLShader& shader);

    // Unbind the shadow maps from whichever units bindShadowMaps last put them on. Detaches
    // only -- the render targets themselves are owned elsewhere and outlive this (contrast
    // releaseShadowBuffers, which frees them).
    //
    // Programs do not agree on where DEFERRED_SHADOW0..5 live -- one deferred shader may map
    // them to units 6-11 and another to 26-31 -- so unbinding "this shader's" shadow channels
    // leaves the previous layout's units still holding depth textures under the compare
    // sampler. The next program to use those low units for ordinary material maps then reads
    // a depth texture through a non-shadow sampler, which is undefined behaviour.
    void unbindShadowMaps();

    // Rebuild the shared shadow/SSAO constant block from current state (CPU only). Called once
    // per deferred pass; every value in it is fixed for the whole pass.
    void packDeferredUBO();
    // Upload (if dirty) and bind that block at UB_DEFERRED.
    void bindDeferredUBO();

    void bindDeferredShaderFast(LLGLSLShader& shader);
    void bindDeferredShader(LLGLSLShader& shader, LLRenderTarget* light_target = nullptr, LLRenderTarget* depth_target = nullptr);
    void setupSpotLight(LLGLSLShader& shader, LLDrawable* drawablep);

    void unbindDeferredShader(LLGLSLShader& shader);

    // set env_mat parameter in given shader
    void setEnvMat(LLGLSLShader& shader);

    void bindReflectionProbes(LLGLSLShader& shader);
    void unbindReflectionProbes(LLGLSLShader& shader);

    void renderDeferredLighting();

    // apply atmospheric haze based on contents of color and depth buffer
    // should be called just before rendering water when camera is under water
    // and just before rendering alpha when camera is above water
    void doAtmospherics();

    // Whether this frame's haze pass could converge anything on the sky behind (mSkyBehind): the main view's, with
    // atmospherics on, the camera above the water, and water among what was sorted to draw.
    bool skyBehindWanted() const;

    // apply water haze based on contents of color and depth buffer
    // should be called just before rendering pre-water alpha objects
    void doWaterHaze();

    // Mark where the PASS_INVISIBLE prims remove the water in the bound screen target's stencil
    // (ALWaterStencil::EXCLUDE_BIT), against the scene's depth, before the water draws.
    void doWaterExclusion();

    void postDeferredGammaCorrect(LLRenderTarget* screen_target);

    void generateSunShadow(LLCamera& camera);
    // The pack being rendered through (mRT).
    ALRenderResourcePlan::Pack renderPack() const;
    // The sun cascades the pack being rendered through renders and has maps for (ALRenderResourcePlan::sunCascades).
    U32 sunCascades() const;
    // The shadow maps the pass being rendered has none of, a bit each: sun cascades 0 to 3, then the spot pair. A cascade
    // past those its pack renders, or a map that could not be built, which reconcileResources leaves unbuilt: no pass
    // draws into it, and the shader reads its shadows lit (unbuilt_shadow_maps).
    U32 unbuiltShadowMaps() const;
    LLRenderTarget* getSunShadowTarget(U32 i);
    LLRenderTarget* getSpotShadowTarget(U32 i);

    void renderHighlight(const LLViewerObject* obj, F32 fade);

    // `occlusion` is the sUseOcclusion level the pass culls with: 0 for none, 2 to ask the shadow camera's own
    // occlusion queries and draw them after the opaque casters.
    void renderShadow(const LLMatrix4a& view, const LLMatrix4a& proj, LLCamera& camera, LLCullResult& result, bool depth_clamp, bool do_cull = true, S32 occlusion = 0);
    void renderSelectedFaces(const LLColor4& color);
    void renderHighlights();
    void renderDebug();
    void renderPhysicsDisplay();

    void rebuildPools(); // Rebuild pools

    void findReferences(LLDrawable *drawablep); // Find the lists which have references to this object
    bool verify();                      // Verify that all data in the pipeline is "correct"

    S32  getLightCount() const { return static_cast<S32>(mLights.size()); }

    void calcNearbyLights(LLCamera& camera);
    void setupHWLights();
    void setupAvatarLights(bool for_edit = false);
    void enableLights(U32 mask);
    void enableLightsDynamic();
    void enableLightsAvatar();
    void enableLightsPreview();
    void enableLightsAvatarEdit(const LLColor4& color);
    void enableLightsFullbright();
    void disableLights();

    void shiftObjects(const LLVector3 &offset);

    void setLight(LLDrawable *drawablep, bool is_light);

    bool hasRenderBatches(const U32 type) const;
    LLCullResult::drawinfo_iterator beginRenderMap(U32 type);
    LLCullResult::drawinfo_iterator endRenderMap(U32 type);
    LLCullResult::sg_iterator beginAlphaGroups();
    LLCullResult::sg_iterator endAlphaGroups();
    LLCullResult::sg_iterator beginRiggedAlphaGroups();
    LLCullResult::sg_iterator endRiggedAlphaGroups();

    void addTrianglesDrawn(S32 index_count);
    void recordTrianglesDrawn();

    bool hasRenderDebugFeatureMask(const U32 mask) const    { return bool(mRenderDebugFeatureMask & mask); }
    bool hasRenderDebugMask(const U64 mask) const           { return bool(mRenderDebugMask & mask); }
    void setAllRenderDebugFeatures() { mRenderDebugFeatureMask = 0xffffffff; }
    void clearAllRenderDebugFeatures() { mRenderDebugFeatureMask = 0x0; }
    void setAllRenderDebugDisplays() { mRenderDebugMask = 0xffffffffffffffff; }
    void clearAllRenderDebugDisplays() { mRenderDebugMask = 0x0; }

    bool hasRenderType(const U32 type) const;
    bool hasAnyRenderType(const U32 type, ...) const;

    static bool isWaterClip();

    // Depth format for the main scene / non-shadow depth targets: float32 under reverse-Z
    // (same 4 bytes/px as DEPTH24 on desktop GPUs), fixed 24-bit otherwise.
    static LLRenderTarget::eDepthFormat mainDepthFormat();

    // Latch reverse-Z on/off as the shader inputs say (the setting where the context has clip control): flips
    // LLRender::sReverseZ, sets glClipControl + clear depth, re-bases the ambient depth func, and clears the sampler
    // cache. Idempotent; requires a current GL context. Called from setShaders() so the whole convention flip (clip
    // control, clear depth, RT formats, shader define) is atomic.
    static void updateReverseZ(bool reverse_z);

    /// RenderHDREnabled as the inputs were last latched: what every HDR-dependent target and pass decides by. The
    /// G-buffer's normal layout follows LLRender::sGBufferNormHDR, which setShaders latches from the same inputs.
    static bool hdrEnabled();

    void setRenderTypeMask(U32 type, ...);
    // This is equivalent to 'setRenderTypeMask'
    //void orRenderTypeMask(U32 type, ...);
    void andRenderTypeMask(U32 type, ...);
    void clearRenderTypeMask(U32 type, ...);
    void setAllRenderTypes();
    void clearAllRenderTypes();

    void pushRenderTypeMask();
    void popRenderTypeMask();

    void pushRenderDebugFeatureMask();
    void popRenderDebugFeatureMask();

    static void toggleRenderType(U32 type);

    // For UI control of render features
    static bool hasRenderTypeControl(U32 data);
    static void toggleRenderDebug(U64 data);
    static void toggleRenderDebugFeature(U32 data);
    static void toggleRenderTypeControl(U32 data);
    static bool toggleRenderTypeControlNegated(S32 data);
    static bool toggleRenderDebugControl(U64 data);
    static bool toggleRenderDebugFeatureControl(U32 data);
    static void setRenderDebugFeatureControl(U32 bit, bool value);

    static void setRenderParticleBeacons(bool val);
    static void toggleRenderParticleBeacons();
    static bool getRenderParticleBeacons();

    static void setRenderSoundBeacons(bool val);
    static void toggleRenderSoundBeacons();
    static bool getRenderSoundBeacons();

    static void setRenderMOAPBeacons(bool val);
    static void toggleRenderMOAPBeacons();
    static bool getRenderMOAPBeacons();

    static void setRenderPhysicalBeacons(bool val);
    static void toggleRenderPhysicalBeacons();
    static bool getRenderPhysicalBeacons();

    static void setRenderScriptedBeacons(bool val);
    static void toggleRenderScriptedBeacons();
    static bool getRenderScriptedBeacons();

    static void setRenderScriptedTouchBeacons(bool val);
    static void toggleRenderScriptedTouchBeacons();
    static bool getRenderScriptedTouchBeacons();

    static void setRenderBeacons(bool val);
    static void toggleRenderBeacons();
    static bool getRenderBeacons();

    static void setRenderHighlights(bool val);
    static void toggleRenderHighlights();
    static bool getRenderHighlights();
    static void setRenderHighlightTextureChannel(LLRender::eTexIndex channel); // sets which UV setup to display in highlight overlay

    static void refreshCachedSettings();

    void addDebugBlip(const LLVector3& position, const LLColor4& color);

    void hidePermanentObjects( std::vector<U32>& restoreList );
    void restorePermanentObjects( const std::vector<U32>& restoreList );
    void skipRenderingOfTerrain( bool flag );
    void hideObject( const LLUUID& id );
    void restoreHiddenObject( const LLUUID& id );

    LLReflectionMapManager mReflectionMapManager;
    LLHeroProbeManager mHeroProbeManager;

private:
    void unloadShaders();
    void addToQuickLookup( LLDrawPool* new_poolp );
    void removeFromQuickLookup( LLDrawPool* poolp );
    bool updateDrawableGeom(LLDrawable* drawable);
    void assertInitializedDoError();
    bool assertInitialized() { const bool is_init = isInit(); if (!is_init) assertInitializedDoError(); return is_init; };
    void connectRefreshCachedSettingsSafe(const std::string name);
    void hideDrawable( LLDrawable *pDrawable );
    void unhideDrawable( LLDrawable *pDrawable );
    void skipRenderingShadows();
public:
    enum {GPU_CLASS_MAX = 3 };

    enum LLRenderTypeMask
    {
        // Following are pool types (some are also object types)
        RENDER_TYPE_SKY                         = LLDrawPool::POOL_SKY,
        RENDER_TYPE_WL_SKY                      = LLDrawPool::POOL_WL_SKY,
        RENDER_TYPE_TERRAIN                     = LLDrawPool::POOL_TERRAIN,
        RENDER_TYPE_SIMPLE                      = LLDrawPool::POOL_SIMPLE,
        RENDER_TYPE_GRASS                       = LLDrawPool::POOL_GRASS,
        RENDER_TYPE_ALPHA_MASK                  = LLDrawPool::POOL_ALPHA_MASK,
        RENDER_TYPE_FULLBRIGHT_ALPHA_MASK       = LLDrawPool::POOL_FULLBRIGHT_ALPHA_MASK,
        RENDER_TYPE_FULLBRIGHT                  = LLDrawPool::POOL_FULLBRIGHT,
        RENDER_TYPE_BUMP                        = LLDrawPool::POOL_BUMP,
        RENDER_TYPE_MATERIALS                   = LLDrawPool::POOL_MATERIALS,
        RENDER_TYPE_AVATAR                      = LLDrawPool::POOL_AVATAR,
        RENDER_TYPE_CONTROL_AV                  = LLDrawPool::POOL_CONTROL_AV, // Animesh
        RENDER_TYPE_TREE                        = LLDrawPool::POOL_TREE,
        RENDER_TYPE_WATEREXCLUSION              = LLDrawPool::POOL_WATEREXCLUSION,
        RENDER_TYPE_VOIDWATER                   = LLDrawPool::POOL_VOIDWATER,
        RENDER_TYPE_WATER                       = LLDrawPool::POOL_WATER,
        RENDER_TYPE_GLTF_PBR                    = LLDrawPool::POOL_GLTF_PBR,
        RENDER_TYPE_GLTF_PBR_ALPHA_MASK         = LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK,
        RENDER_TYPE_ALPHA                       = LLDrawPool::POOL_ALPHA,
        RENDER_TYPE_ALPHA_PRE_WATER             = LLDrawPool::POOL_ALPHA_PRE_WATER,
        RENDER_TYPE_ALPHA_POST_WATER            = LLDrawPool::POOL_ALPHA_POST_WATER,
        RENDER_TYPE_GLOW                        = LLDrawPool::POOL_GLOW,
        RENDER_TYPE_PASS_SIMPLE                 = LLRenderPass::PASS_SIMPLE,
        RENDER_TYPE_PASS_SIMPLE_RIGGED = LLRenderPass::PASS_SIMPLE_RIGGED,
        RENDER_TYPE_PASS_GRASS                  = LLRenderPass::PASS_GRASS,
        RENDER_TYPE_PASS_FULLBRIGHT             = LLRenderPass::PASS_FULLBRIGHT,
        RENDER_TYPE_PASS_FULLBRIGHT_RIGGED = LLRenderPass::PASS_FULLBRIGHT_RIGGED,
        RENDER_TYPE_PASS_INVISIBLE              = LLRenderPass::PASS_INVISIBLE,
        RENDER_TYPE_PASS_INVISIBLE_RIGGED = LLRenderPass::PASS_INVISIBLE_RIGGED,
        RENDER_TYPE_PASS_INVISI_SHINY           = LLRenderPass::PASS_INVISI_SHINY,
        RENDER_TYPE_PASS_INVISI_SHINY_RIGGED = LLRenderPass::PASS_INVISI_SHINY_RIGGED,
        RENDER_TYPE_PASS_FULLBRIGHT_SHINY       = LLRenderPass::PASS_FULLBRIGHT_SHINY,
        RENDER_TYPE_PASS_FULLBRIGHT_SHINY_RIGGED = LLRenderPass::PASS_FULLBRIGHT_SHINY_RIGGED,
        RENDER_TYPE_PASS_SHINY                  = LLRenderPass::PASS_SHINY,
        RENDER_TYPE_PASS_SHINY_RIGGED = LLRenderPass::PASS_SHINY_RIGGED,
        RENDER_TYPE_PASS_BUMP                   = LLRenderPass::PASS_BUMP,
        RENDER_TYPE_PASS_BUMP_RIGGED = LLRenderPass::PASS_BUMP_RIGGED,
        RENDER_TYPE_PASS_POST_BUMP              = LLRenderPass::PASS_POST_BUMP,
        RENDER_TYPE_PASS_POST_BUMP_RIGGED = LLRenderPass::PASS_POST_BUMP_RIGGED,
        RENDER_TYPE_PASS_GLOW                   = LLRenderPass::PASS_GLOW,
        RENDER_TYPE_PASS_GLOW_RIGGED = LLRenderPass::PASS_GLOW_RIGGED,
        RENDER_TYPE_PASS_GLTF_GLOW = LLRenderPass::PASS_GLTF_GLOW,
        RENDER_TYPE_PASS_GLTF_GLOW_RIGGED = LLRenderPass::PASS_GLTF_GLOW_RIGGED,
        RENDER_TYPE_PASS_ALPHA                  = LLRenderPass::PASS_ALPHA,
        RENDER_TYPE_PASS_ALPHA_MASK             = LLRenderPass::PASS_ALPHA_MASK,
        RENDER_TYPE_PASS_ALPHA_MASK_RIGGED = LLRenderPass::PASS_ALPHA_MASK_RIGGED,
        RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK  = LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK,
        RENDER_TYPE_PASS_FULLBRIGHT_ALPHA_MASK_RIGGED = LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK_RIGGED,
        RENDER_TYPE_PASS_MATERIAL               = LLRenderPass::PASS_MATERIAL,
        RENDER_TYPE_PASS_MATERIAL_RIGGED = LLRenderPass::PASS_MATERIAL_RIGGED,
        RENDER_TYPE_PASS_MATERIAL_ALPHA         = LLRenderPass::PASS_MATERIAL_ALPHA,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_RIGGED = LLRenderPass::PASS_MATERIAL_ALPHA_RIGGED,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK    = LLRenderPass::PASS_MATERIAL_ALPHA_MASK,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_MASK_RIGGED = LLRenderPass::PASS_MATERIAL_ALPHA_MASK_RIGGED,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE= LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE,
        RENDER_TYPE_PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED = LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED,
        RENDER_TYPE_PASS_SPECMAP                = LLRenderPass::PASS_SPECMAP,
        RENDER_TYPE_PASS_SPECMAP_RIGGED = LLRenderPass::PASS_SPECMAP_RIGGED,
        RENDER_TYPE_PASS_SPECMAP_BLEND          = LLRenderPass::PASS_SPECMAP_BLEND,
        RENDER_TYPE_PASS_SPECMAP_BLEND_RIGGED = LLRenderPass::PASS_SPECMAP_BLEND_RIGGED,
        RENDER_TYPE_PASS_SPECMAP_MASK           = LLRenderPass::PASS_SPECMAP_MASK,
        RENDER_TYPE_PASS_SPECMAP_MASK_RIGGED = LLRenderPass::PASS_SPECMAP_MASK_RIGGED,
        RENDER_TYPE_PASS_SPECMAP_EMISSIVE       = LLRenderPass::PASS_SPECMAP_EMISSIVE,
        RENDER_TYPE_PASS_SPECMAP_EMISSIVE_RIGGED = LLRenderPass::PASS_SPECMAP_EMISSIVE_RIGGED,
        RENDER_TYPE_PASS_NORMMAP                = LLRenderPass::PASS_NORMMAP,
        RENDER_TYPE_PASS_NORMMAP_RIGGED = LLRenderPass::PASS_NORMMAP_RIGGED,
        RENDER_TYPE_PASS_NORMMAP_BLEND          = LLRenderPass::PASS_NORMMAP_BLEND,
        RENDER_TYPE_PASS_NORMMAP_BLEND_RIGGED = LLRenderPass::PASS_NORMMAP_BLEND_RIGGED,
        RENDER_TYPE_PASS_NORMMAP_MASK           = LLRenderPass::PASS_NORMMAP_MASK,
        RENDER_TYPE_PASS_NORMMAP_MASK_RIGGED = LLRenderPass::PASS_NORMMAP_MASK_RIGGED,
        RENDER_TYPE_PASS_NORMMAP_EMISSIVE       = LLRenderPass::PASS_NORMMAP_EMISSIVE,
        RENDER_TYPE_PASS_NORMMAP_EMISSIVE_RIGGED = LLRenderPass::PASS_NORMMAP_EMISSIVE_RIGGED,
        RENDER_TYPE_PASS_NORMSPEC               = LLRenderPass::PASS_NORMSPEC,
        RENDER_TYPE_PASS_NORMSPEC_RIGGED = LLRenderPass::PASS_NORMSPEC_RIGGED,
        RENDER_TYPE_PASS_NORMSPEC_BLEND         = LLRenderPass::PASS_NORMSPEC_BLEND,
        RENDER_TYPE_PASS_NORMSPEC_BLEND_RIGGED = LLRenderPass::PASS_NORMSPEC_BLEND_RIGGED,
        RENDER_TYPE_PASS_NORMSPEC_MASK          = LLRenderPass::PASS_NORMSPEC_MASK,
        RENDER_TYPE_PASS_NORMSPEC_MASK_RIGGED = LLRenderPass::PASS_NORMSPEC_MASK_RIGGED,
        RENDER_TYPE_PASS_NORMSPEC_EMISSIVE      = LLRenderPass::PASS_NORMSPEC_EMISSIVE,
        RENDER_TYPE_PASS_NORMSPEC_EMISSIVE_RIGGED = LLRenderPass::PASS_NORMSPEC_EMISSIVE_RIGGED,
        RENDER_TYPE_PASS_GLTF_PBR                 = LLRenderPass::PASS_GLTF_PBR,
        RENDER_TYPE_PASS_GLTF_PBR_RIGGED         = LLRenderPass::PASS_GLTF_PBR_RIGGED,
        RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK        = LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK,
        RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK_RIGGED = LLRenderPass::PASS_GLTF_PBR_ALPHA_MASK_RIGGED,
        // Following are object types (only used in drawable mRenderType)
        RENDER_TYPE_HUD = LLRenderPass::NUM_RENDER_TYPES,
        RENDER_TYPE_VOLUME,
        RENDER_TYPE_PARTICLES,
        RENDER_TYPE_CLOUDS,
        RENDER_TYPE_HUD_PARTICLES,
        NUM_RENDER_TYPES,
        END_RENDER_TYPES = NUM_RENDER_TYPES
    };

    enum LLRenderDebugFeatureMask
    {
        RENDER_DEBUG_FEATURE_UI                 = 0x0001,
        RENDER_DEBUG_FEATURE_SELECTED           = 0x0002,
        RENDER_DEBUG_FEATURE_HIGHLIGHTED        = 0x0004,
        RENDER_DEBUG_FEATURE_DYNAMIC_TEXTURES   = 0x0008,
//      RENDER_DEBUG_FEATURE_HW_LIGHTING        = 0x0010,
        RENDER_DEBUG_FEATURE_FLEXIBLE           = 0x0010,
        RENDER_DEBUG_FEATURE_FOG                = 0x0020,
        RENDER_DEBUG_FEATURE_FR_INFO            = 0x0080,
        RENDER_DEBUG_FEATURE_FOOT_SHADOWS       = 0x0100,
    };

    enum LLRenderDebugMask: U64
    {
        RENDER_DEBUG_COMPOSITION        =  0x00000001,
        RENDER_DEBUG_VERIFY             =  0x00000002,
        RENDER_DEBUG_BBOXES             =  0x00000004,
        RENDER_DEBUG_OCTREE             =  0x00000008,
        RENDER_DEBUG_WIND_VECTORS       =  0x00000010,
        RENDER_DEBUG_OCCLUSION          =  0x00000020,
        RENDER_DEBUG_POINTS             =  0x00000040,
        RENDER_DEBUG_TEXTURE_PRIORITY   =  0x00000080,
        RENDER_DEBUG_TEXTURE_AREA       =  0x00000100,
        RENDER_DEBUG_FACE_AREA          =  0x00000200,
        RENDER_DEBUG_PARTICLES          =  0x00000400,
        RENDER_DEBUG_GLOW               =  0x00000800, // not used
        RENDER_DEBUG_TEXTURE_ANIM       =  0x00001000,
        RENDER_DEBUG_LIGHTS             =  0x00002000,
        RENDER_DEBUG_BATCH_SIZE         =  0x00004000,
        RENDER_DEBUG_ALPHA_BINS         =  0x00008000, // not used
        RENDER_DEBUG_RAYCAST            =  0x00010000,
        RENDER_DEBUG_AVATAR_DRAW_INFO   =  0x00020000,
        RENDER_DEBUG_SHADOW_FRUSTA      =  0x00040000,
        RENDER_DEBUG_SCULPTED           =  0x00080000,
        RENDER_DEBUG_AVATAR_VOLUME      =  0x00100000,
        RENDER_DEBUG_AVATAR_JOINTS      =  0x00200000,
        RENDER_DEBUG_AGENT_TARGET       =  0x00800000,
        RENDER_DEBUG_UPDATE_TYPE        =  0x01000000,
        RENDER_DEBUG_PHYSICS_SHAPES     =  0x02000000,
        RENDER_DEBUG_NORMALS            =  0x04000000,
        RENDER_DEBUG_LOD_INFO           =  0x08000000,
        RENDER_DEBUG_NODES              =  0x20000000,
        RENDER_DEBUG_TEXEL_DENSITY      =  0x40000000,
        RENDER_DEBUG_TRIANGLE_COUNT     =  0x80000000,
        RENDER_DEBUG_IMPOSTORS          = 0x100000000,
        RENDER_DEBUG_REFLECTION_PROBES  = 0x200000000,
        RENDER_DEBUG_PROBE_UPDATES      = 0x400000000,
    };

public:

    LLSpatialPartition* getSpatialPartition(LLViewerObject* vobj);

    void updateCamera(bool reset = false);

    LLVector3               mFlyCamPosition;
    LLQuaternion            mFlyCamRotation;

    bool                     mBackfaceCull;
    S32                      mMatrixOpCount;
    S32                      mTextureMatrixOps;
    S32                      mTextureMatrixOpsShadow;   // portion of mTextureMatrixOps issued during shadow passes
    S32                      mTextureMatrixOpsProbe;    // portion issued during reflection/hero probe updates
    S32                      mTextureMatrixOpsIdentity; // portion whose matrix was exactly identity (degenerate anims)
    // Classify and count one texture-matrix load; draw pools call this instead of
    // bumping mTextureMatrixOps directly so the debug display can attribute the total.
    void countTextureMatrixOp(const LLMatrix4& mat);
    S32                      mNumVisibleNodes;

    S32                      mDebugTextureUploadCost;
    S32                      mDebugSculptUploadCost;
    S32                      mDebugMeshUploadCost;

    S32                      mNumVisibleFaces;

    S32                     mPoissonOffset;

    static S32              sCompiles;

    static bool             sShowHUDAttachments;
    static bool             sForceOldBakedUpload; // If true will not use capabilities to upload baked textures.
    static S32              sUseOcclusion;  // 0 = no occlusion, 1 = read only, 2 = read/write
    static bool             sAutoMaskAlphaDeferred;
    static bool             sAutoMaskAlphaNonDeferred;
    static bool             sBakeSunlight;
    static bool             sNoAlpha;
    static bool             sUseFarClip;
    static bool             sShadowRender;
    static bool             sDynamicLOD;
    static bool             sPickAvatar;
    static bool             sImpostorRender;
    // Set for the frames of a snapshot cutout (ALRenderIsolation::Scope).
    static bool             sCutoutRender;
    static bool             sUnderWaterRender;
    static bool             sRenderGlow;
    static bool             sTextureBindTest;
    static bool             sRenderAttachedLights;
    static bool             sRenderAttachedParticles;
    static bool             sRenderDeferred;
    // Render inputs as the passes read them: set from the latched inputs (latchRenderInputs), never from a setting's
    // commit, so a pass and the programs and targets it draws with agree on them.
    static bool             sRenderTransparentWater;
    static bool             sReflectionProbesEnabled;
    static S32              sVisibleLightCount;
    static bool             sRenderingHUDs;
    static F32              sDistortionWaterClipPlaneMargin;
// [SL:KB] - Patch: Render-TextureToggle (Catznip-4.0)
    static bool             sRenderTextures;
// [/SL:KB]
// [RLVa:KB] - @setsphere
    static bool             sUseDepthTexture;
// [/RLVa:KB]

    // Set while a scopes floater is open. Nothing is sampled or read back
    // while this is false, so the feature costs exactly nothing when closed.
    static bool             sScopeCapture;
    /// Hold-to-compare: suppresses colour grading for as long as it is set.
    /// Render-time only, with nothing persistent behind it -- toggling
    /// RenderColorGrade instead would mark the active Look dirty, and a key
    /// released at the wrong moment could save the comparison as the look.
    static bool             sGradeBypass;

    /// The main camera's pass of a cutout frame: the one pass whose view is
    /// cut down to the cutout's subject. Shadows, impostor bakes, probes and
    /// HUDs in the same frame still see the whole world, which is what keeps
    /// the subject lit where it stands.
    static bool cullingToCutoutSubject() { return sCutoutRender && isMainWorldPass(); }

    /// Whether the alpha channel accumulates coverage rather than glow: in an
    /// impostor bake, and in a cutout frame's main pass. Blended geometry adds
    /// its coverage and glow writes are left out.
    static bool alphaWritesCoverage() { return sImpostorRender || cullingToCutoutSubject(); }

    /// A frame without the lens and print effects: a "No post-processing"
    /// snapshot, and every cutout frame, since each of those effects reaches
    /// past the subject's edge into what is to be left empty. Grading is not
    /// part of this -- only "No post-processing" drops the grade.
    static bool suppressesLensAndPrintEffects();

    /// One bit per grading group, for comparing a single section against the
    /// rest of the grade rather than against nothing at all.
    enum EGradeBypass : U32
    {
        GRADE_BYPASS_BASIC     = 1 << 0, ///< White balance, tone, presence: the Basic section
        GRADE_BYPASS_PRIMARIES = 1 << 1, ///< Lift / gamma / gain
        GRADE_BYPASS_SPLIT     = 1 << 2, ///< Split toning
        GRADE_BYPASS_LUT       = 1 << 3, ///< 3D LUT
        GRADE_BYPASS_CURVE     = 1 << 4, ///< Tone curve
    };

    /// A set bit makes colorCorrect upload that group's identity values rather
    /// than its settings, which lands in the early-out the shader already has
    /// for that step -- so a bypassed group costs less than an active one, and
    /// no shader variant is involved.
    ///
    /// Render-time only, and deliberately not a setting, for the same reason
    /// sGradeBypass is not: the Looks whitelist watches every grading control,
    /// so building a comparison out of one would mark the active Look dirty
    /// and let the comparison itself be saved as the look. It follows that
    /// whoever sets a bit owns clearing it -- see ~ALFloaterLightBox.
    static U32              sGradeBypassMask;

    class RenderTargetPack
    {
    public:
        U32                     width = 0;
        U32                     height = 0;

        //screen texture
        LLRenderTarget          screen;
        LLRenderTarget          deferredScreen;

        //sun shadow map
        LLRenderTarget          shadow[4];

        // How deep the HDR bloom pyramid generateBloomHDR acquires is: the
        // levels the plan describes, from the first, that the pool can build.
        U32                         bloomMipCount = 0;
    };

    // main full resoltuion render target
    RenderTargetPack mMainRT;

    // auxillary 512x512 render target pack
    // used by reflection probes and dynamic texture bakes
    RenderTargetPack mAuxillaryRT;

    // Auxillary render target pack scaled to the hero probe's per-face size.
    RenderTargetPack mHeroProbeRT;

    // currently used render target pack
    RenderTargetPack* mRT;

    // The frame's short-lived targets, lent by description to the passes and effects that use them: the plan's
    // transient resources (ALRenderResourcePlan::isTransient) and the scratch an effect takes in a resource's
    // description. Each is a pass's from its acquisition to its scope's end, and the next pass's after that.
    ALTransientTargetPool mTransientTargets;

    // A target of the description the plan gives @a id, from @a scope, named @a name or else as the plan names it: the
    // transient resource itself, or scratch in a resource's description. Null where the plan wants none, or none can be
    // built. One sharing a persistent resource's depth is attached to it; one sharing a transient resource's, to
    // @a depth_from, the target the caller acquired for it.
    LLRenderTarget* acquireResource(ALTransientTargets& scope, ALRenderResourcePlan::Id id, std::string_view name = {},
                                    LLRenderTarget* depth_from = nullptr);

    // The light map of the view whose deferred lighting is running, which bindDeferredShader binds where it is given
    // none; null outside that lighting, where a program reading it reads white.
    LLRenderTarget* mLightMap = nullptr;

    LLRenderTarget          mSpotShadow[2];

    LLRenderTarget          mPbrBrdfLut;

    // copy of the color/depth buffer just before gamma correction
    // for use by SSR
    LLRenderTarget          mSceneMap;

    // The auto-exposure: the meter grid (luminanceF.glsl), its 64-bin histogram
    // (exposureHistogramV.glsl), and the exposure, 1x1: x the scale, y the EV the next
    // frame adapts from (exposureF.glsl). mLastExposure is last frame's: the two swap their
    // storage as each frame's exposure is made.
    LLRenderTarget          mLuminanceMap;
    LLRenderTarget          mExposureHistogram;
    LLRenderTarget          mExposureMap;
    LLRenderTarget          mLastExposure;

    // lens flare sun state, 2x1, swapped each frame: [0] is this frame's, [1]
    // last frame's. Layout in lensFlareStateF.glsl.
    LLRenderTarget          mLensFlareState[2];
    bool                    mLensFlareStateValid = false;
    bool                    mLensFlareSunUp = true;    // which body the history describes

    // downres scratch space for GPU downscaling of textures
    LLRenderTarget          mDownResMap;

    // Scopes floater sample. Point-decimated copy of the presented frame plus
    // two readback slots: the read-back of one capture is collected at the
    // next one, several frames later, once it has landed, so neither the read
    // nor the collect waits -- on the GPU, or on a threaded driver's own
    // thread.
    LLRenderTarget          mScopeSample;
    ALGPUReadback           mScopeCaptures;
    LLFrameTimer            mScopeSampleTimer;
    ALScopeData             mScopeData;
    std::vector<U8>         mScopeReadback;

    // Reference still: a grabbed copy of a presented frame, for wiping the
    // live image against. Full resolution, so it is only allocated once
    // somebody asks for one.
    LLRenderTarget          mReferenceStill;
    bool                    mReferenceStillWanted = false;

    // One-shot scene probe, serviced in renderFinalize while the buffer is
    // still linear. See requestScenePixel.
    void                    serviceScenePixelProbe(LLRenderTarget* src);
    scene_pixel_cb_t        mScenePixelCallback;
    S32                     mScenePixelX = 0;
    S32                     mScenePixelY = 0;
    bool                    mScenePixelPending = false;

    LLCullResult            mSky;
    LLCullResult            mReflectedObjects;
    LLCullResult            mRefractedObjects;

    //utility buffers for rendering post effects
    LLPointer<LLVertexBuffer> mDeferredVB;

    // a single triangle that covers the whole screen
    LLPointer<LLVertexBuffer> mScreenTriangleVB;

    // the exposure meter's points: two per meter grid texel, its non-sky and sky parts
    LLPointer<LLVertexBuffer> mExposureHistogramVB;

    //utility buffer for rendering cubes, 8 vertices are corners of a cube [-1, 1]
    LLPointer<LLVertexBuffer> mCubeVB;

    //list of currently bound reflection maps
    std::vector<LLReflectionMap*> mReflectionMaps;

    std::vector<LLVector3>  mShadowFrustPoints[4];
    LLVector4               mShadowError;
    LLVector4               mShadowFOV;
    LLVector3               mShadowFrustOrigin[4];
    LLCamera                mShadowCamera[8];
    LLVector3               mShadowExtents[4][2];
    // TODO : separate Sun Shadow and Spot Shadow matrices
    LLMatrix4a              mSunShadowMatrix[6];
    LLMatrix4a              mShadowModelview[6];
    LLMatrix4a              mShadowProjection[6];
    LLMatrix4a              mReflectionModelView;

    LLPointer<LLDrawable>   mShadowSpotLight[2];
    F32                     mSpotLightFade[2];
    LLPointer<LLDrawable>   mTargetShadowSpotLight[2];

    LLVector4               mSunClipPlanes;
    LLVector4               mSunOrthoClipPlanes;
    LLVector2               mScreenScale;

    // ---- Shared deferred uniform block (UB_DEFERRED) --------------------------------------
    // std140-packed shadow/SSAO constants and the specular exponent, uploaded once per deferred
    // pass instead of being re-pushed by every bindDeferredShader call. Holds only what is
    // constant across a pass: screen_res is overridden per call and sun_dir/moon_dir are
    // entangled with atmospherics, so those stay loose.
    //
    // shadow_matrix@0, ssao_effect_mat@384, shadow_clip@432, shadow_res@448,
    // proj_shadow_res@456, scalars @464..500, size 512. Matrices go up as they lie: an
    // LLMatrix4a's rows are the columns std140 reads by default, and ssao_effect_mat is
    // symmetric either way. Checked against the driver at shader load by
    // validateEngineBlockLayouts() in debug builds.
    struct alignas(16) DeferredUBOData
    {
        F32 shadow_matrix[6][16];      // mat4[6]
        F32 ssao_effect_mat[12];       // mat3 (3 vec4-strided columns)
        F32 shadow_clip[4];            // vec4
        F32 shadow_res[2];             // vec2
        F32 proj_shadow_res[2];        // vec2
        F32 shadow_bias;
        F32 shadow_offset;
        F32 spot_shadow_bias;
        F32 spot_shadow_offset;
        F32 ssao_radius;
        F32 ssao_max_radius;
        F32 ssao_factor;
        F32 ssao_factor_inv;
        F32 shadow_pcss_scale;
        F32 specular_exponent;
    };

    DeferredUBOData             mDeferredUBOData{};
    ALUniformBuffer             mDeferredUBO;
    bool                        mDeferredUBODirty{ true };

    // The water pool's copies of the scene under its surface, from the pool for its pass alone (beginWaterTargets to
    // endWaterTargets) where the water is transparent: the screen's colour over the part of the screen the surface
    // reads it at (its field's screen rectangle and the ripples' reach past it), and the scene's depth, copied since the
    // surface writes its own depth over the scene's while it reads what lies beneath, and which the waves' depth
    // pyramid is built from. The frame's one copy of the depth: the haze passes, which write none, read the scene's in
    // place. Null outside that pass, and for opaque water, which reads neither.
    void            beginWaterTargets();
    void            endWaterTargets();
    LLRenderTarget* waterRefraction() const { return mWaterRefraction; }
    LLRenderTarget* waterDepthCopy() const { return mWaterDepthCopy; }
    std::optional<ALTransientTargets> mWaterTargets;
    LLRenderTarget*                   mWaterRefraction = nullptr;
    LLRenderTarget*                   mWaterDepthCopy  = nullptr;

    // The sky dome behind everything at half resolution: the sky pool draws it again here, with no depth, and the
    // haze pass converges far water on it (hazeF.glsl). The main view's alone, from the pool from the sky pool's draw
    // (acquireSkyBehind) to the end of the deferred lighting its haze runs in (releaseSkyBehind): null outside that,
    // and in a frame that has none to draw.
    LLRenderTarget*             acquireSkyBehind();
    void                        releaseSkyBehind();
    ALTransientTargets          mSkyBehindTargets{ mTransientTargets };
    LLRenderTarget*             mSkyBehind = nullptr;
    // Whether any water was in front of the sky's depth when the sky pool drew: the sky behind is drawn only then, and
    // the haze shades the water's pixels only then. An occlusion query of gGL's, asked in mSkyBehindQueryFrame.
    ALQueryTicket               mSkyBehindQuery;
    U32                         mSkyBehindQueryFrame = 0;

    // The attachments profileAvatar() timed, with their spans of gGL's pool, until readAttachmentTimes() has them.
    std::vector<std::pair<LLPointer<LLViewerObject>, ALQuerySpan>> mAttachmentSpans;

    // The water's layout around the main camera and its map of cell heights, laid out at the start of each main frame.
    ALWaterSurface              mWaterSurface;

    // The farthest depth under each texel of the main view's depth, built where a GPU cull asks for it.
    ALDepthPyramid              mDepthPyramid;

    // Profiling tracks, plotted at the start of each main frame for a frame a few before: the primitives the main view's
    // terrain and water passes draw, the water's flat field and its waves' patches apart, and the draws of water
    // geometry in every view and pass.
    ALPrimitivePlot             mTerrainPrimitives{ "terrain primitives", true };
    ALPrimitivePlot             mWaterPrimitives{ "water primitives", false };
    ALPrimitivePlot             mWaterPatchPrimitives{ "water patch primitives", true };
    U32                         mWaterDraws = 0;

    static constexpr U32 MAX_PREVIEW_WIDTH = ALRenderResourcePlan::PREVIEW_SIZE;


    //noise map
    U32                 mNoiseMap;
    U32                 mTrueNoiseMap;
    // The units the shadow maps are bound to live in LLGLSLShader::sCompareSamplerUnits --
    // published there because LLGLSLShader::bind() is what releases them ahead of
    // non-declaring programs; see bindShadowMaps.


    //smaa
    U32                 mSMAAAreaMap = 0;
    U32                 mSMAASearchMap = 0;
    U32                 mSMAASampleMap = 0;

    // Lens dirt plate, generated rather than loaded -- see generateLensDirt.
    // Nothing is allocated until the effect is switched on, and the memory goes
    // back when it is switched off, so an incomplete target is also the signal
    // to force the strength uniform to 0 and leave the sampler unread.
    LLRenderTarget      mLensDirtMap;

    // What the current plate was generated from. Comparing the whole set each
    // frame is what triggers a rebuild, which covers parameter edits and window
    // resizes through one test and needs no commit-signal plumbing. It also
    // records a *failed* attempt, so a plate that could not be allocated is
    // retried when something changes rather than on every frame.
    struct LensDirtParams
    {
        U32 width      = 0;
        U32 height     = 0;
        F32 seed       = -1.f;
        F32 grime      = -1.f;
        F32 mote_scale = -1.f;
        F32 smudge     = -1.f;
        S32 scratches  = -1;
        F32 toe        = -1.f;
        F32 gain       = -1.f;

        bool operator==(const LensDirtParams&) const = default;
    };
    LensDirtParams      mLensDirtParams;

    // The lens distortion auto-fit scale, and what it was solved for. The
    // solve walks the frame edge and an interior grid, and its inputs move
    // only when a slider or the window does, so it is compared and skipped
    // each frame the way the dirt plate is.
    struct LensDistortFit
    {
        struct Inputs
        {
            F32 k1 = 0.f;
            F32 k2 = 0.f;
            F32 p1 = 0.f;
            F32 p2 = 0.f;
            F32 cx = 0.f;
            F32 cy = 0.f;
            F32 axis_x = 0.f;
            F32 axis_y = 0.f;
            F32 sq_x = 0.f;
            S32 fit_mode = -1;

            bool operator==(const Inputs&) const = default;
        };
        Inputs inputs;
        F32    scale = 1.f;
    };
    LensDistortFit      mLensDistortFit;

    // Raised by the Lightbox while one of the generation sliders is being
    // dragged. The plate is full-resolution, so rebuilding on every frame of a
    // drag is a stutter rather than a preview -- and worse the slower the
    // machine, which is backwards. Holding off means the rebuild lands once, on
    // release, which is also where the user expects to see the result.
    bool                mLensDirtSliderHeld = false;
    // Tone curve LUT: a 512x1 RGBA16 row (ALToneCurveSet::LUT_SIZE) whose R, G
    // and B texels hold master(channel(x)) for each channel. A raw GL name
    // like the SMAA maps above: created empty by reconcileResources, filled by
    // bakeToneCurveLut, released in releaseAllGLResources.
    U32                 mToneCurveLut = 0;
    bool                mToneCurveLutDirty = true;   // settings changed, or the texture was recreated, since the last bake
    bool                mToneCurveIdentity = true;   // the last bake found all four curves on the diagonal

    LLColor4            mSunDiffuse;
    LLColor4            mMoonDiffuse;
    LLVector4           mSunDir;
    LLVector4           mMoonDir;
    bool                mNeedsShadowTargetClear;

    LLVector4           mTransformedSunDir;
    LLVector4           mTransformedMoonDir;

    // Sun (or moon) on screen this frame in UV, from generateLensFlareState.
    LLVector2           mLensFlareSunUV = LLVector2(0.5f, 0.5f);

    bool                    mInitialized;
    bool                    mShadersLoaded;

protected:
    bool                    mRenderTypeEnabled[NUM_RENDER_TYPES];
    std::stack<std::string> mRenderTypeEnableStack;

    U32                     mRenderDebugFeatureMask;
    U64                     mRenderDebugMask;
    U64                     mOldRenderDebugMask;
    std::stack<U32>         mRenderDebugFeatureStack;

    /////////////////////////////////////////////
    //
    //
    LLDrawable::drawable_vector_t   mMovedList;
    LLDrawable::drawable_vector_t mMovedBridge;
    LLDrawable::drawable_vector_t   mShiftList;

    /////////////////////////////////////////////
    //
    //
    struct Light
    {
        Light(LLDrawable* ptr, F32 d, F32 f = 0.0f)
            : drawable(ptr),
              dist(d),
              fade(f)
        {}
        LLPointer<LLDrawable> drawable;
        F32 dist;
        F32 fade;
        struct compare
        {
            bool operator()(const Light& a, const Light& b) const
            {
                if ( a.dist < b.dist )
                    return true;
                else if ( a.dist > b.dist )
                    return false;
                else
                    return a.drawable < b.drawable;
            }
        };
    };
    typedef std::set< Light, Light::compare > light_set_t;

    LLDrawable::ordered_drawable_set_t  mLights;
    light_set_t                     mNearbyLights; // lights near camera
    LLColor4                        mHWLightColors[8];

    /////////////////////////////////////////////
    //
    // Different queues of drawables being processed.
    //
    LLDrawable::drawable_list_t     mBuildQ1; // priority
    LLSpatialGroup::sg_vector_t     mGroupQ1; //priority

    LLSpatialGroup::sg_vector_t     mGroupSaveQ1; // a place to save mGroupQ1 until it is safe to unref

    LLSpatialGroup::sg_vector_t     mMeshDirtyGroup; //groups that need rebuildMesh called

    LLDrawable::drawable_list_t     mPartitionQ; //drawables that need to update their spatial partition radius

    bool mGroupQ1Locked;

    bool mResetVertexBuffers; //if true, clear vertex buffers on next update

    LLViewerObject::vobj_list_t     mCreateQ;

    LLDrawable::drawable_set_t      mRetexturedList;

    class HighlightItem
    {
    public:
        const LLPointer<LLDrawable> mItem;
        mutable F32 mFade;

        HighlightItem(LLDrawable* item)
        : mItem(item), mFade(0)
        {
        }

        bool operator<(const HighlightItem& rhs) const
        {
            return mItem < rhs.mItem;
        }

        bool operator==(const HighlightItem& rhs) const
        {
            return mItem == rhs.mItem;
        }

        void incrFade(F32 val) const
        {
            mFade = llclamp(mFade+val, 0.f, 1.f);
        }
    };

    //////////////////////////////////////////////////
    //
    // Draw pools are responsible for storing all rendered data,
    // and performing the actual rendering of objects.
    //
    struct compare_pools
    {
        bool operator()(const LLDrawPool* a, const LLDrawPool* b) const
        {
            if (!a)
                return true;
            else if (!b)
                return false;
            else
            {
                S32 atype = a->getType();
                S32 btype = b->getType();
                if (atype < btype)
                    return true;
                else if (atype > btype)
                    return false;
                else
                    return a->getId() < b->getId();
            }
        }
    };
    typedef std::set<LLDrawPool*, compare_pools > pool_set_t;
    pool_set_t mPools;
    LLDrawPool* mLastRebuildPool;

    // For quick-lookups into mPools (mapped by texture pointer)
    std::map<uintptr_t, LLDrawPool*>    mTerrainPools;
    std::map<uintptr_t, LLDrawPool*>    mTreePools;
    LLDrawPoolAlpha*            mAlphaPoolPreWater = nullptr;
    LLDrawPoolAlpha*            mAlphaPoolPostWater = nullptr;
    LLDrawPool*                 mSkyPool = nullptr;
    LLDrawPool*                 mTerrainPool = nullptr;
    LLDrawPool*                 mWaterPool = nullptr;
    LLRenderPass*               mSimplePool = nullptr;
    LLRenderPass*               mGrassPool = nullptr;
    LLRenderPass*               mAlphaMaskPool = nullptr;
    LLRenderPass*               mFullbrightAlphaMaskPool = nullptr;
    LLRenderPass*               mFullbrightPool = nullptr;
    LLDrawPool*                 mGlowPool = nullptr;
    LLDrawPool*                 mBumpPool = nullptr;
    LLDrawPool*                 mMaterialsPool = nullptr;
    LLDrawPool*                 mWLSkyPool = nullptr;
    LLDrawPool*                 mPBROpaquePool = nullptr;
    LLDrawPool*                 mPBRAlphaMaskPool = nullptr;
    LLDrawPool*                 mWaterExclusionPool      = nullptr;

    // Note: no need to keep an quick-lookup to avatar pools, since there's only one per avatar

    // Color grading lookup texture and size
    LLPointer<ALTexture3D> mCGLut;
    LLVector4 mCGLutSize{};

public:
    std::vector<LLFace*>        mHighlightFaces;    // highlight faces on physical objects
protected:
    std::vector<LLFace*>        mSelectedFaces;

    class DebugBlip
    {
    public:
        LLColor4 mColor;
        LLVector3 mPosition;
        F32 mAge;

        DebugBlip(const LLVector3& position, const LLColor4& color)
            : mColor(color), mPosition(position), mAge(0.f)
        { }
    };

    std::list<DebugBlip> mDebugBlips;

    LLPointer<LLViewerFetchedTexture>   mFaceSelectImagep;

    U32                     mLightMask;
    U32                     mLightMovingMask;

    static bool             sRenderPhysicalBeacons;
    static bool             sRenderMOAPBeacons;
    static bool             sRenderScriptedTouchBeacons;
    static bool             sRenderScriptedBeacons;
    static bool             sRenderParticleBeacons;
    static bool             sRenderSoundBeacons;
public:
    static bool             sRenderBeacons;
    static bool             sRenderHighlight;

    // Determines which set of UVs to use in highlight display
    //
    static LLRender::eTexIndex sRenderHighlightTextureChannel;

    //debug use
    static U32              sCurRenderPoolType ;

    //cached settings
    static bool WindLightUseAtmosShaders;
    static bool RenderDeferred;
    static F32 RenderDeferredSunWash;
    static S32 RenderShadowSplits;
    static bool RenderDelayCreation;
    static bool RenderAnimateRes;
    static bool FreezeTime;
    static S32 DebugBeaconLineWidth;
    static F32 RenderHighlightBrightness;
    static LLColor4 RenderHighlightColor;
    static F32 RenderHighlightThickness;
    static bool RenderSpotLightsInNondeferred;
    static LLColor4 PreviewAmbientColor;
    static LLColor4 PreviewDiffuse0;
    static LLColor4 PreviewSpecular0;
    static LLColor4 PreviewDiffuse1;
    static LLColor4 PreviewSpecular1;
    static LLColor4 PreviewDiffuse2;
    static LLColor4 PreviewSpecular2;
    static LLVector3 PreviewDirection0;
    static LLVector3 PreviewDirection1;
    static LLVector3 PreviewDirection2;
    static F32 RenderGlowMinLuminance;
    static F32 RenderGlowMaxExtractAlpha;
    static F32 RenderGlowWarmthAmount;
    static LLVector3 RenderGlowLumWeights;
    static LLVector3 RenderGlowWarmthWeights;
    static S32 RenderGlowIterations;
    static F32 RenderGlowWidth;
    static F32 RenderGlowStrength;
    static bool RenderDepthOfField;
    static F32 CameraFocusTransitionTime;
    static F32 CameraFNumber;
    static F32 CameraFocalLength;
    static F32 CameraFieldOfView;
    static F32 RenderShadowNoise;
    static F32 RenderShadowBlurSize;
    static F32 RenderSSAOScale;
    static U32 RenderSSAOMaxScale;
    static F32 RenderSSAOFactor;
    static LLVector3 RenderSSAOEffect;
    static F32 RenderShadowOffsetError;
    static F32 RenderShadowBiasError;
    static F32 RenderShadowOffset;
    static F32 RenderShadowBias;
    static F32 RenderSpotShadowOffset;
    static F32 RenderSpotShadowBias;
    static LLDrawable* RenderSpotLight;
    static F32 RenderEdgeDepthCutoff;
    static F32 RenderEdgeNormCutoff;
    static LLVector3 RenderShadowGaussian;
    static F32 RenderShadowBlurDistFactor;
    static bool RenderDeferredAtmospheric;
    static F32 RenderHighlightFadeTime;
    static F32 RenderFarClip;
    static LLVector3 RenderShadowSplitExponent;
    static F32 RenderShadowErrorCutoff;
    static F32 RenderShadowFOVCutoff;
    static bool CameraOffset;
    static F32 CameraMaxCoF;
    static F32 CameraDoFResScale;
    static F32 RenderAutoHideSurfaceAreaLimit;
    static S32 RenderScreenSpaceReflectionIterations;
    static F32 RenderScreenSpaceReflectionRayStep;
    static F32 RenderScreenSpaceReflectionDistanceBias;
    static F32 RenderScreenSpaceReflectionDepthRejectBias;
    static F32 RenderScreenSpaceReflectionAdaptiveStepMultiplier;
    static S32 RenderScreenSpaceReflectionGlossySamples;
    static S32 RenderBufferVisualization;
    static S32 RenderHeroProbeUpdateRate;
    static S32 RenderHeroProbeConservativeUpdateMultiplier;

private:
    // The main camera's view of the world: not a shadow map, an impostor
    // bake, the HUD or a probe's cube face.
    static bool isMainWorldPass();
};

// Occlusion culling off for as long as this lives.
class LLDisableOcclusionCulling
{
public:
    S32 mUseOcclusion;

    LLDisableOcclusionCulling()
    {
        mUseOcclusion = LLPipeline::sUseOcclusion;
        LLPipeline::sUseOcclusion = 0;
    }

    ~LLDisableOcclusionCulling()
    {
        LLPipeline::sUseOcclusion = mUseOcclusion;
    }
};

void render_bbox(const LLVector3 &min, const LLVector3 &max);
void render_hud_elements();

extern LLPipeline gPipeline;
extern bool gDebugPipeline;
extern const LLMatrix4a* gGLLastMatrix;

#endif
