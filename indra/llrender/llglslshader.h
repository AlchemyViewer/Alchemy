/**
 * @file llglslshader.h
 * @brief GLSL shader wrappers
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLGLSLSHADER_H
#define LL_LLGLSLSHADER_H

#include "llgl.h"
#include "llrender.h"
#include "llsd.h"
#include "llstaticstringtable.h"
#include <boost/unordered_map.hpp>

class LLShaderFeatures
{
public:
    S32 mIndexedTextureChannels = 0;
    bool calculatesLighting = false;
    bool calculatesAtmospherics = false;
    bool hasLighting = false; // implies no transport (it's possible to have neither though)
    bool isAlphaLighting = false; // indicates lighting shaders need not be linked in (lighting performed directly in alpha shader to match deferred lighting functions)
    bool isSpecular = false;
    bool hasTransport = false; // implies no lighting (it's possible to have neither though)
    bool hasSkinning = false;
    bool hasObjectSkinning = false;
    bool mGLTF = false;
    bool hasAtmospherics = false;
    bool hasGamma = false;
    bool hasShadows = false;
    bool hasAmbientOcclusion = false;
    bool hasSrgb = false;
    bool isDeferred = false;
    bool hasFullGBuffer = false;
    bool hasScreenSpaceReflections = false;
    bool hasAlphaMask = false;
    bool hasReflectionProbes = false;
    bool attachNothing = false;
    bool hasHeroProbes = false;
    bool isPBRTerrain = false;
    bool hasTonemap = false;
    bool hasColorGrade = false;
    bool hasPostEffects = false;
};

// ============= Structure for caching shader uniforms ===============
class LLGLSLShader;

class LLShaderUniforms
{
public:

    template<typename T>
    struct UniformSetting
    {
        S32 mUniform{ 0 };
        T mValue{};
    };

    typedef UniformSetting<S32> IntSetting;
    typedef UniformSetting<F32> FloatSetting;
    typedef UniformSetting<LLVector4> VectorSetting;
    typedef UniformSetting<LLVector3> Vector3Setting;

    void clear()
    {
        mIntegers.resize(0);
        mFloats.resize(0);
        mVectors.resize(0);
        mVector3s.resize(0);
    }

    void uniform1i(S32 index, S32 value)
    {
        mIntegers.push_back({ index, value });
    }

    void uniform1f(S32 index, F32 value)
    {
        mFloats.push_back({ index, value });
    }

    void uniform4fv(S32 index, const LLVector4& value)
    {
        mVectors.push_back({ index, value });
    }

    void uniform4fv(S32 index, const F32* value)
    {
        mVectors.push_back({ index, LLVector4(value) });
    }

    void uniform3fv(S32 index, const LLVector3& value)
    {
        mVector3s.push_back({ index, value });
    }

    void uniform3fv(S32 index, const F32* value)
    {
        mVector3s.push_back({ index, LLVector3(value) });
    }

    void apply(LLGLSLShader* shader);


    std::vector<IntSetting> mIntegers;
    std::vector<FloatSetting> mFloats;
    std::vector<VectorSetting> mVectors;
    std::vector<Vector3Setting> mVector3s;
};
class LLGLSLShader
{
public:
    // NOTE: Keep gShaderConsts and LLGLSLShader::ShaderConsts_e in sync!
    enum eShaderConsts
    {
        SHADER_CONST_CLOUD_MOON_DEPTH
        , SHADER_CONST_STAR_DEPTH
        , NUM_SHADER_CONSTS
    };

    // enum primarily used to control application sky settings uniforms
    typedef enum
    {
        SG_DEFAULT = 0,  // not sky or water specific
        SG_SKY,  //
        SG_WATER,
        SG_ANY,
        SG_COUNT
    } eGroup;

    // Indexed uniform-buffer binding points the engine attaches its shared constant blocks to
    // (glBindBufferBase); every consumer program references the block at the SAME point, so they
    // are a fixed cross-program contract. Programs are remapped onto them by NAME after link
    // (mapUniforms) rather than by an in-source `layout(binding=)`, which needs GLSL 4.20.
    enum UniformBlock : GLuint
    {
        UB_REFLECTION_PROBES,   // "ReflectionProbes"
        UB_GLTF_JOINTS,         // "GLTFJoints"
        UB_GLTF_NODES,          // "GLTFNodes"
        UB_GLTF_MATERIALS,      // "GLTFMaterials"
        UB_ENVIRONMENT,         // "Environment" (sky/water constants)
        UB_DEFERRED,            // "Deferred" (shadow/SSAO constants)
        UB_LIGHTS,              // "Lights" (forward fixed-function light array state)
        UB_MATRICES,            // "Matrices" (modelview/projection/derived matrix stack state)
        NUM_UNIFORM_BLOCKS
    };

    // GL block names for the engine uniform blocks, in UniformBlock enum order (index ==
    // binding). The single source of these names: read by the post-link remap in mapUniforms
    // and by the debug layout validator. Keep in lockstep with the enum and the GLSL blocks.
    static constexpr const char* UNIFORM_BLOCK_NAMES[NUM_UNIFORM_BLOCKS] =
    {
        "ReflectionProbes", // UB_REFLECTION_PROBES
        "GLTFJoints",       // UB_GLTF_JOINTS
        "GLTFNodes",        // UB_GLTF_NODES
        "GLTFMaterials",    // UB_GLTF_MATERIALS
        "Environment",      // UB_ENVIRONMENT
        "Deferred",         // UB_DEFERRED
        "Lights",           // UB_LIGHTS
        "Matrices",         // UB_MATRICES
    };

    // Expected std140 layout of an engine UBO block, registered by the module that owns the
    // C++ mirror struct with offsetof-derived offsets -- so the debug validator and the pack
    // code can never drift apart. mReservedUniform indexes LLShaderMgr::mReservedUniforms for
    // the GLSL member name; mName overrides it for members that aren't reserved uniforms.
    // mMatrix marks a matrix member, which is additionally required to introspect
    // COLUMN-major: std140's default, and what the pack code writes (glm's own storage,
    // uploaded straight through). A row-major layout would silently transpose every read.
#if !LL_RELEASE_FOR_DOWNLOAD
    struct EngineBlockLayoutMember
    {
        S32         mReservedUniform;
        const char* mName;
        U32         mOffset;
        bool        mMatrix;
    };
    // Called from the owning module's static initializer to describe what the C++ upload
    // expects; validated against every linked program at load.
    //
    // Debug-only, declaration included: the registered layouts are read by exactly one
    // consumer (validateEngineBlockLayouts), which is itself debug-only, so in a release build
    // the registrations were pure cost. The CALL SITES are gated to match; adding another
    // engine block means gating its registration too.
    static void registerEngineBlockLayout(const char* block_name, std::vector<EngineBlockLayoutMember> members);
#endif // !LL_RELEASE_FOR_DOWNLOAD

    // An active uniform read back from the linked program, plus the texture-unit
    // ordering used to assign sampler channels deterministically (see mapUniforms).
    struct gl_uniform_data_t
    {
        std::string name;
        GLenum type = (GLenum)-1;
        GLint size = -1;
        U32 texunit_priority = UINT_MAX; // lower value gets an earlier texture-unit index
    };


    // Every program that created successfully. Walked for name uniqueness and by unloadShaders();
    // entries are removed by unloadInternal() and by the destructor.
    //
    // A reference to a deliberately leaked set rather than a set. Programs are non-local statics
    // in other translation units and leave this registry from their destructors, and destruction
    // order across translation units is unspecified -- a registry that destroyed itself could be
    // gone before the last program left it. One empty set at process exit is the cheaper half of
    // that trade. Nothing touches this before dynamic initialisation finishes: entries only ever
    // arrive from createShader(), which needs a GL context.
    static std::set<LLGLSLShader*>& sInstances;
    static bool sProfileEnabled;
    static bool sCanProfile;

    LLGLSLShader();
    ~LLGLSLShader();

    static GLuint sCurBoundShader;
    static LLGLSLShader* sCurBoundShaderPtr;
    static S32 sIndexedTextureChannels;

    // Units currently holding a depth texture under a compare sampler, as a bitmask by
    // unit. LLPipeline::bindShadowMaps -- the only compare-sampler bind in the tree --
    // publishes its units here, and bind() releases them before any program that does not
    // declare the shadow samplers runs: a compare-mode depth read through a plain
    // sampler2D is undefined even where the shader's dynamic branching never reaches the
    // unit. Programs that DO declare them are relayouted by the next bindShadowMaps
    // instead, so the deferred family pays nothing here.
    static U32 sCompareSamplerUnits;
    // Unbind the texture AND the sampler on every published unit. The sampler must go
    // too: unbind() leaves the white placeholder on the unit, which IS sampleable, and
    // reading it through depth comparison is undefined.
    static void releaseCompareSamplerUnits();
    // Does this program declare any of the shadow-map samplers (shadowMap0..5)? Computed once
    // in mapUniforms(); bind() asks on every program switch while the maps are bound.
    bool declaresShadowSamplers() const { return mDeclaresShadowSamplers; }
    // Hard ceiling on the indexed-GLTF slot count. setShaders() clamps
    // sIndexedGLTFChannels to this, and every fixed per-slot array that walks a batch is
    // sized from it. Those two things MUST agree: the arrays are indexed by slot up to
    // sIndexedGLTFChannels, so a clamp raised past the array width overruns them. One
    // constant so raising the ceiling moves both together instead of relying on matching
    // literals in four files.
    static constexpr S32 MAX_INDEXED_GLTF_CHANNELS = 8;

    // Hard ceiling on the indexed-texture ladder width (tex0..texN). Three things must
    // agree on this number: the tex%d samplers the indexed sources declare, the
    // texture_list[] genDrawInfo batches into, and sIndexedTextureChannels. It is a
    // declaration ceiling, not a budget -- the affordable width is a device+shader
    // property computed in setShaders().
    static constexpr S32 MAX_BATCH_TEXTURE_COUNT = 32;

    // Number of GLTF PBR materials that can be batched into one indexed draw call.
    // Each material consumes four texture units (base color, normal, ORM, emissive),
    // so this is roughly (available fragment texture units) / 4, clamped to
    // MAX_INDEXED_GLTF_CHANNELS. See PASS_GLTF_PBR_INDEXED and
    // LLVolumeGeometryManager::genDrawInfo.
    static S32 sIndexedGLTFChannels;

    // True once the indexed legacy (Blinn-Phong) material programs have loaded.
    // Gates indexed POOL_MATERIALS batching (shares sIndexedGLTFChannels for the
    // per-slot stride); independent so a material-shader failure doesn't disable PBR.
    static bool sIndexedLegacyMaterials;

    static U32 sMaxGLTFMaterials;
    static U32 sMaxGLTFNodes;

    static void initProfile();
    static void finishProfile(LLSD& stats=sDefaultStats);

    static void startProfile();
    static void stopProfile();

    void unload();
    void clearStats();
    void dumpStats(LLSD& stats);

    // place query objects for profiling if profiling is enabled
    // if for_runtime is true, will place timer query only whether or not profiling is enabled
    void placeProfileQuery(bool for_runtime = false);

    // Readback query objects if profiling is enabled
    // If for_runtime is true, will readback timer query iff query is available
    // Will return false if a query is pending (try again later)
    // If force_read is true, will force an immediate readback (severe performance penalty)
    bool readProfileQuery(bool for_runtime = false, bool force_read = false);

    // Compile-time variant axes, passed to createShader() as a mask. They are built in this
    // order because each composes over the ones before it: a program asking for all three gets
    // the full rigged x classic x mirror corner set, every corner reachable via
    // selectVariant()->bind(rigged). Each corner is derived from this program's config plus the
    // axis defines, so nothing depends on a sibling program being compiled first.
    enum EVariant : U32
    {
        VARIANT_RIGGED  = 1 << 0, // HAS_SKIN=1     -- skinned/animesh  (per-DRAW, via bind(rigged))
        VARIANT_CLASSIC = 1 << 1, // CLASSIC_MODE=1 -- classic sky lighting     (per-PASS)
        VARIANT_MIRROR  = 1 << 2, // MIRROR_CLIP=1  -- hero-probe mirror clip   (per-PASS)
    };

    // Compile this program and, on success, build the requested variant subtrees (owned by this
    // program; freed by unload()). Any previously built variants are dropped first, since
    // recreating the program invalidates them. Returns false if the program OR any requested
    // variant fails; a failed axis leaves its pointer null.
    bool createShader(U32 variants = 0);
    bool attachFragmentObject(std::string object);
    bool attachVertexObject(std::string object);
    void attachObject(GLuint object);
    void attachObjects(GLuint* objects = NULL, S32 count = 0);
    bool mapAttributes();
    bool mapUniforms();
    void mapUniform(const gl_uniform_data_t& gl_uniform);
    void uniform1i(U32 index, GLint i);
    void uniform1f(U32 index, GLfloat v);
    void fastUniform1f(U32 index, GLfloat v);
    void uniform1ui(U32 index, GLuint i);
    void uniform2f(U32 index, GLfloat x, GLfloat y);
    void uniform3f(U32 index, GLfloat x, GLfloat y, GLfloat z);
    void uniform4f(U32 index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
    void uniform1iv(U32 index, U32 count, const GLint* i);
    void uniform4iv(U32 index, U32 count, const GLint* i);
    void uniform1fv(U32 index, U32 count, const GLfloat* v);
    void uniform2fv(U32 index, U32 count, const GLfloat* v);
    void uniform3fv(U32 index, U32 count, const GLfloat* v);
    void uniform4fv(U32 index, U32 count, const GLfloat* v);
    void fastUniform4fv(U32 index, U32 count, const GLfloat* v);
    void uniform4uiv(U32 index, U32 count, const GLuint* v);
    void uniformMatrix2fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v);
    void uniformMatrix3fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v);
    void uniformMatrix3x4fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v);
    void uniformMatrix4fv(U32 index, U32 count, GLboolean transpose, const GLfloat* v);
    void uniform1i(const LLStaticHashedString& uniform, GLint i);

    void setMinimumAlpha(F32 minimum);

    void vertexAttrib4f(U32 index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
    void vertexAttrib4fv(U32 index, GLfloat* v);

    //GLint getUniformLocation(const std::string& uniform);
    GLint getUniformLocation(const LLStaticHashedString& uniform);
    GLint getUniformLocation(U32 index);

    // True if the reserved uniform is present, whether as a default-block
    // uniform (real location) or a Slang $Globals block member (UBO-backed).
    // Use this instead of getUniformLocation()>-1 for presence checks that then
    // set via the uniform*() setters, so block-resident uniforms aren't skipped.
    bool hasUniform(U32 index) const;

    GLint getAttribLocation(U32 attrib);
    GLint mapUniformTextureChannel(GLint location, GLenum type, GLint size);

    void clearPermutations();
    void addPermutation(std::string name, std::string value);
    void addPermutations(const std::map<std::string, std::string>& defines)
    {
        mDefines.insert(defines.begin(), defines.end());
        mPermutationsAdded = true;
    }
    void removePermutation(std::string name);

    void addConstant(const LLGLSLShader::eShaderConsts shader_const);

    // Texture channel for the specified uniform, or -1 if the uniform is not used as a
    // texture. enableTexture only resolves it; disableTexture also releases the slot, and its
    // `mode` is the caller's expected target, checked against what was actually bound there
    // when gDebugGL is on.
    S32 enableTexture(S32 uniform);
    S32 disableTexture(S32 uniform, ALTextureSlot::eTextureType mode = ALTextureSlot::TT_TEXTURE);

    // get the texture channel of the given uniform, or -1 if uniform is not used as a texture
    S32 getTextureChannel(S32 uniform) const;

    // bindTexture returns the texture unit we've bound the texture to.
    // You can reuse the return value to unbind a texture when required.
    //
    // The sampler is NAMED BY THE CALLER, and there is no overload that infers it. How a pass
    // reads an image is a property of the pass: two materials can reference one texture and
    // want it read differently, so a texture that decided its own filtering would have to pick
    // a winner and be wrong for the other. The inferring form existed until every call site
    // had chosen; it was deleted once that count reached zero, so it cannot come back by habit.
    S32 bindTexture(S32 uniform, LLTexture* texture, ALSampler key);
    // An unnamed key resolves to the target's per-attachment default (bilinear for
    // attachment 0, point for data attachments) -- see LLRenderTarget::bindTexture.
    S32 bindTexture(S32 uniform, LLRenderTarget* texture, ALSampler key = ALSamplers::TargetDefault, U32 index = 0);

    // Colour and depth attachments are separate calls rather than one with a bool.
    //
    // They were one, and the bool sat third -- exactly where a filter reads naturally. The
    // legacy filter enum is unscoped and TFO_POINT is 0, so four post-process call sites wrote
    // bindTexture(uniform, target, LLTexUnit::TFO_POINT), meaning "sample this point-filtered",
    // and silently got depth=false plus the DEFAULT bilinear filter. It compiled, it ran, and
    // the depth-of-field passes had been reading bilinear for as long as the call existed.
    // Splitting the two removes the parameter that made that expressible.
    // Point by default, matching LLRenderTarget::getDefaultDepthSampler: linear filtering a
    // DEPTH_COMPONENT texture without compare mode is implementation-defined, and at
    // silhouette edges it averages foreground and background into a depth no surface has.
    S32 bindDepthTexture(S32 uniform, LLRenderTarget* texture, ALSampler key = ALSamplers::PointClamp);
    S32 unbindTexture(S32 uniform);

    bool link(bool suppress_errors = false);
    void bind();
    //helper to conditionally bind mRiggedVariant instead of this
    void bind(bool rigged);

    bool isComplete() const { return mProgramObject != 0; }

    LLUUID hash();

    // Unbinds any previously bound shader by explicitly binding no shader.
    static void unbind();

    U32 mLightHash;

    GLuint mProgramObject;
#if LL_DEBUG || LL_RELEASE_WITH_DEBUG_INFO
    struct attr_name
    {
        GLint loc;
        const char* name;
        void operator = (GLint _loc) { loc = _loc; }
        operator GLint () { return loc; }
    };
    std::vector<attr_name> mAttribute; //lookup table of attribute enum to attribute channel
#else
    std::vector<GLint> mAttribute; //lookup table of attribute enum to attribute channel
#endif
    U32 mAttributeMask;  //mask of which reserved attributes are set (lines up with LLVertexBuffer::getTypeMask())
    std::vector<GLint> mUniform;   //lookup table of uniform enum to uniform location
    LLStaticStringTable<GLint> mUniformMap; //lookup map of uniform name to uniform location
    typedef boost::unordered_map<GLint, LLVector4>  uniform_value_map_t;
    uniform_value_map_t mValue; //lookup map of uniform location to last known value
    std::vector<GLint> mTexture;
    // Last value routed to MINIMUM_ALPHA on this program, so setMinimumAlpha can skip the
    // batch flush and the whole uniform path when unchanged (the alpha pool re-applies it
    // per batch; GLTF material binds re-push it per material). EVERY writer of that
    // uniform must go through setMinimumAlpha to keep this authoritative -- do not write
    // it via uniform1f/fastUniform1f directly. -1 is a live value (GLTF "no masking"),
    // so the unset sentinel sits far outside the usable range; reset alongside mValue
    // (mapUniforms/unloadInternal), since both shadow GPU-side program state.
    static constexpr F32 MINIMUM_ALPHA_UNSET = -1e30f;
    F32 mMinimumAlpha = MINIMUM_ALPHA_UNSET;

    S32 mTotalUniformSize;
    S32 mActiveTextureChannels;
    S32 mShaderLevel;
    S32 mShaderGroup; // see LLGLSLShader::eGroup
    // environment-uniform generation this program last applied; see sEnvironmentGeneration.
    // Starts LEVEL with it, never behind -- a program built before LLEnvironment exists must
    // not try to apply an environment on its first bind.
    U32 mEnvUniformsGeneration = 1;
    // cached in mapUniforms(); see declaresShadowSamplers()
    bool mDeclaresShadowSamplers = false;
    LLShaderFeatures mFeatures;
    std::vector< std::pair< std::string, GLenum > > mShaderFiles;
    std::string mName;
    typedef std::map<std::string, std::string> defines_map_t; //NOTE: this must be an ordered map to maintain hash consistency
    defines_map_t mDefines;
    // set by addPermutation(s) and cleared by createShader()/unload(); see clearPermutations()
    bool mPermutationsAdded = false;
    static defines_map_t sGlobalDefines;
    LLUUID mShaderHash;
    bool mUsingBinaryProgram = false;

    //statistics for profiling shader performance
    bool mProfilePending = false;
    U32 mTimerQuery;
    U32 mSamplesQuery;
    U32 mPrimitivesQuery;

    U64 mTimeElapsed;
    static U64 sTotalTimeElapsed;
    U32 mTrianglesDrawn;
    static U32 sTotalTrianglesDrawn;
    U64 mSamplesDrawn;
    static U64 sTotalSamplesDrawn;
    U32 mBinds;
    static U32 sTotalBinds;

    // This program expects its diffuse/base-colour texture already LINEAR: the bind decodes
    // sRGB on the sampler (ALSampler::SRGBDecode), the shader has dropped its own
    // srgb_to_linear on the diffuse, and its vertex-colour tint is linearised in the vertex
    // stage. Set on the forward writers that were converted to filter albedo in linear space
    // -- see the pool bind sites (bindIndexedTextures, pushBatch, LLDrawPoolAlpha::TexSetup),
    // which read it to choose the diffuse sampler for a polymorphic per-draw shader. Left
    // false on the HUD programs (they output sRGB) and the FOR_IMPOSTOR alpha program (it
    // writes the sRGB sample straight to the bake).
    //
    // DERIVED in createShader() from the LINEAR_DIFFUSE permutation -- the same define the
    // shader source keys its colour-space guards on -- so engine and compiled code cannot
    // disagree. Add the permutation; never assign this.
    bool mLinearDiffuse = false;

    // Compile-time variant axes:
    //   mRiggedVariant  -- HAS_SKIN=1     (per-DRAW; chosen via bind(rigged))
    //   mClassicVariant -- CLASSIC_MODE=1 (per-PASS; classic sky lighting active)
    //   mMirrorVariant  -- MIRROR_CLIP=1  (per-PASS; hero-probe mirror clip active)
    // The per-pass variants carry their own rigged (and each other's) sub-variants, so a single
    // selectVariant() walk reaches any rigged x classic x mirror corner.
    //
    // OWNERSHIP: createShader()'s variant mask is the only thing that allocates one, and it
    // heap-allocates, so a program owns its whole variant subtree -- unload() frees it via
    // freeVariant(). mOwnedVariant records that; it exists because these members are public, so
    // pointing one at a global instead would make freeVariant() delete a static object. A
    // skinned program is its own rigged variant (a self-edge set by createShader), which
    // freeVariant() and forEachVariant() must not traverse.
    LLGLSLShader* mRiggedVariant = nullptr;
    LLGLSLShader* mClassicVariant = nullptr;
    LLGLSLShader* mMirrorVariant = nullptr;

    // true when this program was heap-allocated as a variant and is owned by its parent
    bool mOwnedVariant = false;

    // Release a variant reference held by this program: unload + delete it when this program
    // owns it, otherwise just clear the pointer. Idempotent, and recurses through the subtree
    // (see unload()). Never frees through a self-edge.
    void freeVariant(LLGLSLShader*& variant);

    // drop this program's whole owned variant subtree (see unload() / createShader())
    void freeOwnedVariants();

    // Apply `fn` to this program and every owned variant, depth-first. The subtree is a tree
    // (no corner is shared), so each program is visited exactly once -- use this for anything
    // that must reach every corner, e.g. post-createShader feature tweaks or sampler unit maps.
    // NOTE: skips a self-referencing child; recursing into that edge would never terminate.
    template <typename FUNC>
    void forEachVariant(FUNC fn)
    {
        fn(*this);
        if (mRiggedVariant  && mRiggedVariant  != this) { mRiggedVariant->forEachVariant(fn); }
        if (mClassicVariant && mClassicVariant != this) { mClassicVariant->forEachVariant(fn); }
        if (mMirrorVariant  && mMirrorVariant  != this) { mMirrorVariant->forEachVariant(fn); }
    }

    // Returns the variant to bind for the current global render-pass state, composing the
    // per-PASS compile-time axes in a fixed order: the MIRROR_CLIP clone during the mirror
    // pass, then the CLASSIC_MODE clone under classic sky lighting. An axis that is inactive
    // or has no variant is a no-op, so this is safe on every program (including those with no
    // variants at all). The per-DRAW rigged axis is applied afterward on the returned pointer
    // via bind(rigged) / ->mRiggedVariant. Call before bind(); route all subsequent
    // uniform*/bindTexture calls through the returned pointer.
    LLGLSLShader* selectVariant()
    {
        LLGLSLShader* s = this;
        if (LLRender::sMirrorPass  && s->mMirrorVariant)  s = s->mMirrorVariant;
        if (LLRender::sClassicMode && s->mClassicVariant) s = s->mClassicVariant;
        return s;
    }

    // Invalidate the shared environment (sky/water) uniform set for every live program; each
    // re-applies on its next bind -- see the definition
    static void dirtyEnvironmentUniforms();

    // Generation of the shared environment uniform set. LLEnvironment bumps it via
    // dirtyEnvironmentUniforms(); a program re-applies on bind when its own generation is
    // behind. A counter rather than a per-program dirty flag so the per-frame environment
    // update stays O(1) and reaches programs no list happens to hold. A new program starts
    // level with it, never behind -- see createShader().
    static U32 sEnvironmentGeneration;



    // hacky flag used for optimization in LLDrawPoolAlpha
    bool mCanBindFast = false;

