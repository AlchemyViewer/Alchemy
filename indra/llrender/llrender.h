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
#include "lltexunit.h"
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

// LL_NUM_TEXTURE_LAYERS lives in lltexunit.h alongside the units it sizes.
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
    friend class LLTexUnit;
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
    // error. Call sites defended against it by issuing getTexUnit(0)->activate() first, which
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
    void setSceneBlendType(eBlendType type);

    // applies blend func to both color and alpha
    void blendFunc(eBlendFactor sfactor, eBlendFactor dfactor);
    // applies separate blend functions to color and alpha
    void blendFunc(eBlendFactor color_sfactor, eBlendFactor color_dfactor,
               eBlendFactor alpha_sfactor, eBlendFactor alpha_dfactor);

    LLLightState* getLight(U32 index);
    void setAmbientLightColor(const LLColor4& color);

    void setLineWidth(F32 width);

    LLTexUnit* getTexUnit(U32 index);

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
    // LLTexUnit::mCurrSampler would still claim it is bound, and GL may hand the same name
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
    static bool s10bitBackBuffer;

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

    bool            mDirty;
    U32             mCount;
    U32             mMode;
    U32             mCurrTextureUnitIndex;
    bool            mCurrColorMask[4];
    F32             mLineWidth = 1.f;

    LLPointer<LLVertexBuffer>   mBuffer;
    LLStrider<LLVector4a>       mVerticesp;
    LLStrider<LLVector2>        mTexcoordsp;
    LLStrider<LLColor4U>        mColorsp;
    U32                         mDummyVAO = 0;
    std::array<LLTexUnit, LL_NUM_TEXTURE_LAYERS> mTexUnits;
    // This context's sampler objects. Sits beside mTexUnits deliberately: the units hold
    // the bindings, this holds the objects those bindings name, and the two have to be
    // torn down together and by the same thread.
    ALSamplerCache mSamplerCache;
    LLTexUnit           mDummyTexUnit;
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

// glh compat
glm::vec3 mul_mat4_vec3(const glm::mat4& mat, const glm::vec3& vec);

#define LL_SHADER_LOADING_WARNS(...) LL_WARNS()

#endif
