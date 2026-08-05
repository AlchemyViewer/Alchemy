 /**
 * @file llrender.cpp
 * @brief LLRender implementation
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

#include "linden_common.h"

#include "llrender.h"

#include "alsamplerstate.h"
#include "llvertexbuffer.h"
#include "llcubemap.h"
#include "llglslshader.h"
#include "llimagegl.h"
#include "llrendertarget.h"
#include "lltexture.h"
#include "llshadermgr.h"
#include "hbxxh.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/matrix_inverse.hpp" // glm::affineInverse
#include "glm/ext/matrix_clip_space.hpp" // glm::perspective / glm::ortho
#include "glm/ext/matrix_projection.hpp" // glm::project(ZO) / glm::unProject(ZO)

#if GL_ARB_debug_output
#ifndef APIENTRY
#define APIENTRY
#endif

extern void APIENTRY gl_debug_callback(GLenum source,
                                GLenum type,
                                GLuint id,
                                GLenum severity,
                                GLsizei length,
                                const GLchar* message,
                                GLvoid* userParam)
;
#endif

thread_local LLRender gGL;

#if !LL_RELEASE_FOR_DOWNLOAD
namespace
{
    // Register the "Lights" block's expected std140 layout for the debug-build validator
    // (LLGLSLShader::registerEngineBlockLayout), derived from offsetof() on the very struct
    // packLightsUBO() writes -- so the check can never drift from the pack. Array members
    // introspect under their "[0]" element name, hence the explicit mName overrides; the
    // scalar-name member resolves from the reserved-uniform table.
    const bool s_lights_layout_registered = []
    {
        using D = LLRender::LightsUBOData;
        std::vector<LLGLSLShader::EngineBlockLayoutMember> members =
        {
            { -1, "light_position[0]",             (U32)offsetof(D, light_position),             false },
            { -1, "light_direction[0]",            (U32)offsetof(D, light_direction),            false },
            { -1, "light_attenuation[0]",          (U32)offsetof(D, light_attenuation),          false },
            { -1, "light_deferred_attenuation[0]", (U32)offsetof(D, light_deferred_attenuation), false },
            { -1, "light_diffuse[0]",              (U32)offsetof(D, light_diffuse),              false },
            { LLShaderMgr::LIGHT_AMBIENT, nullptr, (U32)offsetof(D, light_ambient),              false },
        };
        LLGLSLShader::registerEngineBlockLayout("Lights", std::move(members));

        using M = LLRender::MatricesUBOData;
        LLGLSLShader::registerEngineBlockLayout("Matrices",
        {
            { LLShaderMgr::MODELVIEW_MATRIX,            nullptr, (U32)offsetof(M, modelview),            true },
            { LLShaderMgr::PROJECTION_MATRIX,           nullptr, (U32)offsetof(M, projection),           true },
            { LLShaderMgr::MODELVIEW_PROJECTION_MATRIX, nullptr, (U32)offsetof(M, modelview_projection), true },
            { LLShaderMgr::INVERSE_PROJECTION_MATRIX,   nullptr, (U32)offsetof(M, inv_proj),             true },
            { LLShaderMgr::TEXTURE_MATRIX0,             nullptr, (U32)offsetof(M, texture0),             true },
            { LLShaderMgr::NORMAL_MATRIX,               nullptr, (U32)offsetof(M, normal),               true },
        });
        return true;
    }();
}
#endif // !LL_RELEASE_FOR_DOWNLOAD

// Handy copies of last good GL matrices
F32 gGLModelView[16];
F32 gGLLastModelView[16];
F32 gGLLastProjection[16];
F32 gGLProjection[16];

// transform from last frame's camera space to this frame's camera space (and inverse)
glm::mat4 gGLDeltaModelView;
glm::mat4 gGLInverseDeltaModelView;

S32 gGLViewport[4];


U32 LLRender::sUICalls = 0;
U32 LLRender::sUIVerts = 0;
U32 ALTextureSlot::sWhiteTexture = 0;
F32 LLRender::sAnisotropicFilteringLevel = 0.f;
bool LLRender::sGLCoreProfile = false;
bool LLRender::sNsightDebugSupport = false;
LLVector2 LLRender::sUIGLScaleFactor = LLVector2(1.f, 1.f);
bool LLRender::sClassicMode = false;
bool LLRender::sMirrorPass = false;
bool LLRender::sReverseZ = false;
bool LLRender::s10bitBackBuffer = false;


const U32 immediate_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXCOORD0;

static const GLenum sGLBlendFactor[] =
{
    GL_ONE,
    GL_ZERO,
    GL_DST_COLOR,
    GL_SRC_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_DST_ALPHA,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,

    GL_ZERO // 'BF_UNDEF'
};


LLLightState::LLLightState(S32 index)
: mIndex(index),
  mEnabled(false),
  mConstantAtten(1.f),
  mLinearAtten(0.f),
  mQuadraticAtten(0.f),
  mSpotExponent(0.f),
  mSpotCutoff(180.f)
{
    if (mIndex == 0)
    {
        mDiffuse.set(1,1,1,1);
        mDiffuseB.set(0,0,0,0);
        mSpecular.set(1,1,1,1);
    }

    mSunIsPrimary = true;

    mAmbient.set(0,0,0,1);
    mPosition.set(0,0,1,0);
    mSpotDirection.set(0,0,-1);
}

void LLLightState::enable()
{
    mEnabled = true;
}

void LLLightState::disable()
{
    mEnabled = false;
}

void LLLightState::setDiffuse(const LLColor4& diffuse)
{
    if (mDiffuse != diffuse)
    {
        ++gGL.mLightHash;
        mDiffuse = diffuse;
    }
}

void LLLightState::setDiffuseB(const LLColor4& diffuse)
{
    if (mDiffuseB != diffuse)
    {
        ++gGL.mLightHash;
        mDiffuseB = diffuse;
    }
}

void LLLightState::setSunPrimary(bool v)
{
    if (mSunIsPrimary != v)
    {
        ++gGL.mLightHash;
        mSunIsPrimary = v;
    }
}

void LLLightState::setSize(F32 v)
{
    if (mSize != v)
    {
        ++gGL.mLightHash;
        mSize = v;
    }
}

void LLLightState::setFalloff(F32 v)
{
    if (mFalloff != v)
    {
        ++gGL.mLightHash;
        mFalloff = v;
    }
}

void LLLightState::setAmbient(const LLColor4& ambient)
{
    if (mAmbient != ambient)
    {
        ++gGL.mLightHash;
        mAmbient = ambient;
    }
}

void LLLightState::setSpecular(const LLColor4& specular)
{
    if (mSpecular != specular)
    {
        ++gGL.mLightHash;
        mSpecular = specular;
    }
}

void LLLightState::setPosition(const LLVector4& position)
{
    //always set position because modelview matrix may have changed
    ++gGL.mLightHash;
    mPosition = position;
    //transform position by current modelview matrix
    glm::vec4 pos(position);
    pos = gGL.getModelviewMatrix() * pos;
    mPosition.set(glm::value_ptr(pos));
}

void LLLightState::setConstantAttenuation(const F32& atten)
{
    if (mConstantAtten != atten)
    {
        mConstantAtten = atten;
        ++gGL.mLightHash;
    }
}

void LLLightState::setLinearAttenuation(const F32& atten)
{
    if (mLinearAtten != atten)
    {
        ++gGL.mLightHash;
        mLinearAtten = atten;
    }
}

void LLLightState::setQuadraticAttenuation(const F32& atten)
{
    if (mQuadraticAtten != atten)
    {
        ++gGL.mLightHash;
        mQuadraticAtten = atten;
    }
}

void LLLightState::setSpotExponent(const F32& exponent)
{
    if (mSpotExponent != exponent)
    {
        ++gGL.mLightHash;
        mSpotExponent = exponent;
    }
}

void LLLightState::setSpotCutoff(const F32& cutoff)
{
    if (mSpotCutoff != cutoff)
    {
        ++gGL.mLightHash;
        mSpotCutoff = cutoff;
    }
}

void LLLightState::setSpotDirection(const LLVector3& direction)
{
    //always set direction because modelview matrix may have changed
    ++gGL.mLightHash;

    //transform direction by current modelview matrix
    glm::vec3 dir(direction);
    const glm::mat3 mat(gGL.getModelviewMatrix());
    dir = mat * dir;

    mSpotDirection.set(glm::value_ptr(dir));
}

LLRender::LLRender()
  : mDirty(false),
    mCount(0),
    mMode(LLRender::TRIANGLES),
    mCurrTextureUnitIndex(0)
{
    for (U32 i = 0; i < AL_NUM_TEXTURE_SLOTS; i++)
    {
        mTextureSlots[i].mIndex = i;
    }

    for (U32 i = 0; i < LL_NUM_LIGHT_UNITS; ++i)
    {
        mLightState[i].mIndex = i;
    }

    for (U32 i = 0; i < 4; i++)
    {
        mCurrColorMask[i] = true;
    }

    mCurrBlendColorSFactor = BF_UNDEF;
    mCurrBlendAlphaSFactor = BF_UNDEF;
    mCurrBlendColorDFactor = BF_UNDEF;
    mCurrBlendAlphaDFactor = BF_UNDEF;

    mMatrixMode = LLRender::MM_MODELVIEW;

    for (U32 i = 0; i < NUM_MATRIX_MODES; ++i)
    {
        for (U32 j = 0; j < LL_MATRIX_STACK_DEPTH; ++j)
        {
            mMatrix[i][j] = glm::identity<glm::mat4>();
        }
        mMatIdx[i] = 0;
        mMatHash[i] = 0;
        mCurMatHash[i] = 0xFFFFFFFF;
    }

    mLightHash = 0;
}

LLRender::~LLRender()
{
    shutdown();
}

bool LLRender::init(bool needs_vertex_buffer)
{
#if GL_ARB_debug_output && !LL_DARWIN
    if (gGLManager.mHasDebugOutput && gDebugGL)
    { //setup debug output callback
        //glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, GL_TRUE);
        glDebugMessageCallback((GLDEBUGPROC) gl_debug_callback, NULL);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }
#endif

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Fresh context: nothing is bound at UB_LIGHTS/UB_MATRICES yet, whatever a previous one had.
    mLightsUBOBound   = false;
    mMatricesUBOBound = false;

    // Build this context's sampler objects before anything can ask for one.
    mSamplerCache.warmup();


    gGL.setSceneBlendType(LLRender::BT_ALPHA);
    gGL.setAmbientLightColor(LLColor4::black);

    glCullFace(GL_BACK);

    // necessary for reflection maps
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

#if LL_WINDOWS
    if (glGenVertexArrays == nullptr)
    {
        return false;
    }
#endif

    { //bind a dummy vertex array object so we're core profile compliant
        glGenVertexArrays(1, &mDummyVAO);
        glBindVertexArray(mDummyVAO);
    }

    if (needs_vertex_buffer)
    {
        initVertexBuffer();
    }
    return true;
}

void LLRender::initVertexBuffer()
{
    llassert_always(mBuffer.isNull());
    stop_glerror();
    mBuffer = new LLVertexBuffer(immediate_mask);
    mBuffer->allocateBuffer(4096, 0);
    mBuffer->getVertexStrider(mVerticesp);
    mBuffer->getTexCoord0Strider(mTexcoordsp);
    mBuffer->getColorStrider(mColorsp);
    stop_glerror();
}

void LLRender::resetVertexBuffer()
{
    mBuffer = nullptr;
    mBufferDataList = nullptr;
    mVBCache.clear();
}

void LLRender::shutdown()
{
    // NOTE: gGL is thread_local, so this runs once per thread that ever touched it --
    // including the texture upload thread, which owns a second shared context. Everything
    // released here must therefore belong to THIS thread's context and no other. That is
    // why mSamplerCache is a member: a static one would have the upload thread deleting
    // the render thread's samplers. mDummyVAO and mLightsUBO stay zero on worker threads.
    clearSamplers();

    resetVertexBuffer();
    if (mDummyVAO)
    {
        // ~LLRender calls shutdown() again during thread_local destruction, after the
        // context is gone. Normally the explicit gGL.shutdown() during teardown got here
        // first and zeroed this, but an abnormal exit skips it and leaves a live handle
        // with a dead context -- deleting the VAO then faults inside the driver. The name
        // dies with the context regardless, so skipping the delete costs nothing.
        //
        // Same gate LLUniformBuffer::release() and LLVertexBuffer already use.
        if (gGLManager.mInited)
        {
            glBindVertexArray(0);
            glDeleteVertexArrays(1, &mDummyVAO);
        }
        mDummyVAO = 0;
    }

    // Drop the shared light block with the context that owns it. Forcing a re-pack (rather
    // than just clearing mLightsUBOBound) means a restarted context can never rebind a
    // buffer name from the dead one.
    mLightsUBO.release();
    mLightsUBOHash  = 0xFFFFFFFFu;
    mLightsUBOBound = false;

    // Same for the matrix block. syncMatrices re-initialises the shadow on the next use
    // because release() leaves it unallocated.
    mMatricesUBO.release();
    for (U32 i = 0; i < NUM_MATRIX_MODES; ++i)
    {
        mMatricesUBOHash[i] = 0xFFFFFFFFu;
    }
    mMatricesUBOBound = false;
}

void LLRender::clearSamplers()
{
    mSamplerCache.clear();

    for (ALTextureSlot& unit : mTextureSlots)
    {
        unit.mCurrSampler = 0;
    }

}

void LLRender::warmupSamplers()
{
    mSamplerCache.warmup();
}

void LLRender::refreshState(void)
{
    mDirty = true;

    // Called when GL state may have been changed behind our back, so re-assert the shared
    // blocks' bindings along with the texture units and colour mask.
    mLightsUBOBound   = false;
    mMatricesUBOBound = false;

    U32 active_unit = mCurrTextureUnitIndex;

    for (U32 i = 0; i < mTextureSlots.size(); i++)
    {
        mTextureSlots[i].refreshState();
    }

    mTextureSlots[active_unit].activate();

    setColorMask(mCurrColorMask[0], mCurrColorMask[1], mCurrColorMask[2], mCurrColorMask[3]);

    // Unconditional re-issue: setPolygonOffset would see the cache already agreeing with the
    // requested value and skip the GL call, leaving the fresh context at its 0,0 default.
    rebasePolygonOffset();

    flush();

    mDirty = false;
}

// Pack the light arrays into the shared block. Returns true when the bytes moved.
bool LLRender::packLightsUBO()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    // Must match the std140 layout the shaders declare: five 16-byte-strided [8] arrays
    // (128 bytes each) then a float3 rounded up to the block's 16-byte multiple.
    static_assert(sizeof(LightsUBOData) == 656, "LightsUBOData must match std140 (656 bytes)");

    LightsUBOData d{};

    for (U32 i = 0; i < LL_NUM_LIGHT_UNITS; i++)
    {
        const LLLightState* light = &mLightState[i];

        // std140 pads every array element to 16 bytes whatever the element type, so each
        // row below is float[4] and the unused tail components stay zero.
        memcpy(d.light_position[i], light->mPosition.mV, sizeof(F32) * 4);
        memcpy(d.light_direction[i], light->mSpotDirection.mV, sizeof(F32) * 3);

        d.light_attenuation[i][0] = light->mLinearAtten;
        d.light_attenuation[i][1] = light->mQuadraticAtten;
        d.light_attenuation[i][2] = light->mSpecular.mV[2];
        d.light_attenuation[i][3] = light->mSpecular.mV[3];

        d.light_deferred_attenuation[i][0] = light->mSize;
        d.light_deferred_attenuation[i][1] = light->mFalloff;

        memcpy(d.light_diffuse[i], light->mDiffuse.mV, sizeof(F32) * 3);
    }

    memcpy(d.light_ambient, mAmbientLightColor.mV, sizeof(F32) * 3);

    if (memcmp(&d, &mLightsUBOData, sizeof(LightsUBOData)) == 0)
    {
        return false;
    }

    mLightsUBOData = d;
    return true;
}

void LLRender::syncLightState()
{
    // The light ARRAYS live in the shared UB_LIGHTS block: packed once when the light state
    // actually moves, not once per program. mLightHash is only a cheap trigger -- setPosition
    // and setSpotDirection bump it unconditionally (the modelview may have changed), so the
    // pack still byte-compares before spending an upload.
    if (mLightsUBOHash != mLightHash || !mLightsUBO.allocated())
    {
        mLightsUBOHash = mLightHash;
        if (packLightsUBO() || !mLightsUBO.allocated())
        {
            mLightsUBO.update(&mLightsUBOData, sizeof(LightsUBOData));
            mLightsUBOBound = false; // re-assert the binding against the new store
        }
    }

    if (!mLightsUBOBound)
    {
        mLightsUBO.bind(LLGLSLShader::UB_LIGHTS);
        mLightsUBOBound = true;
    }

    // Everything below is still a LOOSE per-program uniform, so it keeps the per-shader hash
    // gate. These names have writers outside LLRender (LLPipeline::bindDeferredShader pushes
    // an auto-adjusted sunlight_color; sun_up_factor is written from the pipeline, the draw
    // pools and LLSettingsVO), so folding them into the shared block would let one writer
    // stomp another.
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

    if (!shader || shader->mLightHash == mLightHash)
    {
        return;
    }

    shader->mLightHash = mLightHash;

    shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, mLightState[0].mSunIsPrimary ? 1 : 0);

    if (sClassicMode)
    {
        LLVector3 diffuse(mLightState[0].mDiffuse.mV);
        LLVector3 diffuse_b(mLightState[0].mDiffuseB.mV);

        shader->uniform3fv(LLShaderMgr::AMBIENT, 1, mAmbientLightColor.mV);
        shader->uniform3fv(LLShaderMgr::SUNLIGHT_COLOR, 1, diffuse.mV);
        shader->uniform3fv(LLShaderMgr::MOONLIGHT_COLOR, 1, diffuse_b.mV);
    }
}

void LLRender::packMatricesUBO()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    static_assert(sizeof(MatricesUBOData) == 368, "MatricesUBOData must match std140 (368 bytes)");

    const glm::mat4& mdv  = mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];
    const glm::mat4& proj = mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]];
    const glm::mat4& tex  = mMatrix[MM_TEXTURE0][mMatIdx[MM_TEXTURE0]];

    const bool mdv_moved  = mMatHash[MM_MODELVIEW]  != mMatricesUBOHash[MM_MODELVIEW];
    const bool proj_moved = mMatHash[MM_PROJECTION] != mMatricesUBOHash[MM_PROJECTION];

    if (mdv_moved)
    {
        // The modelview is affine (view * model, no perspective), so affineInverse is exact
        // and much cheaper than a general 4x4 inverse.
        mCachedInvMdv = glm::affineInverse(mdv);
    }
    if (proj_moved)
    {
        // Projection is not affine -- general inverse required.
        mCachedInvProj = glm::inverse(proj);
    }
    if (mdv_moved || proj_moved)
    {
        mCachedMVP = proj * mdv;
    }

    MatricesUBOData d;
    memcpy(d.modelview,            glm::value_ptr(mdv),           sizeof(d.modelview));
    memcpy(d.projection,           glm::value_ptr(proj),          sizeof(d.projection));
    memcpy(d.modelview_projection, glm::value_ptr(mCachedMVP),    sizeof(d.modelview_projection));
    memcpy(d.inv_proj,             glm::value_ptr(mCachedInvProj), sizeof(d.inv_proj));
    memcpy(d.texture0,             glm::value_ptr(tex),           sizeof(d.texture0));

    // normal_matrix is the upper 3x3 of transpose(inv(modelview)). Column c of that transpose
    // is row c of the inverse, and glm stores columns contiguously -- so element k of column c
    // is the inverse's (row c, column k), i.e. value_ptr(inv)[4*k + c]. This reproduces exactly
    // the nine floats the loose uniformMatrix3fv used to upload.
    const F32* inv = glm::value_ptr(mCachedInvMdv);
    for (U32 c = 0; c < 3; ++c)
    {
        d.normal[c][0] = inv[0 * 4 + c];
        d.normal[c][1] = inv[1 * 4 + c];
        d.normal[c][2] = inv[2 * 4 + c];
        d.normal[c][3] = 0.f;
    }

    U8* shadow = mMatricesUBO.beginWrite();
    if (memcmp(shadow, &d, sizeof(d)) != 0)
    {
        memcpy(shadow, &d, sizeof(d));
        mMatricesUBO.endWrite(0, sizeof(d));
    }
}

void LLRender::syncMatrices()
{
    STOP_GLERROR;
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

    if (shader)
    {
        // The matrices live in the shared UB_MATRICES block: packed once per matrix EPOCH (any
        // mMatHash movement since the last pack) rather than once per program, so a program
        // bind costs no matrix work at all. The derived matrices (inverses, normal, MVP) are
        // recomputed per epoch under per-stack gates inside the pack, and unconditionally --
        // any program may read them from the block, so the old hasUniform() gates are gone.
        if (!mMatricesUBO.allocated())
        {
            // Fresh context (first use, or re-init after shutdown released the old block):
            // re-pack and re-bind everything.
            mMatricesUBO.initShadowed(sizeof(MatricesUBOData));
            for (U32 i = 0; i < NUM_MATRIX_MODES; ++i)
            {
                mMatricesUBOHash[i] = 0xFFFFFFFFu;
            }
            mMatricesUBOBound = false;
        }

        if (mMatHash[MM_MODELVIEW]  != mMatricesUBOHash[MM_MODELVIEW]  ||
            mMatHash[MM_PROJECTION] != mMatricesUBOHash[MM_PROJECTION] ||
            mMatHash[MM_TEXTURE0]   != mMatricesUBOHash[MM_TEXTURE0])
        {
            packMatricesUBO();
            for (U32 i = 0; i < NUM_MATRIX_MODES; ++i)
            {
                mMatricesUBOHash[i] = mMatHash[i];
            }
        }

        if (!mMatricesUBOBound)
        {
            // First attach on this context (or a refreshState() re-assert): bindCurrent both
            // uploads any pending bytes and (re)claims the binding point -- needed even when
            // clean, e.g. UPDATE_DIRECT where endWrite uploads without ever dirtying.
            mMatricesUBO.bindCurrent(LLGLSLShader::UB_MATRICES);
            mMatricesUBOBound = true;
        }
        else
        {
            // Per-draw steady state: upload when the pack dirtied the shadow, otherwise just
            // confirm the last flushed slice is still readable (streaming-ring reuse can
            // invalidate it). No GL calls at all on the clean-and-live path.
            mMatricesUBO.ensureCurrent(LLGLSLShader::UB_MATRICES);
        }

        if (shader->mFeatures.hasLighting || shader->mFeatures.calculatesLighting || shader->mFeatures.calculatesAtmospherics)
        { //also sync light state
            syncLightState();
        }
    }
    STOP_GLERROR;
}

void LLRender::translatef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
    flush();

    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::translate(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::vec3(x, y, z));
        mMatHash[mMatrixMode]++;
    }
}

void LLRender::scalef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
    flush();

    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::scale(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::vec3(x, y, z));
        mMatHash[mMatrixMode]++;
    }
}

void LLRender::ortho(F32 left, F32 right, F32 bottom, F32 top, F32 zNear, F32 zFar)
{
    flush();

    {
        // al_ortho emits reversed-ZO under reverse-Z (mapping legacy z in [-1,1] fully
        // inside the [0,1] clip volume, so 2D/UI content at z in [-1,0) is not clipped),
        // and plain glm::ortho otherwise. Converts all gGL.ortho() callers at once.
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] *= al_ortho(left, right, bottom, top, zNear, zFar);
        mMatHash[mMatrixMode]++;
    }
}

void LLRender::rotatef(const GLfloat& a, const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
    flush();

    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::rotate(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::radians(a), glm::vec3(x,y,z));
        mMatHash[mMatrixMode]++;
    }
}

void LLRender::pushMatrix()
{
    flush();

    {
        if (mMatIdx[mMatrixMode] < LL_MATRIX_STACK_DEPTH-1)
        {
            mMatrix[mMatrixMode][mMatIdx[mMatrixMode]+1] = mMatrix[mMatrixMode][mMatIdx[mMatrixMode]];
            ++mMatIdx[mMatrixMode];
        }
        else
        {
            LL_WARNS() << "Matrix stack overflow." << LL_ENDL;
        }
    }
}

void LLRender::popMatrix()
{
    flush();
    {
        if (mMatIdx[mMatrixMode] > 0)
        {
            --mMatIdx[mMatrixMode];
            mMatHash[mMatrixMode]++;
        }
        else
        {
            LL_WARNS() << "Matrix stack underflow." << LL_ENDL;
        }
    }
}

void LLRender::loadMatrix(const GLfloat* m)
{
    flush();
    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::make_mat4((GLfloat*) m);
        mMatHash[mMatrixMode]++;
    }
}

void LLRender::multMatrix(const GLfloat* m)
{
    flush();
    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] *= glm::make_mat4(m);
        mMatHash[mMatrixMode]++;
    }
}

void LLRender::matrixMode(eMatrixMode mode)
{
    mMatrixMode = mode;
}

LLRender::eMatrixMode LLRender::getMatrixMode()
{
    return mMatrixMode;
}


void LLRender::loadIdentity()
{
    flush();

    {
        llassert_always(mMatrixMode < NUM_MATRIX_MODES) ;

        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::identity<glm::mat4>();
        mMatHash[mMatrixMode]++;
    }
}

const glm::mat4& LLRender::getModelviewMatrix()
{
    return mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]];
}

const glm::mat4& LLRender::getProjectionMatrix()
{
    return mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]];
}

void LLRender::translateUI(F32 x, F32 y, F32 z)
{
    if (mUIOffset.empty())
    {
        LL_ERRS() << "Need to push a UI translation frame before offsetting" << LL_ENDL;
    }

    mUIOffset.back().add(LLVector4a(x, y, z));
}

void LLRender::scaleUI(F32 x, F32 y, F32 z)
{
    if (mUIScale.empty())
    {
        LL_ERRS() << "Need to push a UI transformation frame before scaling." << LL_ENDL;
    }

    mUIScale.back().mul(LLVector4a(x, y, z));
}

void LLRender::pushUIMatrix()
{
    if (mUIOffset.empty())
    {
        mUIOffset.emplace_back(0.f);
    }
    else
    {
        mUIOffset.push_back(mUIOffset.back());
    }

    if (mUIScale.empty())
    {
        mUIScale.emplace_back(1.f);
    }
    else
    {
        mUIScale.push_back(mUIScale.back());
    }
}

void LLRender::popUIMatrix()
{
    if (mUIOffset.empty())
    {
        LL_ERRS() << "UI offset stack blown." << LL_ENDL;
    }
    mUIOffset.pop_back();
    mUIScale.pop_back();
}

LLVector3 LLRender::getUITranslation()
{
    if (mUIOffset.empty())
    {
        return LLVector3::zero;
    }

    return LLVector3(mUIOffset.back().getF32ptr());
}

LLVector3 LLRender::getUIScale()
{
    if (mUIScale.empty())
    {
        return LLVector3::all_one;
    }

    return LLVector3(mUIScale.back().getF32ptr());
}


void LLRender::loadUIIdentity()
{
    if (mUIOffset.empty())
    {
        LL_ERRS() << "Need to push UI translation frame before clearing offset." << LL_ENDL;
    }

    mUIOffset.back().clear();
    mUIScale.back().splat(1);
}

void LLRender::setColorMask(bool writeColor, bool writeAlpha)
{
    setColorMask(writeColor, writeColor, writeColor, writeAlpha);
}

void LLRender::setColorMask(bool writeColorR, bool writeColorG, bool writeColorB, bool writeAlpha)
{
    flush();

    if (mCurrColorMask[0] != writeColorR ||
        mCurrColorMask[1] != writeColorG ||
        mCurrColorMask[2] != writeColorB ||
        mCurrColorMask[3] != writeAlpha)
    {
        mCurrColorMask[0] = writeColorR;
        mCurrColorMask[1] = writeColorG;
        mCurrColorMask[2] = writeColorB;
        mCurrColorMask[3] = writeAlpha;

        glColorMask(writeColorR ? GL_TRUE : GL_FALSE,
                    writeColorG ? GL_TRUE : GL_FALSE,
                    writeColorB ? GL_TRUE : GL_FALSE,
                    writeAlpha ? GL_TRUE : GL_FALSE);
    }
}

void LLRender::setSceneBlendType(eBlendType type)
{
    switch (type)
    {
        case BT_ALPHA:
            blendFunc(BF_SOURCE_ALPHA, BF_ONE_MINUS_SOURCE_ALPHA);
            break;
        case BT_ADD:
            blendFunc(BF_ONE, BF_ONE);
            break;
        case BT_ADD_WITH_ALPHA:
            blendFunc(BF_SOURCE_ALPHA, BF_ONE);
            break;
        case BT_MULT:
            blendFunc(BF_DEST_COLOR, BF_ZERO);
            break;
        case BT_MULT_ALPHA:
            blendFunc(BF_DEST_ALPHA, BF_ZERO);
            break;
        case BT_MULT_X2:
            blendFunc(BF_DEST_COLOR, BF_SOURCE_COLOR);
            break;
        case BT_REPLACE:
            blendFunc(BF_ONE, BF_ZERO);
            break;
        default:
            LL_ERRS() << "Unknown Scene Blend Type: " << type << LL_ENDL;
            break;
    }
}

void LLRender::blendFunc(eBlendFactor sfactor, eBlendFactor dfactor)
{
    llassert(sfactor < BF_UNDEF);
    llassert(dfactor < BF_UNDEF);
    if (mCurrBlendColorSFactor != sfactor || mCurrBlendColorDFactor != dfactor ||
        mCurrBlendAlphaSFactor != sfactor || mCurrBlendAlphaDFactor != dfactor)
    {
        mCurrBlendColorSFactor = sfactor;
        mCurrBlendAlphaSFactor = sfactor;
        mCurrBlendColorDFactor = dfactor;
        mCurrBlendAlphaDFactor = dfactor;
        flush();
        glBlendFunc(sGLBlendFactor[sfactor], sGLBlendFactor[dfactor]);
    }
}

void LLRender::blendFunc(eBlendFactor color_sfactor, eBlendFactor color_dfactor,
             eBlendFactor alpha_sfactor, eBlendFactor alpha_dfactor)
{
    llassert(color_sfactor < BF_UNDEF);
    llassert(color_dfactor < BF_UNDEF);
    llassert(alpha_sfactor < BF_UNDEF);
    llassert(alpha_dfactor < BF_UNDEF);

    if (mCurrBlendColorSFactor != color_sfactor || mCurrBlendColorDFactor != color_dfactor ||
        mCurrBlendAlphaSFactor != alpha_sfactor || mCurrBlendAlphaDFactor != alpha_dfactor)
    {
        mCurrBlendColorSFactor = color_sfactor;
        mCurrBlendAlphaSFactor = alpha_sfactor;
        mCurrBlendColorDFactor = color_dfactor;
        mCurrBlendAlphaDFactor = alpha_dfactor;
        flush();

        glBlendFuncSeparate(sGLBlendFactor[color_sfactor], sGLBlendFactor[color_dfactor],
                           sGLBlendFactor[alpha_sfactor], sGLBlendFactor[alpha_dfactor]);
    }
}

ALTextureSlot* LLRender::getTextureSlot(U32 index)
{
    if (index < mTextureSlots.size())
    {
        return &mTextureSlots[index];
    }
    else
    {
        LL_DEBUGS() << "Non-existing texture unit layer requested: " << index << LL_ENDL;
        return &mDummySlot;
    }
}

LLLightState* LLRender::getLight(U32 index)
{
    if (index < mLightState.size())
    {
        return &mLightState[index];
    }

    return NULL;
}

void LLRender::setAmbientLightColor(const LLColor4& color)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    if (color != mAmbientLightColor)
    {
        ++mLightHash;
        mAmbientLightColor = color;
    }
}

bool LLRender::verifyTexUnitActive(U32 unitToVerify)
{
    if (mCurrTextureUnitIndex == unitToVerify)
    {
        return true;
    }
    else
    {
        LL_WARNS() << "TexUnit currently active: " << mCurrTextureUnitIndex << " (expecting " << unitToVerify << ")" << LL_ENDL;
        return false;
    }
}

void LLRender::clearErrors()
{
    while (glGetError())
    {
        //loop until no more error flags left
    }
}

void LLRender::beginList(std::list<LLVertexBufferData> *list)
{
    if (mBufferDataList)
    {
        LL_ERRS() << "beginList called while another list is open." << LL_ENDL;
    }
    llassert(LLGLSLShader::sCurBoundShaderPtr == &gUIProgram);
    flush();
    mBufferDataList = list;
}

void LLRender::endList()
{
    if (mBufferDataList)
    {
        flush();
        mBufferDataList = nullptr;
    }
    else
    {
        llassert(false); // endList called without an open list
    }
}

void LLRender::begin(const GLuint& mode)
{
    if (mode != mMode)
    {
        if (mMode == LLRender::LINES ||
            mMode == LLRender::TRIANGLES ||
            mMode == LLRender::POINTS)
        {
            flush();
        }
        else if (mCount != 0)
        {
            LL_ERRS() << "gGL.begin() called redundantly." << LL_ENDL;
        }

        mMode = mode;
    }
}

void LLRender::end()
{
    if (mCount == 0)
    {
        return;
        //IMM_ERRS << "GL begin and end called with no vertices specified." << LL_ENDL;
    }

    if ((mMode != LLRender::LINES &&
        mMode != LLRender::TRIANGLES &&
        mMode != LLRender::POINTS) ||
        mCount > 2048)
    {
        flush();
    }
}

void LLRender::flush()
{
    STOP_GLERROR;
    if (mCount > 0)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
        llassert_always(LLGLSLShader::sCurBoundShaderPtr != nullptr);

        if (!mUIOffset.empty())
        {
            sUICalls++;
            sUIVerts += mCount;
        }

        //store mCount in a local variable to avoid re-entrance (drawArrays may call flush)
        U32 count = mCount;

        if (mMode == LLRender::TRIANGLES)
        {
            if (mCount%3 != 0)
            {
            count -= (mCount % 3);
            LL_WARNS() << "Incomplete triangle requested." << LL_ENDL;
            }
        }

        if (mMode == LLRender::LINES)
        {
            if (mCount%2 != 0)
            {
                count -= (mCount % 2);
                LL_WARNS() << "Incomplete line requested." << LL_ENDL;
            }
        }

        mCount = 0;

        if (mBuffer)
        {

            LLVertexBuffer *vb;

            U32 attribute_mask = LLGLSLShader::sCurBoundShaderPtr->mAttributeMask;

            if (mBufferDataList)
            {
                vb = genBuffer(attribute_mask, count);
                mBufferDataList->emplace_back(
                    vb,
                    mMode,
                    count,
                    gGL.getTextureSlot(0)->mCurrTexture,
                    gGL.getTextureSlot(0)->mCurrSampler,
                    mMatrix[MM_MODELVIEW][mMatIdx[MM_MODELVIEW]],
                    mMatrix[MM_PROJECTION][mMatIdx[MM_PROJECTION]],
                    mMatrix[MM_TEXTURE0][mMatIdx[MM_TEXTURE0]]
                    );
            }
            else
            {
                vb = bufferfromCache(attribute_mask, count);
            }

            drawBuffer(vb, mMode, count);
        }
        else
        {
            // mBuffer is present in main thread and not present in an image thread
            LL_ERRS() << "A flush call from outside main rendering thread" << LL_ENDL;
        }

        resetStriders(count);
    }
}

LLVertexBuffer* LLRender::bufferfromCache(U32 attribute_mask, U32 count)
{
    LLVertexBuffer *vb = nullptr;
    HBXXH64 hash;

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache hash");

        hash.update((U8*)mVerticesp.get(), count * sizeof(LLVector4a));
        if (attribute_mask & LLVertexBuffer::MAP_TEXCOORD0)
        {
            hash.update((U8*)mTexcoordsp.get(), count * sizeof(LLVector2));
        }

        if (attribute_mask & LLVertexBuffer::MAP_COLOR)
        {
            hash.update((U8*)mColorsp.get(), count * sizeof(LLColor4U));
        }

        hash.finalize();
    }

    U64 vhash = hash.digest();

    // check the VB cache before making a new vertex buffer
    // This is a giant hack to deal with (mostly) our terrible UI rendering code
    // that was built on top of OpenGL immediate mode.  Huge performance wins
    // can be had by not uploading geometry to VRAM unless absolutely necessary.
    // Most of our usage of the "immediate mode" style draw calls is actually
    // sending the same geometry over and over again.
    // To leverage this, we maintain a running hash of the vertex stream being
    // built up before a flush, and then check that hash against a VB
    // cache just before creating a vertex buffer in VRAM
    boost::unordered_map<U64, LLVBCache>::iterator cache = mVBCache.find(vhash);
    if (cache != mVBCache.end())
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache hit");
        // cache hit, just use the cached buffer
        vb = cache->second.vb;
        cache->second.touched = std::chrono::steady_clock::now();
    }
    else
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache miss");
        vb = genBuffer(attribute_mask, count);

        mVBCache[vhash] = { vb , std::chrono::steady_clock::now() };

        static U32 miss_count = 0;
        miss_count++;
        if (miss_count > 1024)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb cache clean");
            miss_count = 0;
            auto now = std::chrono::steady_clock::now();

            using namespace std::chrono_literals;
            // every 1024 misses, clean the cache of any VBs that haven't been touched in the last second
            for (boost::unordered_map<U64, LLVBCache>::iterator iter = mVBCache.begin(); iter != mVBCache.end();)
            {
                if (now - iter->second.touched > 1s)
                {
                    iter = mVBCache.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }
        }
    }
    return vb;
}

LLVertexBuffer* LLRender::genBuffer(U32 attribute_mask, S32 count)
{
    LLVertexBuffer * vb = new LLVertexBuffer(attribute_mask);
    vb->allocateBuffer(count, 0);

    // Non-Apple path uses glBufferSubData inside setXxxData, so the VBO
    // must already be bound. On Apple, the VBO is lazily created in
    // _unmapBuffer (LLAppleVBOPool); calling setBuffer() here would bind
    // mGLBuffer == 0 and then setupVertexBuffer would issue
    // glVertexAttribPointer with a non-null offset against no bound
    // GL_ARRAY_BUFFER — GL_INVALID_OPERATION in core profile.
    if (!gGLManager.mIsApple)
    {
        vb->setBuffer();
    }

    vb->setPositionData(mVerticesp.get());

    if (attribute_mask & LLVertexBuffer::MAP_TEXCOORD0)
    {
        vb->setTexCoord0Data(mTexcoordsp.get());
    }

    if (attribute_mask & LLVertexBuffer::MAP_COLOR)
    {
        vb->setColorData(mColorsp.get());
    }

#if LL_DARWIN
    // unmapBuffer creates the GL buffer, uploads, and leaves it bound;
    // drawBuffer's later setBuffer() then runs setupVertexBuffer against
    // a valid VBO.
    vb->unmapBuffer();
#endif
    vb->unbind();

    return vb;
}

void LLRender::drawBuffer(LLVertexBuffer* vb, U32 mode, S32 count)
{
    vb->setBuffer();
    vb->drawArrays(mode, 0, count);
}

void LLRender::resetStriders(S32 count)
{
    mVerticesp[0] = mVerticesp[count];
    mTexcoordsp[0] = mTexcoordsp[count];
    mColorsp[0] = mColorsp[count];

    mCount = 0;
}

void LLRender::vertex3f(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
    //the range of mVerticesp, mColorsp and mTexcoordsp is [0, 4095]
    if (mCount > 2048)
    { //break when buffer gets reasonably full to keep GL command buffers happy and avoid overflow below
        switch (mMode)
        {
            case LLRender::POINTS: flush(); break;
            case LLRender::TRIANGLES: if (mCount%3==0) flush(); break;
            case LLRender::LINES: if (mCount%2 == 0) flush(); break;
        }
    }

    if (mCount > 4094)
    {
    //  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
        return;
    }

    if (mUIOffset.empty())
    {
        mVerticesp[mCount].set(x,y,z);
    }
    else
    {
        LLVector4a vert(x, y, z);
        vert.add(mUIOffset.back());
        vert.mul(mUIScale.back());
        mVerticesp[mCount] = vert;
    }

    mCount++;
    mVerticesp[mCount] = mVerticesp[mCount-1];
    mColorsp[mCount] = mColorsp[mCount-1];
    mTexcoordsp[mCount] = mTexcoordsp[mCount-1];
}

void LLRender::vertexBatchPreTransformed(LLVector4a* verts, S32 vert_count)
{
    if (mCount + vert_count > 4094)
    {
        //  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
        return;
    }

    for (S32 i = 0; i < vert_count; i++)
    {
        mVerticesp[mCount] = verts[i];

        mCount++;
        mTexcoordsp[mCount] = mTexcoordsp[mCount-1];
        mColorsp[mCount] = mColorsp[mCount-1];
    }

    if( mCount > 0 ) // ND: Guard against crashes if mCount is zero, yes it can happen
        mVerticesp[mCount] = mVerticesp[mCount-1];
}

void LLRender::vertexBatchPreTransformed(LLVector4a* verts, LLVector2* uvs, S32 vert_count)
{
    if (mCount + vert_count > 4094)
    {
        //  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
        return;
    }

    for (S32 i = 0; i < vert_count; i++)
    {
        mVerticesp[mCount] = verts[i];
        mTexcoordsp[mCount] = uvs[i];

        mCount++;
        mColorsp[mCount] = mColorsp[mCount-1];
    }

    if (mCount > 0)
    {
        mVerticesp[mCount] = mVerticesp[mCount - 1];
        mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
    }
}

void LLRender::vertexBatchPreTransformed(LLVector4a* verts, LLVector2* uvs, LLColor4U* colors, S32 vert_count)
{
    if (mCount + vert_count > 4094)
    {
        //  LL_WARNS() << "GL immediate mode overflow.  Some geometry not drawn." << LL_ENDL;
        return;
    }

    for (S32 i = 0; i < vert_count; i++)
    {
        mVerticesp[mCount] = verts[i];
        mTexcoordsp[mCount] = uvs[i];
        mColorsp[mCount] = colors[i];

        mCount++;
    }

    if (mCount > 0)
    {
        mVerticesp[mCount] = mVerticesp[mCount - 1];
        mTexcoordsp[mCount] = mTexcoordsp[mCount - 1];
        mColorsp[mCount] = mColorsp[mCount - 1];
    }
}

void LLRender::vertex2i(const GLint& x, const GLint& y)
{
    vertex3f((GLfloat) x, (GLfloat) y, 0);
}

void LLRender::vertex2f(const GLfloat& x, const GLfloat& y)
{
    vertex3f(x,y,0);
}

void LLRender::vertex2fv(const GLfloat* v)
{
    vertex3f(v[0], v[1], 0);
}

void LLRender::vertex3fv(const GLfloat* v)
{
    vertex3f(v[0], v[1], v[2]);
}

void LLRender::texCoord2f(const GLfloat& x, const GLfloat& y)
{
    mTexcoordsp[mCount] = LLVector2(x,y);
}

void LLRender::texCoord2i(const GLint& x, const GLint& y)
{
    texCoord2f((GLfloat) x, (GLfloat) y);
}

void LLRender::texCoord2fv(const GLfloat* tc)
{
    texCoord2f(tc[0], tc[1]);
}

void LLRender::color4ub(const GLubyte& r, const GLubyte& g, const GLubyte& b, const GLubyte& a)
{
    if (!LLGLSLShader::sCurBoundShaderPtr || LLGLSLShader::sCurBoundShaderPtr->mAttributeMask & LLVertexBuffer::MAP_COLOR)
    {
        mColorsp[mCount] = LLColor4U(r,g,b,a);
    }
    else
    { //not using shaders or shader reads color from a uniform
        diffuseColor4ub(r,g,b,a);
    }
}
void LLRender::color4ubv(const GLubyte* c)
{
    color4ub(c[0], c[1], c[2], c[3]);
}

void LLRender::color4f(const GLfloat& r, const GLfloat& g, const GLfloat& b, const GLfloat& a)
{
    color4ub((GLubyte) (llclamp(r, 0.f, 1.f)*255),
        (GLubyte) (llclamp(g, 0.f, 1.f)*255),
        (GLubyte) (llclamp(b, 0.f, 1.f)*255),
        (GLubyte) (llclamp(a, 0.f, 1.f)*255));
}

void LLRender::color4fv(const GLfloat* c)
{
    color4f(c[0],c[1],c[2],c[3]);
}

void LLRender::color3f(const GLfloat& r, const GLfloat& g, const GLfloat& b)
{
    color4f(r,g,b,1);
}

void LLRender::color3fv(const GLfloat* c)
{
    color4f(c[0],c[1],c[2],1);
}

void LLRender::diffuseColor3f(F32 r, F32 g, F32 b)
{
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader != NULL);

    if (shader)
    {
        shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r,g,b,1.f);
    }
}

void LLRender::diffuseColor3fv(const F32* c)
{
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader != NULL);

    if (shader)
    {
        shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0], c[1], c[2], 1.f);
    }
}

void LLRender::diffuseColor4f(F32 r, F32 g, F32 b, F32 a)
{
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader != NULL);

    if (shader)
    {
        shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r,g,b,a);
    }
}

void LLRender::diffuseColor4fv(const F32* c)
{
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader != NULL);

    if (shader)
    {
        shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, c);
    }
}

void LLRender::diffuseColor4ubv(const U8* c)
{
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader != NULL);

    if (shader)
    {
        shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, c[0]/255.f, c[1]/255.f, c[2]/255.f, c[3]/255.f);
    }
}

void LLRender::diffuseColor4ub(U8 r, U8 g, U8 b, U8 a)
{
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader != NULL);

    if (shader)
    {
        shader->uniform4f(LLShaderMgr::DIFFUSE_COLOR, r/255.f, g/255.f, b/255.f, a/255.f);
    }
}

void LLRender::setLineWidth(F32 width)
{
    gGL.flush();

    width = llclamp(width, gGLManager.mAliasedLineRange[0], gGLManager.mAliasedLineRange[1]);
    if(mLineWidth != width)
    {
        mLineWidth = width;
        glLineWidth(width);
    }
}

void LLRender::setPolygonOffset(F32 factor, F32 units)
{
    if (mPolygonOffsetFactor != factor || mPolygonOffsetUnits != units)
    {
        mPolygonOffsetFactor = factor;
        mPolygonOffsetUnits = units;
        flush();

        const F32 sign = sReverseZ ? -1.f : 1.f;
        glPolygonOffset(sign * factor, sign * units);
    }
}

void LLRender::rebasePolygonOffset()
{
    flush();

    const F32 sign = sReverseZ ? -1.f : 1.f;
    glPolygonOffset(sign * mPolygonOffsetFactor, sign * mPolygonOffsetUnits);
}

void LLRender::debugTexUnits(void)
{
    LL_INFOS("TextureUnit") << "Active TexUnit: " << mCurrTextureUnitIndex << LL_ENDL;
    std::string active_enabled = "false";
    for (U32 i = 0; i < mTextureSlots.size(); i++)
    {
        if (getTextureSlot(i)->mCurrTexType != ALTextureSlot::TT_NONE)
        {
            if (i == mCurrTextureUnitIndex) active_enabled = "true";
            LL_INFOS("TextureUnit") << "TexUnit: " << i << " Enabled" << LL_ENDL;
            LL_INFOS("TextureUnit") << "Enabled As: " ;
            switch (getTextureSlot(i)->mCurrTexType)
            {
                case ALTextureSlot::TT_TEXTURE:
                    LL_CONT << "Texture 2D";
                    break;
                case ALTextureSlot::TT_RECT_TEXTURE:
                    LL_CONT << "Texture Rectangle";
                    break;
                case ALTextureSlot::TT_CUBE_MAP:
                    LL_CONT << "Cube Map";
                    break;
                default:
                    LL_CONT << "ARGH!!! NONE!";
                    break;
            }
            LL_CONT << ", Texture Bound: " << getTextureSlot(i)->mCurrTexture << LL_ENDL;
        }
    }
    LL_INFOS("TextureUnit") << "Active TexUnit Enabled : " << active_enabled << LL_ENDL;
}

glm::mat4 get_current_modelview()
{
    return glm::make_mat4(gGLModelView);
}

glm::mat4 get_current_projection()
{
    return glm::make_mat4(gGLProjection);
}

glm::mat4 get_last_modelview()
{
    return glm::make_mat4(gGLLastModelView);
}

glm::mat4 get_last_projection()
{
    return glm::make_mat4(gGLLastProjection);
}

void copy_matrix(const glm::mat4& src, F32* dst)
{
    auto matp = glm::value_ptr(src);
    for (U32 i = 0; i < 16; i++)
    {
        dst[i] = matp[i];
    }
}

void set_current_modelview(const glm::mat4& mat)
{
    copy_matrix(mat, gGLModelView);
}

void set_current_projection(const glm::mat4& mat)
{
    copy_matrix(mat, gGLProjection);
}

void set_last_modelview(const glm::mat4& mat)
{
    copy_matrix(mat, gGLLastModelView);
}

void set_last_projection(const glm::mat4& mat)
{
    copy_matrix(mat, gGLLastProjection);
}

glm::mat4 al_reverse_z_transform(const glm::mat4& p)
{
    // z_ndc' = (1 - z_ndc)/2  =>  row2' = 0.5*(row3 - row2). glm is column-major, so
    // row i is spread across mat[c][i]. Leaves xy and w rows untouched.
    glm::mat4 r = p;
    for (int c = 0; c < 4; ++c)
    {
        r[c][2] = 0.5f * (p[c][3] - p[c][2]);
    }
    return r;
}

glm::mat4 al_perspective(F32 fovy_rad, F32 aspect, F32 z_near, F32 z_far)
{
    // Forward branch is exactly glm::perspective (byte-identical to pre-reverse-Z callers).
    glm::mat4 p = glm::perspective(fovy_rad, aspect, z_near, z_far);
    return LLRender::sReverseZ ? al_reverse_z_transform(p) : p;
}

glm::mat4 al_ortho(F32 left, F32 right, F32 bottom, F32 top, F32 z_near, F32 z_far)
{
    glm::mat4 p = glm::ortho(left, right, bottom, top, z_near, z_far);
    return LLRender::sReverseZ ? al_reverse_z_transform(p) : p;
}

glm::vec3 al_project(const glm::vec3& obj, const glm::mat4& modelview, const glm::mat4& proj, const glm::ivec4& viewport)
{
    // Under reverse-Z the projection already yields [0,1] window z, so use the ZO variant
    // that does not re-apply the [-1,1]->[0,1] remap.
    return LLRender::sReverseZ ? glm::projectZO(obj, modelview, proj, viewport)
                               : glm::project(obj, modelview, proj, viewport);
}

glm::vec3 al_unproject(const glm::vec3& win, const glm::mat4& modelview, const glm::mat4& proj, const glm::ivec4& viewport)
{
    return LLRender::sReverseZ ? glm::unProjectZO(win, modelview, proj, viewport)
                               : glm::unProject(win, modelview, proj, viewport);
}

glm::vec3 mul_mat4_vec3(const glm::mat4& mat, const glm::vec3& vec)
{
#if 1 // SIMD path results in strange crashes. Fall back to scalar for now.
    const float w = vec[0] * mat[0][3] + vec[1] * mat[1][3] + vec[2] * mat[2][3] + mat[3][3];
    return glm::vec3(
       (vec[0] * mat[0][0] + vec[1] * mat[1][0] + vec[2] * mat[2][0] + mat[3][0]) / w,
       (vec[0] * mat[0][1] + vec[1] * mat[1][1] + vec[2] * mat[2][1] + mat[3][1]) / w,
       (vec[0] * mat[0][2] + vec[1] * mat[1][2] + vec[2] * mat[2][2] + mat[3][2]) / w
    );
#else
    LLVector4a x, y, z, s, t, p, q;

    x.splat(vec.x);
    y.splat(vec.y);
    z.splat(vec.z);

    s.splat<3>(mat[0].data);
    t.splat<3>(mat[1].data);
    p.splat<3>(mat[2].data);
    q.splat<3>(mat[3].data);

    s.mul(x);
    t.mul(y);
    p.mul(z);
    q.add(s);
    t.add(p);
    q.add(t);

    x.mul(mat[0].data);
    y.mul(mat[1].data);
    z.mul(mat[2].data);

    x.add(y);
    z.add(mat[3].data);
    LLVector4a res;
    res.load3(glm::value_ptr(vec));
    res.setAdd(x, z);
    res.div(q);
    return glm::make_vec3(res.getF32ptr());
#endif
}