#if LL_PROFILER_ENABLE_RENDER_DOC
    void setLabel(const char* label);
#endif

private:
    void unloadInternal();

    // ---- compile-time variant construction (see EVariant / createShader) ----
    // build one axis's corner set into the matching member; see the definition
    bool createVariant(EVariant axis);
    // heap-allocate one corner: this program's config plus the requested defines, compiled
    LLGLSLShader* makeVariantCorner(const std::string& name, const char* perm_key,
                                    bool add_classic, bool add_rigged) const;
    // copy this program's whole configuration into `dst` under `name` (no defines added)
    void configureVariantClone(LLGLSLShader& dst, const std::string& name) const;

    // gDebugGL-only: warn when an active per-pass axis had a corner this bind did not select
    void warnIfVariantMissed() const;

    // This must be static because finishProfile() is called at least once
    // within a __try block. If we default its stats parameter to a temporary
    // LLSD, that temporary must be destroyed when the stack is unwound,
    // which __try forbids.
    static LLSD sDefaultStats;
};

//UI shader (declared here so llui_libtest will link properly)
extern LLGLSLShader         gUIProgram;
//output vec4(color.rgb,color.a*tex0[tc0].a)
extern LLGLSLShader         gSolidColorProgram;
//Alpha mask shader (declared here so llappearance can access properly)
extern LLGLSLShader         gAlphaMaskProgram;

#if LL_PROFILER_ENABLE_RENDER_DOC
#define LL_SET_SHADER_LABEL(shader) shader.setLabel(#shader)
#else
#define LL_SET_SHADER_LABEL(shader, label)
#endif

#endif
