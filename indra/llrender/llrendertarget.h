/**
 * @file llrendertarget.h
 * @brief Off screen render target abstraction.  Loose wrapper for GL_EXT_framebuffer_objects.
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

#ifndef LL_LLRENDERTARGET_H
#define LL_LLRENDERTARGET_H

// LLRenderTarget is unavailible on the mapserver since it uses FBOs.

#include "llgl.h"
#include "llrender.h"

/*
 Wrapper around OpenGL framebuffer objects for use in render-to-texture

 SAMPLE USAGE:

    LLRenderTarget target;

    ...

    //allocate a 256x256 RGBA render target with depth buffer
    target.allocate(256,256,GL_RGBA,true);

    //render to contents of offscreen buffer
    target.bindTarget();
    target.clear();
    ... <issue drawing commands> ...
    target.flush();

    ...

    //use target as a texture
    gGL.getTextureSlot(INDEX)->bind(&target);
    ... <issue drawing commands> ...

*/

class LLRenderTarget
{
public:
    // Whether or not to use FBO implementation
    static bool sUseFBO;
    static U32 sBytesAllocated;
    static U32 sCurFBO;
    static U32 sCurResX;
    static U32 sCurResY;

    // Format options for the depth buffer.
    // Combined depth+stencil is selected automatically when stencil is requested.
    enum eDepthFormat : U32
    {
        DEPTH_FMT_24 = 0,   // GL_DEPTH_COMPONENT24 / GL_DEPTH24_STENCIL8
        DEPTH_FMT_32F,      // GL_DEPTH_COMPONENT32F / GL_DEPTH32F_STENCIL8
    };

    // Whether this target's colour attachments carry a mip chain, and who fills it.
    //
    // Lived on the texture-unit class as eTextureMipGeneration, where it had nothing to do
    // with binding and no other consumer. It is an allocation policy for a render target.
    enum eMipGeneration : U32
    {
        MIPS_NONE = 0,      // Single level. Attachments are not mipmapped.
        MIPS_AUTO,          // Levels allocated, and regenerated on flush().
        MIPS_MANUAL,        // Levels allocated; the owner regenerates them when it chooses.
    };

    LLRenderTarget();
    ~LLRenderTarget();

    //allocate resources for rendering
    //must be called before use
    //multiple calls will release previously allocated resources
    // resX - width
    // resY - height
    // color_fmt - GL color format (e.g. GL_RGB)
    // depth - if true, allocate a depth buffer
    // stencil - if true, allocate a combined depth+stencil buffer (requires depth)
    // usage - deprecated, should always be TT_TEXTURE
    // depth_fmt - bit depth for the depth component (ignored unless depth is true)
    bool allocate(U32 resx, U32 resy, U32 color_fmt, bool depth = false, bool stencil = false, ALTextureSlot::eTextureType usage = ALTextureSlot::TT_TEXTURE, eMipGeneration generateMipMaps = MIPS_NONE, eDepthFormat depth_fmt = DEPTH_FMT_24);

    //resize existing attachments to use new resolution and color format
    // CAUTION: if the GL runs out of memory attempting to resize, this render target will be undefined
    // DO NOT use for screen space buffers or for scratch space for an image that might be uploaded
    // DO use for render targets that resize often and aren't likely to ruin someone's day if they break
    void resize(U32 resx, U32 resy);

    //point this render target at a particular LLImageGL
    //   Intended usage:
    //      LLRenderTarget target;
    //      target.addColorAttachment(image);
    //      target.bindTarget();
    //      < issue GL calls>
    //      target.flush();
    //      target.releaseColorAttachment();
    //
    // attachment -- LLImageGL to render into
    // use_name -- optional texture name to target instead of attachment->getTexName()
    // NOTE: setColorAttachment and releaseColorAttachment cannot be used in conjuction with
    // addColorAttachment, allocateDepth, resize, etc.
    void setColorAttachment(LLImageGL* attachment, LLGLuint use_name = 0);

    // detach from current color attachment
    void releaseColorAttachment();

    //add color buffer attachment
    //limit of 4 color attachments per render target
    bool addColorAttachment(U32 color_fmt);

    //allocate a depth texture
    bool allocateDepth();

    //share depth buffer with provided render target
    void shareDepthBuffer(LLRenderTarget& target);

    //free any allocated resources
    //safe to call redundantly
    // asserts that this target is not currently bound or present in the RT stack
    void release();

    //bind target for rendering
    //applies appropriate viewport
    //  If an LLRenderTarget is currently bound, stores a reference to that LLRenderTarget
    //  and restores previous binding on flush() (maintains a stack of Render Targets)
    //  Asserts that this target is not currently bound in the stack
    void bindTarget();

