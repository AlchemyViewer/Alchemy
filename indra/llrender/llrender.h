/**
 * @file llrender.h
 * @brief LLRender definition
 *
 *  This class acts as a wrapper for OpenGL calls.
 *  The goal of this class is to minimize the number of api calls due to legacy rendering
 *  code, to define an interface for a multiple rendering API abstraction of the UI
 *  rendering, and to abstract out direct rendering calls in a way that is cleaner and easier to maintain.
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

#ifndef LL_LLGLRENDER_H
#define LL_LLGLRENDER_H

//#include "linden_common.h"

#include "v2math.h"
#include "v3math.h"
#include "v4coloru.h"
#include "v4math.h"
#include "llstrider.h"
#include "llpointer.h"
#include "llglheaders.h"
#include "llmatrix4a.h"
#include "alsamplerstate.h"  // mSamplerCache -- this context's sampler objects
#include "altextureslot.h"
#include "aluniformbuffer.h"
#include "glm/mat4x4.hpp"

#include <boost/unordered_map.hpp>

#include <array>
#include <chrono>
#include <list>
#include <vector>

class LLVertexBuffer;
class LLCubeMap;
class LLImageGL;
class LLRenderTarget;
class LLTexture;
class LLVertexBufferData;

#define LL_MATRIX_STACK_DEPTH 32

// AL_NUM_TEXTURE_SLOTS lives in altextureslot.h alongside the units it sizes.
constexpr U32 LL_NUM_LIGHT_UNITS = 8;

class LLLightState
{
public:
    LLLightState(S32 index = -1);

    void enable();
    void disable();
    void setDiffuse(const LLColor4& diffuse);
    void setDiffuseB(const LLColor4& diffuse);
    void setAmbient(const LLColor4& ambient);
    void setSpecular(const LLColor4& specular);
    void setPosition(const LLVector4& position);
    void setConstantAttenuation(const F32& atten);
    void setLinearAttenuation(const F32& atten);
    void setQuadraticAttenuation(const F32& atten);
    void setSpotExponent(const F32& exponent);
    void setSpotCutoff(const F32& cutoff);
    void setSpotDirection(const LLVector3& direction);
    void setSunPrimary(bool v);
    void setSize(F32 size);
    void setFalloff(F32 falloff);

protected:
    friend class LLRender;

    S32 mIndex;
    bool mEnabled;
    LLColor4 mDiffuse;
    LLColor4 mDiffuseB;
    bool     mSunIsPrimary;
    LLColor4 mAmbient;
    LLColor4 mSpecular;
    LLVector4 mPosition;
    LLVector3 mSpotDirection;

    F32 mConstantAtten;
    F32 mLinearAtten;
    F32 mQuadraticAtten;

    F32 mSpotExponent;
    F32 mSpotCutoff;
    F32 mSize = 0.f;
    F32 mFalloff = 0.f;
};

class LLRender
{
    friend class ALTextureSlot;
public:

    enum eTexIndex : U8
    {
        // Channels for material textures
        DIFFUSE_MAP            = 0,
        ALTERNATE_DIFFUSE_MAP  = 1,
        NORMAL_MAP             = 1,
        SPECULAR_MAP           = 2,
        // Channels for PBR textures
        BASECOLOR_MAP          = 3,
        METALLIC_ROUGHNESS_MAP = 4,
        GLTF_NORMAL_MAP        = 5,
        EMISSIVE_MAP           = 6,
        // Total number of channels
        NUM_TEXTURE_CHANNELS   = 7,
    };

    enum eVolumeTexIndex : U8
    {
        LIGHT_TEX = 0,
        SCULPT_TEX,
        NUM_VOLUME_TEXTURE_CHANNELS,
    };

    enum eGeomModes : U8
    {
        TRIANGLES = 0,
        TRIANGLE_STRIP,
        TRIANGLE_FAN,
        POINTS,
        LINES,
        LINE_STRIP,
        LINE_LOOP,
        NUM_MODES
    };

    enum eCompareFunc : U8
    {
        CF_NEVER = 0,
        CF_ALWAYS,
        CF_LESS,
        CF_LESS_EQUAL,
        CF_EQUAL,
        CF_NOT_EQUAL,
        CF_GREATER_EQUAL,
        CF_GREATER,
        CF_DEFAULT
    };

    enum eBlendType : U8
    {
        BT_ALPHA = 0,
        BT_ADD,
        BT_ADD_WITH_ALPHA,  // Additive blend modulated by the fragment's alpha.
        BT_MULT,
        BT_MULT_ALPHA,
        BT_MULT_X2,
        BT_REPLACE
    };

    // WARNING:  this MUST match the LL_PART_BF enum in LLPartData, so set values explicitly in case someone
    // decides to add more or reorder them
    enum eBlendFactor : U8
    {
        BF_ONE = 0,
        BF_ZERO = 1,
        BF_DEST_COLOR = 2,
        BF_SOURCE_COLOR = 3,
        BF_ONE_MINUS_DEST_COLOR = 4,
        BF_ONE_MINUS_SOURCE_COLOR = 5,
        BF_DEST_ALPHA = 6,
        BF_SOURCE_ALPHA = 7,
        BF_ONE_MINUS_DEST_ALPHA = 8,
        BF_ONE_MINUS_SOURCE_ALPHA = 9,
        BF_UNDEF
    };

    // One texture matrix, not four, and no MM_TEXTURE.
    //
    // MM_TEXTURE used to resolve to MM_TEXTURE0 + getCurrentTexUnitIndex(), making the active
    // texture unit an implicit argument that selected which stack a caller wrote. That was a
    // silent trap rather than a feature: no shader in the tree declares texture_matrix1/2/3
    // (they were dropped during the Slang port), so a resolution landing anywhere but stack 0
    // wrote a matrix nothing would ever read -- the texture transform simply vanished, with no
    // error. Call sites defended against it by issuing getTextureSlot(0)->activate() first, which
    // is why that call appeared next to matrixMode all over the draw paths.
    //
    // Measured before removing: 132 of 132 resolutions in a frame landed on unit 0, so the
    // dynamic form had no users at all, and the guarding activate() calls came to 0.8% of
    // issued glActiveTexture. So this is not a performance change -- it deletes a failure mode
    // that could only ever cost correctness.
    //
    // LLShaderMgr::TEXTURE_MATRIX1/2/3 are deliberately left in place: those enum values index
    // mReservedUniforms, and renumbering it would invalidate the on-disk shader reflection
    // cache. They are simply never referenced now.
    enum eMatrixMode : U8
    {
        MM_MODELVIEW = 0,
        MM_PROJECTION,
        MM_TEXTURE0,
        NUM_MATRIX_MODES
    };

    LLRender();
    ~LLRender();
    bool init(bool needs_vertex_buffer);
    void initVertexBuffer();
    void resetVertexBuffer();
    void shutdown();

    // Refreshes renderer state to the cached values
    // Needed when the render context has changed and invalidated the current state
    void refreshState(void);

    void translatef(const GLfloat& x, const GLfloat& y, const GLfloat& z);
    void scalef(const GLfloat& x, const GLfloat& y, const GLfloat& z);
    void rotatef(const GLfloat& a, const GLfloat& x, const GLfloat& y, const GLfloat& z);
    void ortho(F32 left, F32 right, F32 bottom, F32 top, F32 zNear, F32 zFar);

    void pushMatrix();
    void popMatrix();
    void loadMatrix(const GLfloat* m);
    void loadIdentity();
    void multMatrix(const GLfloat* m);
    void matrixMode(eMatrixMode mode);
    eMatrixMode getMatrixMode();

    const glm::mat4& getModelviewMatrix();
    const glm::mat4& getProjectionMatrix();

    void syncMatrices();
    void syncLightState();

    // ---- Shared forward-lighting uniform block (UB_LIGHTS) --------------------------------
    // The fixed-function light arrays every forward/alpha program reads. They used to be
    // pushed as LOOSE uniforms into each program on its first bind after mLightHash moved --
    // the same ~672 bytes re-derived and re-written once per program. Packed once and bound at
    // a fixed engine point instead, so a light-state change costs one upload total.
    //
    // Holds ONLY what LLRender owns exclusively. sun_up_factor and the sClassicMode
    // ambient/sunlight/moonlight overrides stay LOOSE: those names have other writers
    // (LLPipeline::bindDeferredShader pushes an auto-adjusted sunlight_color, and
    // sun_up_factor is written from many sites across the pipeline, draw pools and
    // LLSettingsVO), and a shared block would let one writer silently stomp another's value.
    //
    // std140 pads every array element to 16 bytes regardless of the element type, hence the
    // float[4] rows for the vec3/vec2 arrays -- the C++ mirror carries that padding explicitly
    // so the struct IS the layout. Public so the debug layout registration can offsetof it.
    struct alignas(16) LightsUBOData
    {
        F32 light_position[LL_NUM_LIGHT_UNITS][4];
        F32 light_direction[LL_NUM_LIGHT_UNITS][4];             // .xyz used, .w padding
        F32 light_attenuation[LL_NUM_LIGHT_UNITS][4];
        F32 light_deferred_attenuation[LL_NUM_LIGHT_UNITS][4];  // .xy used (size, falloff)
        F32 light_diffuse[LL_NUM_LIGHT_UNITS][4];               // .rgb used, .a padding
        F32 light_ambient[3];  F32 _tail_pad;
    };

    // ---- Shared matrix uniform block (UB_MATRICES) ----------------------------------------
    // The matrix stack plus the matrices derived from it. Like the Lights block above, these
    // used to be LOOSE uniforms re-pushed per program -- and they were the worst offenders:
    // every (modelview, projection) epoch (camera set, shadow cascade, probe face, mirror
    // pass, each UI translate/scale) re-taxed every program bound after it with up to six
    // matrix writes. Packed once per epoch and bound at a fixed engine point, a matrix change
    // costs one upload total and a program bind costs no matrix work at all.
    //
    // Matrices are COLUMN-major, std140's default: glm's own storage goes up untouched.
    // normal is a mat3, which std140 strides at 16 bytes per column, hence float[3][4].
    struct alignas(16) MatricesUBOData
    {
        F32 modelview[16];
        F32 projection[16];
        F32 modelview_projection[16];
        F32 inv_proj[16];
        F32 texture0[16];
        F32 normal[3][4];   // .xyz used per column, .w std140 padding
    };

    void translateUI(F32 x, F32 y, F32 z);
    void scaleUI(F32 x, F32 y, F32 z);
    void pushUIMatrix();
    void popUIMatrix();
    void loadUIIdentity();
    LLVector3 getUITranslation();
    LLVector3 getUIScale();

    void flush();

    // if list is set, will store buffers in list for later use, if list isn't set, will use cache
    void beginList(std::list<LLVertexBufferData> *list);
    void endList();

    void begin(const GLuint& mode);
    void end();

    U8 getMode() const { return mMode; }

    void vertex2i(const GLint& x, const GLint& y);
    void vertex2f(const GLfloat& x, const GLfloat& y);
    void vertex3f(const GLfloat& x, const GLfloat& y, const GLfloat& z);
    void vertex2fv(const GLfloat* v);
    void vertex3fv(const GLfloat* v);

    void texCoord2i(const GLint& x, const GLint& y);
    void texCoord2f(const GLfloat& x, const GLfloat& y);
    void texCoord2fv(const GLfloat* tc);

    void color4ub(const GLubyte& r, const GLubyte& g, const GLubyte& b, const GLubyte& a);
    void color4f(const GLfloat& r, const GLfloat& g, const GLfloat& b, const GLfloat& a);
    void color4fv(const GLfloat* c);
    void color3f(const GLfloat& r, const GLfloat& g, const GLfloat& b);
    void color3fv(const GLfloat* c);
    void color4ubv(const GLubyte* c);

    void diffuseColor3f(F32 r, F32 g, F32 b);
    void diffuseColor3fv(const F32* c);
    void diffuseColor4f(F32 r, F32 g, F32 b, F32 a);
    void diffuseColor4fv(const F32* c);
    void diffuseColor4ubv(const U8* c);
    void diffuseColor4ub(U8 r, U8 g, U8 b, U8 a);

    void vertexBatchPreTransformed(LLVector4a* verts, S32 vert_count);
    void vertexBatchPreTransformed(LLVector4a* verts, LLVector2* uvs, S32 vert_count);
    void vertexBatchPreTransformed(LLVector4a* verts, LLVector2* uvs, LLColor4U*, S32 vert_count);

    void setColorMask(bool writeColor, bool writeAlpha);
    void setColorMask(bool writeColorR, bool writeColorG, bool writeColorB, bool writeAlpha);
    // The mask currently in force, so a caller can restore what it found instead of
    // assuming a convention. Channels are R, G, B, A. See LLGLSColorMask.
    void getColorMask(bool (&mask)[4]) const
    {
        mask[0] = mCurrColorMask[0];
        mask[1] = mCurrColorMask[1];
        mask[2] = mCurrColorMask[2];
        mask[3] = mCurrColorMask[3];
    }
    void setSceneBlendType(eBlendType type);

    // applies blend func to both color and alpha
    void blendFunc(eBlendFactor sfactor, eBlendFactor dfactor);
    // applies separate blend functions to color and alpha
    void blendFunc(eBlendFactor color_sfactor, eBlendFactor color_dfactor,
               eBlendFactor alpha_sfactor, eBlendFactor alpha_dfactor);

    LLLightState* getLight(U32 index);
    void setAmbientLightColor(const LLColor4& color);

    void setLineWidth(F32 width);

    // Depth offset applied to polygons while GL_POLYGON_OFFSET_FILL/LINE is enabled.
    // Stated in the forward/semantic convention: a negative factor/units pulls a fragment
    // toward the viewer. Under reverse-Z both terms are negated on the way to GL so that
    // intent survives the flipped depth mapping -- the cache holds what the caller asked
    // for, not what was issued. Redundant sets are dropped without a flush.
    void setPolygonOffset(F32 factor, F32 units);
    // Re-issue the physical offset for the current convention. Call after LLRender::sReverseZ
    // changes: the cached semantic values are unchanged, but the translation they were issued
    // under is not. Mirrors LLGLDepthTest::rebase().
    void rebasePolygonOffset();

    ALTextureSlot* getTextureSlot(U32 index);

    U32 getCurrentTexUnitIndex(void) const { return mCurrTextureUnitIndex; }

    // Resolve a sampler object belonging to THIS context. See ALSamplerCache -- the cache
    // is a member rather than a static precisely so it cannot outlive, or be torn down by,
    // a context other than its own.
    // Sampling INTENT only -- see ALSampler. Compose with |, or use one of the named
    // compositions in namespace ALSamplers.
    U32 getSampler(ALSampler key) { return mSamplerCache.get(key); }

    // For sampling modes a mask cannot express; see ALSamplerCache::get(desc).
    U32 getSampler(const ALSamplerDesc& desc) { return mSamplerCache.get(desc); }

    // Generation of this context's sampler cache; changes whenever the objects are dropped.
    // Lets a hot path cache a resolved name and revalidate it with one compare.
    U32 getSamplerGeneration() const { return mSamplerCache.getGeneration(); }

    // Delete this context's sampler objects and forget the per-unit bindings that pointed
    // at them. Both halves belong together: GL unbinds a deleted sampler automatically, but
    // ALTextureSlot::mCurrSampler would still claim it is bound, and GL may hand the same name
    // back for the next sampler created -- at which point bindSampler's redundancy check
    // would skip a bind that is needed.
    // Drop this context's sampler objects. DROPS ONLY -- it does not rebuild, because one of
    // its callers is shutdown. ~LLRender runs during thread_local destruction, after the
    // context is gone, and reaches this a second time; it is safe there precisely because the
    // first pass left the table empty and the second finds nothing to do. Rebuilding here
    // would hand that second pass a full table to delete on a dead context, which faults
    // inside the driver rather than failing. (gGLManager.mInited does not save you: it says
    // GL is up somewhere, not that THIS thread has a current context.)
    //
    // A caller that wants the objects back -- the anisotropy change in graphics preferences --
    // calls warmupSamplers() itself, where the context is known to be live.
    void clearSamplers();

    // Build every sampler object this context can hand out. get() has no lazy path, so this
    // must run before anything binds; LLRender::init() does it.
    void warmupSamplers();

    bool verifyTexUnitActive(U32 unitToVerify);

    void debugTexUnits(void);

    void clearErrors();

    struct Vertex
    {
        GLfloat v[3];
        GLubyte c[4];
        GLfloat uv[2];
    };

public:
    static U32 sUICalls;
    static U32 sUIVerts;
    static F32 sAnisotropicFilteringLevel;
    static bool sGLCoreProfile;
    static bool sNsightDebugSupport;
    static LLVector2 sUIGLScaleFactor;
    static bool sClassicMode; // classic sky mode active
    static bool sMirrorPass;  // hero-probe planar-reflection (mirror clip) pass active
    static bool s10bitBackBuffer;
    // Reverse-Z depth active this session: clip control ZERO_TO_ONE, reversed projections
    // (near=1, far=0), depth cleared to 0, default depth func GREATER. Latched by
    // LLPipeline::updateReverseZ() (newview) from the setting AND gGLManager.mHasClipControl;
    // llrender cannot read settings, so newview owns the decision and writes it here.
    static bool sReverseZ;

    // The GBuffer normal attachment is GL_RGBA16 when the deferred targets are HDR and
    // GL_RGB10_A2 when they are not, which changes how many bits its blue channel has to spend
    // on the packed geometric normal -- 16 versus 10. Latched by addDeferredAttachments from the
    // same decision that picks the format, because llrender cannot read settings and a shader
    // compiled against the wrong assumption unpacks noise.
    static bool sGBufferNormHDR;

private:
    friend class LLLightState;

    LLVertexBuffer* bufferfromCache(U32 attribute_mask, U32 count);
    LLVertexBuffer* genBuffer(U32 attribute_mask, S32 count);
    void drawBuffer(LLVertexBuffer* vb, U32 mode, S32 count);
    void resetStriders(S32 count);

    eMatrixMode mMatrixMode;
    U32 mMatIdx[NUM_MATRIX_MODES];
    U32 mMatHash[NUM_MATRIX_MODES];
    glm::mat4 mMatrix[NUM_MATRIX_MODES][LL_MATRIX_STACK_DEPTH];
    U32 mCurMatHash[NUM_MATRIX_MODES];
    U32 mLightHash;
    LLColor4 mAmbientLightColor;

    // Pack the light state into mLightsUBOData. Returns true if the bytes actually changed
    // (so the caller uploads). mLightHash is a conservative trigger -- setPosition and
    // setSpotDirection bump it unconditionally because the modelview may have moved, so a
    // frame's worth of "changes" are frequently byte-identical -- hence the compare.
    bool packLightsUBO();

    LightsUBOData   mLightsUBOData{};
    ALUniformBuffer mLightsUBO;
    U32             mLightsUBOHash  = 0xFFFFFFFFu;
    bool            mLightsUBOBound = false;

    // Rebuild the matrix block from the current stacks into the buffer's shadow. The derived
    // matrices are recomputed only for the stack that actually moved.
    void packMatricesUBO();

    ALUniformBuffer mMatricesUBO;
    U32             mMatricesUBOHash[NUM_MATRIX_MODES] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    bool            mMatricesUBOBound = false;

    // Derived matrices, cached across epochs so a stack that did not move is not re-derived.
    // Members rather than function statics: they belong to this context, and shutdown() has to
    // be able to invalidate them along with the block.
    glm::mat4       mCachedInvMdv{ 1.f };
    glm::mat4       mCachedInvProj{ 1.f };
    glm::mat4       mCachedMVP{ 1.f };

    bool            mDirty;
    U32             mCount;
    U32             mMode;
    U32             mCurrTextureUnitIndex;
    bool            mCurrColorMask[4];
    F32             mLineWidth = 1.f;
    // Semantic (forward-convention) polygon offset; GL's own defaults are 0, 0.
    F32             mPolygonOffsetFactor = 0.f;
    F32             mPolygonOffsetUnits = 0.f;

    LLPointer<LLVertexBuffer>   mBuffer;
    LLStrider<LLVector4a>       mVerticesp;
    LLStrider<LLVector2>        mTexcoordsp;
    LLStrider<LLColor4U>        mColorsp;
    U32                         mDummyVAO = 0;
    std::array<ALTextureSlot, AL_NUM_TEXTURE_SLOTS> mTextureSlots;
    // This context's sampler objects. Sits beside mTextureSlots deliberately: the units hold
    // the bindings, this holds the objects those bindings name, and the two have to be
    // torn down together and by the same thread.
    ALSamplerCache mSamplerCache;
    ALTextureSlot           mDummySlot;
    std::array<LLLightState, LL_NUM_LIGHT_UNITS> mLightState;

    eBlendFactor mCurrBlendColorSFactor;
    eBlendFactor mCurrBlendColorDFactor;
    eBlendFactor mCurrBlendAlphaSFactor;
    eBlendFactor mCurrBlendAlphaDFactor;

    std::vector<LLVector4a> mUIOffset;
    std::vector<LLVector4a> mUIScale;

    struct LLVBCache
    {
        LLPointer<LLVertexBuffer> vb;
        std::chrono::steady_clock::time_point touched;
    };

    boost::unordered_map<U64, LLVBCache> mVBCache;
    std::list<LLVertexBufferData>* mBufferDataList = nullptr;
};

extern F32 gGLModelView[16];
extern F32 gGLLastModelView[16];
extern F32 gGLLastProjection[16];
extern F32 gGLProjection[16];
extern S32 gGLViewport[4];
extern glm::mat4 gGLDeltaModelView;
extern glm::mat4 gGLInverseDeltaModelView;

extern thread_local LLRender gGL;

// This rotation matrix moves the default OpenGL reference frame
// (-Z at, Y up) to Cory's favorite reference frame (X at, Z up)
const F32 OGL_TO_CFR_ROTATION[16] = {  0.f,  0.f, -1.f,  0.f,   // -Z becomes X
                                      -1.f,  0.f,  0.f,  0.f,   // -X becomes Y
                                       0.f,  1.f,  0.f,  0.f,   //  Y becomes Z
                                       0.f,  0.f,  0.f,  1.f };

glm::mat4 copy_matrix(F32* src);
glm::mat4 get_current_modelview();
glm::mat4 get_current_projection();
glm::mat4 get_last_modelview();
glm::mat4 get_last_projection();

void copy_matrix(const glm::mat4& src, F32* dst);
void set_current_modelview(const glm::mat4& mat);
void set_current_projection(const glm::mat4& mat);
void set_last_modelview(const glm::mat4& mat);
void set_last_projection(const glm::mat4& mat);

// --- Reverse-Z projection helpers (gated on LLRender::sReverseZ) --------------
// Rewrite a forward [-1,1] projection into reversed zero-to-one (near->1, far->0):
// z_ndc' = (1 - z_ndc)/2, i.e. row2' = 0.5*(row3 - row2). Unconditional math; valid
// for any forward projection, including the hand-rolled shadow perspective whose
// depth rides an off-diagonal clip component.
glm::mat4 al_reverse_z_transform(const glm::mat4& forward_proj);
// Perspective / ortho that emit reversed-ZO when sReverseZ, else the plain forward
// glm matrix. Drop-in replacements for glm::perspective / glm::ortho at the builders.
glm::mat4 al_perspective(F32 fovy_rad, F32 aspect, F32 z_near, F32 z_far);
glm::mat4 al_ortho(F32 left, F32 right, F32 bottom, F32 top, F32 z_near, F32 z_far);
// project / unproject honoring the active convention: glm::*ZO under reverse-Z, since
// the projection already outputs [0,1] and glm must not remap the window z again.
glm::vec3 al_project(const glm::vec3& obj, const glm::mat4& modelview, const glm::mat4& proj, const glm::ivec4& viewport);
glm::vec3 al_unproject(const glm::vec3& win, const glm::mat4& modelview, const glm::mat4& proj, const glm::ivec4& viewport);
// Window depth of the near / far plane under the active convention.
inline F32 al_window_near() { return LLRender::sReverseZ ? 1.f : 0.f; }
inline F32 al_window_far()  { return LLRender::sReverseZ ? 0.f : 1.f; }

// glh compat
glm::vec3 mul_mat4_vec3(const glm::mat4& mat, const glm::vec3& vec);

#define LL_SHADER_LOADING_WARNS(...) LL_WARNS()

#endif
