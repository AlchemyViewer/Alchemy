/**
 * @file lldrawpool.h
 * @brief LLDrawPool class definition
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLDRAWPOOL_H
#define LL_LLDRAWPOOL_H

#include "v4coloru.h"
#include "v2math.h"
#include "v3math.h"
#include "llvertexbuffer.h"

class LLFace;
class LLViewerTexture;
class LLViewerFetchedTexture;
class LLSpatialGroup;
class LLDrawInfo;
class LLVOAvatar;
class LLGLSLShader;
class LLMeshSkinInfo;
class LLFetchedGLTFMaterial;

class LLDrawPool
{
public:
    static S32 sNumDrawPools;

    enum
    {
        // Correspond to LLPipeline render type
        // Also controls render order, so passes that don't use alpha masking/blending should come before
        // other passes to preserve hierarchical Z for occlusion queries.  Occlusion queries happen just
        // before grass, so grass should be the first alpha masked pool.  Other ordering should be done
        // based on fill rate and likelihood to occlude future passes (faster, large occluders first).
        //
        POOL_SKY = 1,
        POOL_WATEREXCLUSION,
        POOL_WL_SKY,
        POOL_SIMPLE,
        POOL_FULLBRIGHT,
        POOL_BUMP,
        POOL_MATERIALS,
        POOL_GLTF_PBR,
        POOL_TERRAIN,
        POOL_GRASS,
        POOL_GLTF_PBR_ALPHA_MASK,
        POOL_TREE,
        POOL_ALPHA_MASK,
        POOL_FULLBRIGHT_ALPHA_MASK,
        POOL_AVATAR,
        POOL_CONTROL_AV, // Animesh
        POOL_GLOW,
        POOL_ALPHA_PRE_WATER,
        POOL_VOIDWATER,
        POOL_WATER,
        POOL_ALPHA_POST_WATER,
        POOL_ALPHA, // note there is no actual "POOL_ALPHA" but pre-water and post-water pools consume POOL_ALPHA faces
        NUM_POOL_TYPES,
    };

    LLDrawPool(const U32 type);
    virtual ~LLDrawPool();

    virtual bool isDead() = 0;

    S32 getId() const { return mId; }
    U32 getType() const { return mType; }

    bool getSkipRenderFlag() const { return mSkipRender;}
    void setSkipRenderFlag( bool flag ) { mSkipRender = flag; }

    virtual LLViewerTexture *getDebugTexture();
    virtual void beginRenderPass( S32 pass );
    virtual void endRenderPass( S32 pass );
    virtual S32  getNumPasses();

    virtual void beginDeferredPass(S32 pass);
    virtual void endDeferredPass(S32 pass);
    virtual S32 getNumDeferredPasses();
    virtual void renderDeferred(S32 pass = 0);

    virtual void beginPostDeferredPass(S32 pass);
    virtual void endPostDeferredPass(S32 pass);
    virtual S32 getNumPostDeferredPasses();
    virtual void renderPostDeferred(S32 pass = 0);

    virtual void beginShadowPass(S32 pass);
    virtual void endShadowPass(S32 pass);
    virtual S32 getNumShadowPasses();
    virtual void renderShadow(S32 pass = 0);

    virtual void render(S32 pass = 0) {};
    virtual void prerender() {};
    virtual U32 getVertexDataMask() { return 0; } // DEPRECATED -- draw pool doesn't actually determine vertex data mask any more
    virtual bool verify() const { return true; }        // Verify that all data in the draw pool is correct!
    virtual S32 getShaderLevel() const { return mShaderLevel; }

    static LLDrawPool* createPool(const U32 type, LLViewerTexture *tex0 = NULL);
    virtual LLViewerTexture* getTexture() = 0;
    virtual bool isFacePool() { return false; }
    virtual void resetDrawOrders() = 0;
    virtual void pushFaceGeometry() {}

    S32 mShaderLevel;
    S32 mId;
    U32 mType;              // Type of draw pool
    bool mSkipRender;
};

// The converted legacy G-buffer writers bind their colour textures with
// ALSamplers::AnisoWrapSRGB (alsamplerstate.h). Hardware decodes the texture, the shader
// linearises the prim tint in its vertex stage, and the deferred pass's hoisted
// GL_FRAMEBUFFER_SRGB (renderGeomDeferred) re-encodes on store -- all three together, or
// the pass renders at the wrong gamma.
//
// Converted: diffuse, diffusealphamask (incl. grass), bump, tree, terrain, materials,
// avatar skin, PBR. The remaining raw writers -- the WL sky family, the avatar
// impostor/rigid passes, and the parcel-owner overlay -- opt OUT of the hoisted encode
// locally and keep the AnisoWrap default on their samples.
//
// A pass gets the vertex-stage half for free if its vertex shader is shared with a converted
// pass, which is how grass came to linearise its tint while sampling and storing sRGB. The
// linearisation is keyed on LINEAR_DIFFUSE for that reason -- the same define mLinearDiffuse
// is derived from -- so a program that has not opted in cannot pick up half a conversion.
//
// Data textures never take the decode even in a converted pass -- terrain's alpha_ramp is a
// mask read through .a, and alpha is not part of the sRGB transfer function anyway.

class LLRenderPass : public LLDrawPool
{
public:
    // list of possible LLRenderPass types to assign a render batch to
    // NOTE: "rigged" variant MUST be non-rigged variant + 1
    // see LLVolumeGeometryManager::registerFace()
    enum
    {
        PASS_SIMPLE = NUM_POOL_TYPES,
        PASS_SIMPLE_RIGGED,
        PASS_GRASS,
        PASS_FULLBRIGHT,
        PASS_FULLBRIGHT_RIGGED,
        PASS_INVISIBLE,                         // Formerly, invisiprims.  Now, water exclusion surfaces.
        PASS_INVISIBLE_RIGGED,
        PASS_INVISI_SHINY,
        PASS_INVISI_SHINY_RIGGED,
        PASS_FULLBRIGHT_SHINY,
        PASS_FULLBRIGHT_SHINY_RIGGED,
        PASS_SHINY,
        PASS_SHINY_RIGGED,
        PASS_BUMP,
        PASS_BUMP_RIGGED,
        PASS_POST_BUMP,
        PASS_POST_BUMP_RIGGED,
        PASS_MATERIAL,
        PASS_MATERIAL_RIGGED,
        PASS_MATERIAL_ALPHA,
        PASS_MATERIAL_ALPHA_RIGGED,
        PASS_MATERIAL_ALPHA_MASK,              // Diffuse texture used as alpha mask
        PASS_MATERIAL_ALPHA_MASK_RIGGED,
        PASS_MATERIAL_ALPHA_EMISSIVE,
        PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED,
        PASS_SPECMAP,
        PASS_SPECMAP_RIGGED,
        PASS_SPECMAP_BLEND,
        PASS_SPECMAP_BLEND_RIGGED,
        PASS_SPECMAP_MASK,                     // Diffuse texture used as alpha mask and specular texture(map)
        PASS_SPECMAP_MASK_RIGGED,
        PASS_SPECMAP_EMISSIVE,
        PASS_SPECMAP_EMISSIVE_RIGGED,
        PASS_NORMMAP,
        PASS_NORMMAP_RIGGED,
        PASS_NORMMAP_BLEND,
        PASS_NORMMAP_BLEND_RIGGED,
        PASS_NORMMAP_MASK,                     // Diffuse texture used as alpha mask and normal map
        PASS_NORMMAP_MASK_RIGGED,
        PASS_NORMMAP_EMISSIVE,
        PASS_NORMMAP_EMISSIVE_RIGGED,
        PASS_NORMSPEC,
        PASS_NORMSPEC_RIGGED,
        PASS_NORMSPEC_BLEND,
        PASS_NORMSPEC_BLEND_RIGGED,
        PASS_NORMSPEC_MASK,                    // Diffuse texture used as alpha mask with normal and specular map
        PASS_NORMSPEC_MASK_RIGGED,
        PASS_NORMSPEC_EMISSIVE,
        PASS_NORMSPEC_EMISSIVE_RIGGED,
        PASS_GLOW,
        PASS_GLOW_RIGGED,
        PASS_GLTF_GLOW,
        PASS_GLTF_GLOW_RIGGED,
        PASS_ALPHA,
        PASS_ALPHA_RIGGED,
        PASS_ALPHA_MASK,
        PASS_ALPHA_MASK_RIGGED,
        PASS_FULLBRIGHT_ALPHA_MASK,            // Diffuse texture used as alpha mask and fullbright
        PASS_FULLBRIGHT_ALPHA_MASK_RIGGED,
        PASS_ALPHA_INVISIBLE,
        PASS_ALPHA_INVISIBLE_RIGGED,
        PASS_GLTF_PBR,
        PASS_GLTF_PBR_RIGGED,
        PASS_GLTF_PBR_ALPHA_MASK,
        PASS_GLTF_PBR_ALPHA_MASK_RIGGED,
        NUM_RENDER_TYPES,
    };

    #if LL_PROFILER_ENABLE_RENDER_DOC
    static inline const char* lookupPassName(U32 pass)
    {
        switch (pass)
        {
            case PASS_SIMPLE:
                return "PASS_SIMPLE";
            case PASS_SIMPLE_RIGGED:
                return "PASS_SIMPLE_RIGGED";
            case PASS_GRASS:
                return "PASS_GRASS";
            case PASS_FULLBRIGHT:
                return "PASS_FULLBRIGHT";
            case PASS_FULLBRIGHT_RIGGED:
                return "PASS_FULLBRIGHT_RIGGED";
            case PASS_INVISIBLE:
                return "PASS_INVISIBLE";
            case PASS_INVISIBLE_RIGGED:
                return "PASS_INVISIBLE_RIGGED";
            case PASS_INVISI_SHINY:
                return "PASS_INVISI_SHINY";
            case PASS_INVISI_SHINY_RIGGED:
                return "PASS_INVISI_SHINY_RIGGED";
            case PASS_FULLBRIGHT_SHINY:
                return "PASS_FULLBRIGHT_SHINY";
            case PASS_FULLBRIGHT_SHINY_RIGGED:
                return "PASS_FULLBRIGHT_SHINY_RIGGED";
            case PASS_SHINY:
                return "PASS_SHINY";
            case PASS_SHINY_RIGGED:
                return "PASS_SHINY_RIGGED";
            case PASS_BUMP:
                return "PASS_BUMP";
            case PASS_BUMP_RIGGED:
                return "PASS_BUMP_RIGGED";
            case PASS_POST_BUMP:
                return "PASS_POST_BUMP";
            case PASS_POST_BUMP_RIGGED:
                return "PASS_POST_BUMP_RIGGED";
            case PASS_MATERIAL:
                return "PASS_MATERIAL";
            case PASS_MATERIAL_RIGGED:
                return "PASS_MATERIAL_RIGGED";
            case PASS_MATERIAL_ALPHA:
                return "PASS_MATERIAL_ALPHA";
            case PASS_MATERIAL_ALPHA_RIGGED:
                return "PASS_MATERIAL_ALPHA_RIGGED";
            case PASS_MATERIAL_ALPHA_MASK:
                return "PASS_MATERIAL_ALPHA_MASK";
            case PASS_MATERIAL_ALPHA_MASK_RIGGED:
                return "PASS_MATERIAL_ALPHA_MASK_RIGGED";
            case PASS_MATERIAL_ALPHA_EMISSIVE:
                return "PASS_MATERIAL_ALPHA_EMISSIVE";
            case PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED:
                return "PASS_MATERIAL_ALPHA_EMISSIVE_RIGGED";
            case PASS_SPECMAP:
                return "PASS_SPECMAP";
            case PASS_SPECMAP_RIGGED:
                return "PASS_SPECMAP_RIGGED";
            case PASS_SPECMAP_BLEND:
                return "PASS_SPECMAP_BLEND";
            case PASS_SPECMAP_BLEND_RIGGED:
                return "PASS_SPECMAP_BLEND_RIGGED";
            case PASS_SPECMAP_MASK:
                return "PASS_SPECMAP_MASK";
            case PASS_SPECMAP_MASK_RIGGED:
                return "PASS_SPECMAP_MASK_RIGGED";
            case PASS_SPECMAP_EMISSIVE:
                return "PASS_SPECMAP_EMISSIVE";
            case PASS_SPECMAP_EMISSIVE_RIGGED:
                return "PASS_SPECMAP_EMISSIVE_RIGGED";
            case PASS_NORMMAP:
                return "PASS_NORMAMAP";
            case PASS_NORMMAP_RIGGED:
                return "PASS_NORMMAP_RIGGED";
            case PASS_NORMMAP_BLEND:
                return "PASS_NORMMAP_BLEND";
            case PASS_NORMMAP_BLEND_RIGGED:
                return "PASS_NORMMAP_BLEND_RIGGED";
            case PASS_NORMMAP_MASK:
                return "PASS_NORMMAP_MASK";
            case PASS_NORMMAP_MASK_RIGGED:
                return "PASS_NORMMAP_MASK_RIGGED";
            case PASS_NORMMAP_EMISSIVE:
                return "PASS_NORMMAP_EMISSIVE";
            case PASS_NORMMAP_EMISSIVE_RIGGED:
                return "PASS_NORMMAP_EMISSIVE_RIGGED";
            case PASS_NORMSPEC:
                return "PASS_NORMSPEC";
            case PASS_NORMSPEC_RIGGED:
                return "PASS_NORMSPEC_RIGGED";
            case PASS_NORMSPEC_BLEND:
                return "PASS_NORMSPEC_BLEND";
            case PASS_NORMSPEC_BLEND_RIGGED:
                return "PASS_NORMSPEC_BLEND_RIGGED";
            case PASS_NORMSPEC_MASK:
                return "PASS_NORMSPEC_MASK";
            case PASS_NORMSPEC_MASK_RIGGED:
                return "PASS_NORMSPEC_MASK_RIGGED";
            case PASS_NORMSPEC_EMISSIVE:
                return "PASS_NORMSPEC_EMISSIVE";
            case PASS_NORMSPEC_EMISSIVE_RIGGED:
                return "PASS_NORMSPEC_EMISSIVE_RIGGED";
            case PASS_GLOW:
                return "PASS_GLOW";
            case PASS_GLOW_RIGGED:
                return "PASS_GLOW_RIGGED";
            case PASS_ALPHA:
                return "PASS_ALPHA";
            case PASS_ALPHA_RIGGED:
                return "PASS_ALPHA_RIGGED";
            case PASS_ALPHA_MASK:
                return "PASS_ALPHA_MASK";
            case PASS_ALPHA_MASK_RIGGED:
                return "PASS_ALPHA_MASK_RIGGED";
            case PASS_FULLBRIGHT_ALPHA_MASK:
                return "PASS_FULLBRIGHT_ALPHA_MASK";
            case PASS_FULLBRIGHT_ALPHA_MASK_RIGGED:
                return "PASS_FULLBRIGHT_ALPHA_MASK_RIGGED";
            case PASS_ALPHA_INVISIBLE:
                return "PASS_ALPHA_INVISIBLE";
            case PASS_ALPHA_INVISIBLE_RIGGED:
                return "PASS_ALPHA_INVISIBLE_RIGGED";
            case PASS_GLTF_PBR:
                return "PASS_GLTF_PBR";
            case PASS_GLTF_PBR_RIGGED:
                return "PASS_GLTF_PBR_RIGGED";
            case PASS_GLTF_PBR_ALPHA_MASK:
                return "PASS_GLTF_PBR_ALPHA_MASK";
            case PASS_GLTF_PBR_ALPHA_MASK_RIGGED:
                return "PASS_GLTF_PBR_ALPHA_MASK_RIGGED";
            default:
                return "Unknown pass";
        }
    }
    #else
    static inline const char* lookupPassName(U32 pass) { return ""; }
    #endif

    LLRenderPass(const U32 type);
    virtual ~LLRenderPass();
    /*virtual*/ LLViewerTexture* getDebugTexture() { return NULL; }
    LLViewerTexture* getTexture() { return NULL; }
    bool isDead() { return false; }
    void resetDrawOrders() { }

    static void applyModelMatrix(const LLDrawInfo& params);
    // For rendering that doesn't use LLDrawInfo for some reason
    static void applyModelMatrix(const LLMatrix4* model_matrix);

    // Bind an indexed-texture batch (tex0..texN-1, see objects/indexedTextureV.glsl).
    //
    // A null entry inside the batch gets a white stand-in rather than being skipped: the
    // ladder is selected per-vertex, so a skipped slot means some vertex samples whatever
    // texture the previous draw happened to leave on that unit -- the wrong image, not merely
    // an unused one.
    //
    // Channels ABOVE the batch are deliberately left alone. The program declares the full
    // ladder, so they do hold a previous draw's textures, but an ordinary 2D texture under an
    // unreached sampler2D is defined and costs nothing; clearing them would mean up to N
    // redundant binds on every draw in the hottest loop in the renderer. What must never land
    // there is a texture whose SAMPLER disagrees with the declaration -- in practice a shadow
    // map under a compare sampler, which is undefined even where the shader's dynamic branch
    // never reaches it. LLPipeline::bindShadowMaps is the only compare-sampler bind in the
    // tree and it publishes its units in LLGLSLShader::sCompareSamplerUnits, which bind()
    // releases before any program that declares no shadow samplers runs.
    // validate_bound_samplers() asserts the invariant at the draw under gDebugGL.
    //
    // So: a NEW compare sampler, or any depth texture bound outside that tracking, breaks
    // this. Publish it in the same mask rather than clearing the tail here.
    //
    // The diffuse sampler is derived from the bound program, not named by the caller:
    // AnisoWrap, plus the SRGBDecode bit when the program's mLinearDiffuse (the
    // LINEAR_DIFFUSE permutation) says the pass shades in linear. The matching re-encode
    // on store comes from the deferred pass's hoisted GL_FRAMEBUFFER_SRGB
    // (renderGeomDeferred); raw pass-through writers -- the WL sky family, the avatar
    // impostor/rigid passes -- opt out of that encode locally and keep undecoded samples,
    // letting the lighting pass's decoded read close the loop instead.
    static void bindIndexedTextures(const LLDrawInfo& params, const LLGLSLShader* shader);
    void pushBatches(U32 type, bool texture = true, bool batch_textures = false);
    void pushUntexturedBatches(U32 type);

    void pushRiggedBatches(U32 type, bool texture = true, bool batch_textures = false);
    void pushUntexturedRiggedBatches(U32 type);

    // push full GLTF batches
    // assumes draw infos of given type have valid GLTF materials
    void pushGLTFBatches(U32 type);

    // Which material maps an indexed GLTF batch binds and uploads. Trimming the set
    // skips needless texture binds / uniform uploads (and the normal-map discard-level
    // fetch) when a pass samples only some maps.
    enum eGLTFIndexedMaps
    {
        GLTF_MAPS_FULL = 0,    // base color + normal + ORM + emissive (GBuffer write)
        GLTF_MAPS_BASE_COLOR,  // base color only (shadow alpha-mask discard)
        GLTF_MAPS_GLOW,        // base color + emissive (glow/emissive pass)
    };

    // Indexed (multi-material) GLTF PBR helpers. Indexed and scalar draw infos
    // coexist in the same render map (PASS_GLTF_PBR); they are distinguished by
    // mGLTFMaterialList.size() > 1. Shadow/probe passes use the plain
    // pushGLTFBatches (rendering everything scalar for depth); only the main
    // opaque GBuffer pass splits the two:
    //   pushGLTFBatchesScalar  -- renders only single-material infos
    //   pushGLTFBatchesIndexed -- renders only multi-material infos (indexed program bound)
    void pushGLTFBatchesScalar(U32 type);
    void pushGLTFBatchesIndexed(U32 type, eGLTFIndexedMaps maps = GLTF_MAPS_FULL);
    static void pushGLTFBatchIndexed(LLDrawInfo& params, eGLTFIndexedMaps maps = GLTF_MAPS_FULL);

    // like pushGLTFBatches, but will not bind textures or set up texture transforms
    void pushUntexturedGLTFBatches(U32 type);

    // helper function for dispatching to textured or untextured pass based on bool
    void pushGLTFBatches(U32 type, bool textured);


    // rigged variants of above
    void pushRiggedGLTFBatches(U32 type);
    void pushRiggedGLTFBatches(U32 type, bool textured);
    void pushUntexturedRiggedGLTFBatches(U32 type);

    // rigged indexed/scalar split (see pushGLTFBatchesScalar/Indexed); each batch
    // is one avatar+skin (the accumulation breaks on skin change), so the matrix
    // palette is uploaded per draw info as usual.
    void pushRiggedGLTFBatchesScalar(U32 type);
    void pushRiggedGLTFBatchesIndexed(U32 type, eGLTFIndexedMaps maps = GLTF_MAPS_FULL);
    static void pushRiggedGLTFBatchIndexed(LLDrawInfo& params, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin, eGLTFIndexedMaps maps = GLTF_MAPS_FULL);

    // push a single GLTF draw call
    // lastMat/lastTex track the most recently bound material+media texture so
    // consecutive draws sharing a material skip the redundant LLFetchedGLTFMaterial::bind
    static void pushGLTFBatch(LLDrawInfo& params, LLFetchedGLTFMaterial*& lastMat, LLViewerTexture*& lastTex);
    static void pushRiggedGLTFBatch(LLDrawInfo& params, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin, LLFetchedGLTFMaterial*& lastMat, LLViewerTexture*& lastTex);
    static void pushUntexturedGLTFBatch(LLDrawInfo& params);
    static void pushUntexturedRiggedGLTFBatch(LLDrawInfo& params, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin);

    void pushMaskBatches(U32 type, bool texture = true, bool batch_textures = false);
    void pushRiggedMaskBatches(U32 type, bool texture = true, bool batch_textures = false);
    // indexed (multi-material) legacy material shadow alpha-mask: binds per-slot
    // diffuse + per-slot cutoff array, then draws. Assumes the indexed material
    // shadow program is bound.
    void pushMaskBatchesIndexed(U32 type, bool rigged);

    // Emissive/glow indexed split. Multi-material (indexed) glow batches coexist
    // with scalar/plain ones in PASS_GLOW, distinguished by mMaterialSlotList.size()
    // > 1. The scalar sweep skips multi-material infos (they would render the whole
    // range with slot 0's diffuse); the indexed sweep binds each slot's diffuse to
    // unit s and draws under the indexed emissive program.
    void pushEmissiveBatchesScalar(U32 type, bool rigged);
    void pushEmissiveBatchesIndexed(U32 type, bool rigged);
    void pushBatch(LLDrawInfo& params, bool texture, bool batch_textures = false);
    void pushUntexturedBatch(LLDrawInfo& params);
    void pushBumpBatch(LLDrawInfo& params, bool texture, bool batch_textures = false);
    static bool uploadMatrixPalette(LLDrawInfo& params);
    static bool uploadMatrixPalette(LLVOAvatar* avatar, LLMeshSkinInfo* skinInfo);
    static bool uploadMatrixPalette(LLVOAvatar* avatar, LLMeshSkinInfo* skinInfo, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin);
    static bool uploadMatrixPalette(LLVOAvatar* avatar, LLMeshSkinInfo* skinInfo, const LLVOAvatar*& lastAvatar, U64& lastMeshId, const LLGLSLShader*& lastAvatarShader, bool& skipLastSkin);
    virtual void renderGroup(LLSpatialGroup* group, U32 type, bool texture = true);
    virtual void renderRiggedGroup(LLSpatialGroup* group, U32 type, bool texture = true);
};