    //clear render targer, clears depth buffer if present,
    //uses scissor rect if in copy-to-texture mode
    // asserts that this target is currently bound
    void clear(U32 mask = 0xFFFFFFFF);

    //get applied viewport
    void getViewport(S32* viewport);

    // Restrict draw output to the first `count` colour attachments; 0 restores all of them.
    // Asserts that this target is currently bound.
    //
    // A fragment shader that declares a single output leaves every OTHER attachment of the
    // bound FBO undefined, and with blending enabled the driver mixes that undefined value
    // into targets the pass never meant to touch. Narrowing is how a multi-attachment target
    // hosts a single-output pass -- the impostor bake runs the forward alpha passes into its
    // own G-buffer, so without this its normal and ORM attachments are corrupted wherever
    // blended geometry drew, and those are what light the billboard afterwards.
    void setDrawBuffers(U32 count = 0);

    //get X resolution
    U32 getWidth() const { return mResX; }

    //get Y resolution
    U32 getHeight() const { return mResY; }

    ALTextureSlot::eTextureType getUsage(void) const { return mUsage; }

    U32 getTexture(U32 attachment = 0) const;
    U32 getNumTextures() const;

    U32 getDepth(void) const { return mDepth; }
    bool hasStencil() const { return mStencil; }
    eDepthFormat getDepthFormat() const { return mDepthFormat; }

    // The sampler a caller gets when it does not name one.
    //
    // These reproduce, as sampler objects, the filter and wrap state that allocation used to
    // write onto the texture objects themselves -- bilinear for attachment 0, point for the
    // data attachments behind it, mirrored repeat except on rectangle textures, and point
    // with plain repeat for depth. That state was the last texture-object sampling state left
    // in the engine, and it stayed only because ALTextureSlot::bind(LLRenderTarget*) defaults its
    // sampler to 0, meaning "sample through whatever the texture object carries".
    //
    // Answering the question here rather than at those call sites keeps the defaults in one
    // place, next to the allocation that used to establish them. A caller that wants
    // something else still passes its own sampler, or uses bindTexture.
    U32 getDefaultColorSampler(U32 attachment = 0) const;
    U32 getDefaultDepthSampler() const;

    // Bind an attachment for sampling.
    //
    // The key selects an immutable sampler object rather than writing state onto the
    // texture, so two passes can sample the same attachment differently in one frame --
    // which the post-process chain does constantly.
    //
    // An unnamed key resolves through getDefaultColorSampler for THIS attachment --
    // bilinear for 0, point for the data attachments -- rather than a flat constant that
    // would quietly answer the question differently from the policy function.
    void bindTexture(U32 index, S32 channel, ALSampler key = ALSamplers::TargetDefault);

    //flush rendering operations
    //must be called when rendering is complete
    //should be used 1:1 with bindTarget
    // call bindTarget once, do all your rendering, call flush once
    // If an LLRenderTarget was bound when bindTarget was called, binds that RenderTarget for rendering (maintains RT stack)
    // asserts  that this target is currently bound
    void flush();

    void copyContents(LLRenderTarget& source, S32 srcX0, S32 srcY0, S32 srcX1, S32 srcY1, S32 dstX0, S32 dstY0, S32 dstX1, S32 dstY1,
                      U32 mask, U32 filter);

    static void copyContentsToFramebuffer(LLRenderTarget& source, S32 srcX0, S32 srcY0, S32 srcX1, S32 srcY1, S32 dstX0, S32 dstY0,
                                          S32 dstX1, S32 dstY1, U32 mask, U32 filter);

    //Returns TRUE if target is ready to be rendered into.
    //That is, if the target has been allocated with at least
    //one renderable attachment (i.e. color buffer, depth buffer).
    bool isComplete() const;

    // Returns true if this RenderTarget is bound somewhere in the stack
    bool isBoundInStack() const;

    static LLRenderTarget* getCurrentBoundTarget() { return sBoundTarget; }

    // *HACK
    void swapFBORefs(LLRenderTarget& other);

    static LLRenderTarget* sBoundTarget;

protected:
    U32 mResX;
    U32 mResY;
    std::vector<U32> mTex;
    std::vector<U32> mInternalFormat;
    U32 mFBO;
    LLRenderTarget* mPreviousRT = nullptr;

    U32 mDepth;
    bool mUseDepth;
    bool mStencil;
    eDepthFormat mDepthFormat;
    eMipGeneration mGenerateMipMaps;
    U32 mMipLevels;

    ALTextureSlot::eTextureType mUsage;
};

#endif

