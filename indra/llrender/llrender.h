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
#include "algpubuffer.h"     // AL_NUM_STORAGE_BINDINGS -- the storage binding points this context keeps
#include "alframefences.h"
#include "alimageslot.h"
#include "alquerypool.h"
#include "altextureslot.h"
#include "aluniformbuffer.h"

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

namespace ll_test { struct RenderProbe; }

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
        PATCHES,    // tessellation input; vertices per patch set with glPatchParameteri
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

    // Memory barrier bits, GL's own values, so a caller states exactly which later reads must see
    // earlier shader writes.
    enum eBarrier : U32
    {
        BARRIER_VERTEX_ATTRIB      = GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT,
        BARRIER_ELEMENT_ARRAY      = GL_ELEMENT_ARRAY_BARRIER_BIT,
        BARRIER_UNIFORM            = GL_UNIFORM_BARRIER_BIT,
        BARRIER_TEXTURE_FETCH      = GL_TEXTURE_FETCH_BARRIER_BIT,
        BARRIER_IMAGE              = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT,
        BARRIER_COMMAND            = GL_COMMAND_BARRIER_BIT,
        BARRIER_PIXEL_BUFFER       = GL_PIXEL_BUFFER_BARRIER_BIT,
        BARRIER_TEXTURE_UPDATE     = GL_TEXTURE_UPDATE_BARRIER_BIT,
        BARRIER_BUFFER_UPDATE      = GL_BUFFER_UPDATE_BARRIER_BIT,
        BARRIER_FRAMEBUFFER        = GL_FRAMEBUFFER_BARRIER_BIT,
        BARRIER_TRANSFORM_FEEDBACK = GL_TRANSFORM_FEEDBACK_BARRIER_BIT,
        BARRIER_ATOMIC_COUNTER     = GL_ATOMIC_COUNTER_BARRIER_BIT,
        BARRIER_STORAGE            = GL_SHADER_STORAGE_BARRIER_BIT,
        BARRIER_CLIENT_MAPPED      = GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT,
        BARRIER_QUERY_BUFFER       = GL_QUERY_BUFFER_BARRIER_BIT,
        BARRIER_ALL                = GL_ALL_BARRIER_BITS,
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
    void loadMatrix(const LLMatrix4a& m);
    void loadIdentity();
    void multMatrix(const GLfloat* m);
    void multMatrix(const LLMatrix4a& m);
    void matrixMode(eMatrixMode mode);
    eMatrixMode getMatrixMode();

    const LLMatrix4a& getModelviewMatrix();
    const LLMatrix4a& getProjectionMatrix();

    void syncMatrices();
    void syncLightState();

    // ---- Shared forward-lighting uniform block (UB_LIGHTS) --------------------------------
    // The fixed-function light arrays every forward/alpha program reads. They used to be
    // pushed as LOOSE uniforms into each program on its first bind after mLightHash moved --
    // the same ~672 bytes re-derived and re-written once per program. Packed once and bound at
    // a fixed engine point instead, so a light-state change costs one upload total.
    //
    // Holds ONLY what LLRender owns exclusively. sun_up_factor stays LOOSE: it is written from
    // many sites across the pipeline, draw pools and LLSettingsVO, and a shared block would let
    // one writer silently stomp another's value. The sun's, moon's and ambient light the
    // atmospherics read are the Environment block's (LLSettingsVOSky::applySpecial).
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
    // Matrices go up as they lie: LLMatrix4a's rows are the columns std140 reads by default.
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
    // The width setLineWidth() left GL with, which only it sets.
    F32  getLineWidth() const { return mLineWidth; }

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

    // Control points per PATCHES primitive, read by the tessellation control stage.
    // Redundant sets are dropped without a flush.
    void setPatchVertices(U32 count);

    // How polygons rasterize, front and back alike, the only face a core context takes: GL_FILL, GL_LINE or
    // GL_POINT. A change flushes what is batched under the old mode; redundant sets are dropped without one.
    void setPolygonMode(U32 mode);
    U32  getPolygonMode() const { return mPolygonMode; }

    // The faces GL_CULL_FACE (an LLGLState) culls: GL_BACK, GL_FRONT or GL_FRONT_AND_BACK. A change flushes what is
    // batched; redundant sets are dropped without one.
    void setCullFace(U32 face);
    U32  getCullFace() const { return mCullFace; }

    // The diameter in pixels points rasterize at while the program writes no gl_PointSize. A change flushes what is
    // batched; redundant sets are dropped without one.
    void setPointSize(F32 size);
    F32  getPointSize() const { return mPointSize; }

    // The stencil test GL_STENCIL_TEST (an LLGLState) runs, front and back alike: its function, reference and the
    // bits it compares, what each outcome does to the stencil, and the bits a draw or a clear may write. Each setter
    // flushes what is batched on a change and drops a redundant set without one. ALGLStencil scopes all three.
    void setStencilFunc(U32 func, S32 ref, U32 mask);
    void setStencilOp(U32 stencil_fail, U32 depth_fail, U32 pass);
    void setStencilMask(U32 mask);
    void getStencilFunc(U32& func, S32& ref, U32& mask) const
    {
        func = mStencilFunc;
        ref  = mStencilRef;
        mask = mStencilReadMask;
    }
    void getStencilOp(U32& stencil_fail, U32& depth_fail, U32& pass) const
    {
        stencil_fail = mStencilFail;
        depth_fail   = mStencilDepthFail;
        pass         = mStencilPass;
    }
    U32  getStencilMask() const { return mStencilWriteMask; }

    // How blending combines source and destination, the colour by `rgb` and alpha by `alpha` (GL_FUNC_ADD, GL_MAX
    // and the rest), for every draw buffer. A change flushes what is batched; redundant sets are dropped without one.
    void setBlendEquation(U32 rgb, U32 alpha);
    void getBlendEquation(U32& rgb, U32& alpha) const
    {
        rgb   = mBlendEquationRGB;
        alpha = mBlendEquationAlpha;
    }

    // The viewport, in pixels of the bound framebuffer. Every writer goes through here, LLRenderTarget's binds
    // included; a change flushes what is batched under the old viewport, and redundant sets are dropped without one.
    void setViewport(S32 x, S32 y, S32 width, S32 height);
    // The viewport in force: x, y, width, height. Width is negative until one is set in this context, which GL starts
    // at the size of the window it was made for.
    void getViewport(S32 (&box)[4]) const
    {
        box[0] = mViewport[0];
        box[1] = mViewport[1];
        box[2] = mViewport[2];
        box[3] = mViewport[3];
    }

    // Issue the clip control and the depth clear value sReverseZ calls for: zero-to-one clip depth cleared to 0
    // under reverse-Z, minus-one-to-one cleared to 1 otherwise, clip control only where the context has it
    // (gGLManager.mHasClipControl). Both are issued whether or not they changed.
    static void applyDepthConvention();

    // Reads the `width` x `height` rectangle at (x, y) of the framebuffer bound for reading into `data`, as
    // glReadPixels reads it in `format` and `type` under the pack alignment of 1 init() set, after what is batched
    // has been drawn. Synchronous: it waits for the GPU, and for a threaded driver's own thread, to finish everything
    // issued before it. For a read made once on purpose, a snapshot or a click's probe; anything read every frame
    // reads through ALGPUReadback.
    void readPixels(S32 x, S32 y, S32 width, S32 height, U32 format, U32 type, void* data);

    // The colour glClear fills colour buffers with. Every writer in the tree goes through
    // here, so the cache is the state. Redundant sets are dropped; nothing batched reads it,
    // so a change needs no flush.
    void setClearColor(const LLColor4& color);
    const LLColor4& getClearColor() const { return mClearColor; }

    // Clears the bound framebuffer's buffers named in mask (GL_COLOR_BUFFER_BIT and the rest)
    // through the write masks, scissor box and clear values in force, after what is batched
    // has been drawn.
    void clear(U32 mask);

    // Draws `instances` copies of `count` vertices from `first` with no vertex arrays: the bound program builds every
    // position from gl_VertexID and gl_InstanceID and declares no inputs, so binding it enabled none. What is batched
    // is drawn first, and the matrices are synced as LLVertexBuffer's draws sync them.
    void drawArraysInstanced(eGeomModes mode, U32 first, U32 count, U32 instances);

    // Draws from the ALDrawArraysIndirectCommand at `offset` in `buffer` (a multiple of 4), with no vertex arrays, as
    // drawArraysInstanced does: the command's count and instances are whatever wrote them, the GPU's own compaction
    // most often. GL 4.0, so it needs no compute -- but a command a shader wrote is read only past BARRIER_COMMAND.
    void drawArraysIndirect(eGeomModes mode, const ALGPUBuffer& buffer, size_t offset);

    // Makes the draws after it read what the draws before it wrote into a texture the bound framebuffer has attached,
    // as binding another framebuffer would: a pass that samples an attachment its target's earlier draws wrote reads it
    // only past one. Without it a driver may answer from caches the writes have not reached, as radeonsi answers a
    // depth from the tile metadata it held before them. Needs gGLManager.mHasTextureBarrier (glTextureBarrier). What is
    // batched is drawn first.
    void textureBarrier();

    // ---- Compute ---------------------------------------------------------------------------------------------------
    // Everything here needs sCompute and asserts it.

    // Make later reads of the kinds in `barriers` (eBarrier bits, or-ed) see what shaders wrote before it -- storage
    // buffers, images and atomics are not ordered against anything else. What is batched is drawn first.
    void memoryBarrier(U32 barriers);

    // Runs the bound compute program over groups_x * groups_y * groups_z work groups. What is batched is drawn first
    // and the matrices are synced, as for a draw. Zero groups dispatch nothing, and a count past the context's limit
    // warns once and dispatches nothing. LLGLSLShader::dispatch and dispatchThreads are the usual way here.
    void dispatchCompute(U32 groups_x, U32 groups_y, U32 groups_z);

    // As dispatchCompute, the group counts read from the ALDispatchIndirectCommand at `offset` in `buffer` (a multiple
    // of 4) -- written on the GPU, so a pass sizes the next without a read back. A command a shader wrote is read only
    // past BARRIER_COMMAND.
    void dispatchComputeIndirect(const ALGPUBuffer& buffer, size_t offset);

    // Attach `buffer`, whole or [offset, offset + bytes), to shader storage binding point `binding`, below
    // AL_NUM_STORAGE_BINDINGS. The points are kept as the texture and image units are: a bind of what a point holds
    // issues nothing, refreshState re-issues them, and deleting a buffer forgets it. A reserved block's point is the
    // same in every program (LLShaderMgr::getStorageBlockBinding), so a buffer bound there serves each in turn.
    // `offset` is a multiple of gGLManager.mShaderStorageBufferOffsetAlignment (asserted).
    void bindStorageBuffer(U32 binding, const ALGPUBuffer& buffer);
    void bindStorageBuffer(U32 binding, const ALGPUBuffer& buffer, size_t offset, size_t bytes);
    // Leave nothing at `binding`.
    void unbindStorageBuffer(U32 binding);
    // The buffer at `binding`, 0 for none or for a point past AL_NUM_STORAGE_BINDINGS.
    U32 getStorageBuffer(U32 binding) const;

    // ---- Frames ----------------------------------------------------------------------------------------------------

    // This context's queries: the viewer's display() retires them before its first cull reads them. Released with the
    // context in shutdown().
    ALQueryPool& queries() { return mQueries; }

    // The frames of this context the GPU has finished, from the one fence endFrame() lays a frame. Released with the
    // context in shutdown().
    ALFrameFences& frameFences() { return mFrameFences; }

    // Ends the frame for what is kept a frame at a time: the query pool's batch closes, the frame's fence is laid, and
    // the uniform buffer rings close their counters and resize. Once a frame, straight after the swap; the main
    // context's only. `frames_ahead`, above 0, paces the CPU: the frame that many before this one is waited for
    // (ALFrameFences::endFrame), on the frames a user sees.
    void endFrame(U32 frames_ahead = 0);

    // The scissor box, in pixels of the bound framebuffer, that GL_SCISSOR_TEST (an
    // LLGLState) cuts to. Every writer goes through here; a change flushes what is batched
    // under the old box, and redundant sets are dropped without one. See LLGLSScissor.
    void setScissor(S32 x, S32 y, S32 width, S32 height);
    // The box in force: x, y, width, height. Width is negative until a box is set in this
    // context, which GL starts at the size of the window it was made for.
    void getScissor(S32 (&box)[4]) const
    {
        box[0] = mScissor[0];
        box[1] = mScissor[1];
        box[2] = mScissor[2];
        box[3] = mScissor[3];
    }

    ALTextureSlot* getTextureSlot(U32 index);

    // The image unit `index`, or a slot that binds nothing past AL_NUM_IMAGE_SLOTS.
    ALImageSlot* getImageSlot(U32 index);

    // Drop these texture names, about to be deleted in this context, from every slot's
    // bind cache, the image slots' included. Deleting a bound texture returns that binding to 0, and GL may hand the
    // name out again; a slot still caching it would take the new texture for bound already
    // and skip the bind.
    void forgetTextures(S32 count, const U32* names);

    // The same for a buffer name about to be deleted, which the indirect draw and dispatch bindings and the storage
    // binding points cache. ALGPUBuffer::release calls it.
    void forgetBuffer(U32 name);

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
    // The draws issued, through LLVertexBuffer and LLRender's own, and the vertices or indices they took, since the
    // frame's statistics were last reset (LLPipeline::resetFrameStats).
    static U32 sDrawCalls;
    static U64 sDrawElements;
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

    // Compute is in use this session: the context has it (gGLManager.mHasCompute), GLSL is 4.30 after
    // whatever fallback the feature manager applied, and the user has not turned it off. Latched by newview
    // at shader load (llrender cannot read settings), as sReverseZ is. Everything compute in LLRender,
    // ALImageSlot and ALGPUBuffer's storage binds asserts it.
    static bool sCompute;

    // The GBuffer normal attachment is GL_RGBA16 when the deferred targets are HDR and
    // GL_RGB10_A2 when they are not, which changes how many bits its blue channel has to spend
    // on the packed geometric normal -- 16 versus 10. Latched by newview at shader load, before
    // anything compiles (llrender cannot read settings), and the attachment's format is then picked
    // from it, because a shader compiled against the wrong assumption unpacks noise.
    static bool sGBufferNormHDR;

private:
    friend class LLLightState;
    // The tests' state guard reads how deep each matrix stack is.
    friend struct ll_test::RenderProbe;

    LLVertexBuffer* bufferfromCache(U32 attribute_mask, U32 count);
    LLVertexBuffer* genBuffer(U32 attribute_mask, S32 count);
    void drawBuffer(LLVertexBuffer* vb, U32 mode, S32 count);
    void resetStriders(S32 count);

    eMatrixMode mMatrixMode;
    U32 mMatIdx[NUM_MATRIX_MODES];
    U32 mMatHash[NUM_MATRIX_MODES];
    LLMatrix4a mMatrix[NUM_MATRIX_MODES][LL_MATRIX_STACK_DEPTH];
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

    ALFrameFences mFrameFences;
    ALQueryPool   mQueries{ mFrameFences };

    // Derived matrices, cached across epochs so a stack that did not move is not re-derived.
    // Members rather than function statics: they belong to this context, and shutdown() has to
    // be able to invalidate them along with the block.
    LLMatrix4a      mCachedInvMdv;
    LLMatrix4a      mCachedInvProj;
    LLMatrix4a      mCachedMVP;

    bool            mDirty;
    U32             mCount;
    U32             mMode;
    U32             mCurrTextureUnitIndex;
    bool            mCurrColorMask[4];
    F32             mLineWidth = 1.f;
    // Semantic (forward-convention) polygon offset; GL's own defaults are 0, 0.
    F32             mPolygonOffsetFactor = 0.f;
    F32             mPolygonOffsetUnits = 0.f;
    U32             mPatchVertices = 3; // GL's initial GL_PATCH_VERTICES
    LLColor4        mClearColor = LLColor4(0.f, 0.f, 0.f, 0.f); // GL's initial GL_COLOR_CLEAR_VALUE
    S32             mScissor[4] = { 0, 0, -1, -1 };              // unknown until set (see getScissor)
    S32             mViewport[4] = { 0, 0, -1, -1 };             // unknown until set (see getViewport)
    // The rest at GL's initial values, which init() issues.
    U32             mPolygonMode = GL_FILL;
    U32             mCullFace = GL_BACK;
    F32             mPointSize = 1.f;
    U32             mStencilFunc = GL_ALWAYS;
    S32             mStencilRef = 0;
    U32             mStencilReadMask = ~0u;
    U32             mStencilFail = GL_KEEP;
    U32             mStencilDepthFail = GL_KEEP;
    U32             mStencilPass = GL_KEEP;
    U32             mStencilWriteMask = ~0u;
    U32             mBlendEquationRGB = GL_FUNC_ADD;
    U32             mBlendEquationAlpha = GL_FUNC_ADD;

    // Issue the polygon mode, cull face, point size, stencil state and blend equation this holds, as init() and
    // refreshState() do.
    void issueRasterState() const;

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
    std::array<ALImageSlot, AL_NUM_IMAGE_SLOTS> mImageSlots;
    ALImageSlot             mDummyImageSlot;
    std::array<LLLightState, LL_NUM_LIGHT_UNITS> mLightState;

    // The buffers bound at GL_DRAW_INDIRECT_BUFFER and GL_DISPATCH_INDIRECT_BUFFER, so a run of draws or dispatches
    // from one buffer binds it once. UNKNOWN_BUFFER after refreshState: the next use binds whatever it wants.
    static constexpr U32 UNKNOWN_BUFFER = 0xFFFFFFFFu;
    U32 mDrawIndirectBuffer     = 0;
    U32 mDispatchIndirectBuffer = 0;
    // Bind `name` at `target`, which `bound` caches, unless it is there already.
    static void bindIndirectBuffer(U32 target, U32& bound, U32 name);

    // What each shader storage binding point holds: a buffer, and the range of it bound, mBytes 0 for the whole.
    struct StorageBinding
    {
        U32    mName   = 0;
        size_t mOffset = 0;
        size_t mBytes  = 0;
    };
    std::array<StorageBinding, AL_NUM_STORAGE_BINDINGS> mStorageBindings;
    void setStorageBinding(U32 binding, const StorageBinding& wanted);
    // Issue what `binding` holds to GL.
    void issueStorageBinding(U32 binding) const;

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

extern S32 gGLViewport[4];

extern thread_local LLRender gGL;

// --- Reverse-Z projection helpers (gated on LLRender::sReverseZ) --------------
// Rewrite a forward [-1,1] projection into reversed zero-to-one (near->1, far->0):
// z_ndc' = (1 - z_ndc)/2, i.e. the depth column becomes 0.5*(w column - depth column).
// Unconditional math; valid for any forward projection, including the hand-rolled
// shadow perspective whose depth rides an off-diagonal clip component.
LLMatrix4a al_reverse_z_transform(const LLMatrix4a& forward_proj);
// Perspective / ortho that emit reversed-ZO when sReverseZ, else the plain forward
// matrix: LLMatrix4a::perspective / ortho with the convention applied.
LLMatrix4a al_perspective(F32 fovy_rad, F32 aspect, F32 z_near, F32 z_far);
// z_far may be +infinity, which under reverse-Z gives the infinite projection (stored depth = near /
// distance, nothing clipped far). True for such a matrix, pick or zoom applied or not.
bool al_projection_is_infinite(const LLMatrix4a& proj);
LLMatrix4a al_ortho(F32 left, F32 right, F32 bottom, F32 top, F32 z_near, F32 z_far);
// project / unproject honoring the active convention: the zero-to-one forms under
// reverse-Z, since the projection already outputs [0,1] window z. The results carry
// w = 1.
LLVector4a al_project(const LLVector4a& obj, const LLMatrix4a& modelview, const LLMatrix4a& proj, const S32 viewport[4]);
LLVector4a al_unproject(const LLVector4a& win, const LLMatrix4a& modelview, const LLMatrix4a& proj, const S32 viewport[4]);
// The same through the inverse of modelview then projection, for a caller with several
// points against one camera.
LLVector4a al_unproject(const LLVector4a& win, const LLMatrix4a& inverse, const S32 viewport[4]);
// Window depth of the near / far plane under the active convention.
inline F32 al_window_near() { return LLRender::sReverseZ ? 1.f : 0.f; }
inline F32 al_window_far()  { return LLRender::sReverseZ ? 0.f : 1.f; }

#define LL_SHADER_LOADING_WARNS(...) LL_WARNS()

#endif