class LLFacePool : public LLDrawPool
{
public:
    typedef std::vector<LLFace*> face_array_t;

    enum
    {
        SHADER_LEVEL_SCATTERING = 2
    };

public:
    LLFacePool(const U32 type);
    virtual ~LLFacePool();

    bool isDead() { return mReferences.empty(); }

    virtual LLViewerTexture *getTexture();
    virtual void dirtyTextures(const std::set<LLViewerFetchedTexture*>& textures);

    virtual void enqueue(LLFace *face);
    virtual bool addFace(LLFace *face);
    virtual bool removeFace(LLFace *face);

    virtual bool verify() const;        // Verify that all data in the draw pool is correct!

    virtual void resetDrawOrders();
    void resetAll();

    void destroy();

    void buildEdges();

    void addFaceReference(LLFace *facep);
    void removeFaceReference(LLFace *facep);

    void printDebugInfo() const;

    bool isFacePool() { return true; }

    // call drawIndexed on every draw face
    void pushFaceGeometry();

    friend class LLFace;
    friend class LLPipeline;
public:
    face_array_t    mDrawFace;
    face_array_t    mMoveFace;
    face_array_t    mReferences;

public:
    class LLOverrideFaceColor
    {
    public:
        LLOverrideFaceColor(LLDrawPool* pool)
            : mOverride(sOverrideFaceColor), mPool(pool)
        {
            sOverrideFaceColor = true;
        }
        LLOverrideFaceColor(LLDrawPool* pool, const LLColor4& color)
            : mOverride(sOverrideFaceColor), mPool(pool)
        {
            sOverrideFaceColor = true;
            setColor(color);
        }
        LLOverrideFaceColor(LLDrawPool* pool, const LLColor4U& color)
            : mOverride(sOverrideFaceColor), mPool(pool)
        {
            sOverrideFaceColor = true;
            setColor(color);
        }
        LLOverrideFaceColor(LLDrawPool* pool, F32 r, F32 g, F32 b, F32 a)
            : mOverride(sOverrideFaceColor), mPool(pool)
        {
            sOverrideFaceColor = true;
            setColor(r, g, b, a);
        }
        ~LLOverrideFaceColor()
        {
            sOverrideFaceColor = mOverride;
        }
        void setColor(const LLColor4& color);
        void setColor(const LLColor4U& color);
        void setColor(F32 r, F32 g, F32 b, F32 a);
        bool mOverride;
        LLDrawPool* mPool;
        static bool sOverrideFaceColor;
    };
};


#endif //LL_LLDRAWPOOL_H
