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

#include "alrendertargetdesc.h"
#include "llgl.h"
#include "llrect.h"
#include "llrender.h"

#include <array>
#include <string>
#include <string_view>

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

class LLGLSLShader;
class LLVertexBuffer;

class LLRenderTarget
{
public:
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

    // What ensure did.
    enum class EnsureResult : U8
    {
        Kept,   // The target already matched: no GL call was made, and every name is the one it had.
        Built,  // The target was built from the description.
        Failed, // The target could not be built, and is released.
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

    // What this target's own storage is: its size, the formats of the colour attachments it allocated, the depth it
    // allocated, and its mips. A released target is the default description. A depth attachment shared from another
    // target is not part of it, nor is a texture lent by setColorAttachment.
    ALRenderTargetDesc desc() const;

    // Make this target what `desc` describes, with a depth attachment of its own where the description has one.
    //
    // A target that already is what `desc` describes, and holds nothing else -- no shared depth, no lent texture --
    // returns Kept, without a GL call. Any other is released and built from the description, as allocate and
    // addColorAttachment would build it, and returns Built. The size is held to the context's largest texture before
    // comparing, as allocate holds it, so a description past that size is kept rather than built again on every call.
    //
    // A description no target can be built from -- no size, no attachment, a format after a 0 in the colour list,
    // stencil without depth, generated mips of an integer first attachment or of none -- and one GL cannot build, return
    // Failed and leave the target released.
    EnsureResult ensure(const ALRenderTargetDesc& desc);

    // Make this target what `desc` describes, its depth attachment the one `depth_from` allocated. `desc` describes
    // this target's own storage, so it has no depth or stencil, and it is the size of depth_from.
    //
    // The colour is built only where it differs from the description, as ensure(desc) builds it, and the depth is
    // attached only where this target shares none or depth_from's depth was built since it was attached. That is told
    // by a serial each depth build takes, not by the texture name, which GL hands out again once a texture is deleted.
    // So building a depth owner again re-points its sharers without building their colour again.
    //
    // Kept means no GL call; Built that the colour was built, the depth attached, or both. A description that cannot
    // be built, one with depth of its own or a size other than depth_from's, and a depth_from with no depth of its
    // own, return Failed and leave this target released.
    EnsureResult ensure(const ALRenderTargetDesc& desc, const LLRenderTarget& depth_from);

    // Whether the depth attachment is another target's, attached by shareDepthBuffer or ensure.
    bool sharesDepth() const { return mUseDepth && !mDepth; }

    // Which build of its own depth this target holds, unique for the process: a target sharing it can tell the depth
    // it attached from one built since in its place. 0 for none.
    U64 getDepthSerial() const { return mDepthSerial; }

    // What sBytesAllocated counts for a target built from `desc`: each colour attachment's first level and the depth
    // of its own, not a depth it shares.
    static U64 bytesFor(const ALRenderTargetDesc& desc);

    // The name debuggers and the GL debug output show for this target: its framebuffer is labelled `label`, the
    // colour attachments it allocated `label.color0` on, and the depth it allocated `label.depth`. The label is kept
    // across builds and given to what each builds, so an owner names its target once; set on a built target, it
    // names what the target holds now. Empty leaves the target unnamed. Objects are labelled under debug GL (gDebugGL)
    // alone; the label is kept either way.
    void setLabel(std::string_view label);
    const std::string& getLabel() const { return mLabel; }

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
    // An integer colour attachment clears to the clear colour's components rounded to integers, each held to what a
    // 32-bit integer of the attachment's sign can hold: a negative one clears an unsigned attachment to 0.
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

    // Take the fragment shader's output `output` (frag_data[output]) into the first colour attachment and
    // discard every other output. Asserts that this target is currently bound and has a colour attachment;
    // the next bindTarget restores the one-to-one mapping.
    //
    // How a single-attachment target hosts a pass written for a G-buffer: the sky writes its colour to the
    // emissive output, frag_data[3], when that buffer is on, and mSkyBehind keeps only that colour.
    void setDrawOutput(U32 output);

    //get X resolution
    U32 getWidth() const { return mResX; }

    //get Y resolution
    U32 getHeight() const { return mResY; }

    ALTextureSlot::eTextureType getUsage(void) const { return mUsage; }

    U32 getTexture(U32 attachment = 0) const;
    U32 getNumTextures() const;
    // The sized internal format the attachment was allocated with, or the attached image's for the texture
    // setColorAttachment lent it; 0 for no such attachment.
    U32 getInternalFormat(U32 attachment = 0) const;

    U32 getDepth(void) const { return mDepth; }
    bool hasStencil() const { return mStencil; }
    eDepthFormat getDepthFormat() const { return mDepthFormat; }

    // The sampler a caller gets when it does not name one.
    //
    // These reproduce, as sampler objects, the filter and wrap state that allocation used to
    // write onto the texture objects themselves -- bilinear for attachment 0, point for the
    // data attachments behind it, mirrored repeat except on rectangle textures, and point
    // with plain repeat for depth -- and point for an integer attachment wherever it is, the
    // only filter GL reads integers through. That state was the last texture-object sampling state left
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
    // bilinear for 0, point for the data attachments and an integer one -- rather than a flat
    // constant that would quietly answer the question differently from the policy function.
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

    // `source`'s colour into `dest` by a draw rather than a blit, which a driver may run as a pixel transfer it has
    // to synchronize with the rendering around it: `program`, a copy reading DIFFUSE_MAP at the screen triangle's
    // coordinates (the viewer's interface/copyF.glsl), over `triangle`, the screen triangle, point sampled, so that each
    // texel of `dest` takes the one of `source` under its centre. Only over `rect` where there is one, the scissor
    // keeping the rest of `dest` as it was. No depth is written, nothing blended, and the targets bound before are bound
    // again after.
    static void drawCopy(LLRenderTarget& dest, LLRenderTarget& source, const LLRect* rect, LLGLSLShader& program,
                         LLVertexBuffer& triangle);

    // Copies the colour of the rect srcX0..srcX1, srcY0..srcY1 of the framebuffer bound now (the window's where no
    // target is) into this target's rect dstX0..dstX1, dstY0..dstY1, scaled with `filter`, GL_NEAREST or GL_LINEAR.
    // The scissor does not clip it, and what is bound stays bound.
    void copyFromBound(S32 srcX0, S32 srcY0, S32 srcX1, S32 srcY1, S32 dstX0, S32 dstY0, S32 dstX1, S32 dstY1, U32 filter);

    // Copies the `width` x `height` rect at (x, y) of the window's framebuffer, the one drawn into with no target
    // bound, into colour attachment 0 at its origin, one to one, whatever is bound now. What is bound stays bound, and
    // texture slot 0 is left holding the attachment.
    void copyFromWindow(S32 x, S32 y, S32 width, S32 height);

    //Returns TRUE if target is ready to be rendered into.
    //That is, if the target has been allocated with at least
    //one renderable attachment (i.e. color buffer, depth buffer).
    bool isComplete() const;

    // Returns true if this RenderTarget is bound somewhere in the stack
    bool isBoundInStack() const;

    static LLRenderTarget* getCurrentBoundTarget() { return sBoundTarget; }

    // *HACK
    // Trades framebuffers and colour textures with `other`, a target of the same description, and with them the shared
    // depth each framebuffer holds. Each keeps its label, given to what it holds now: where the two differ under debug
    // GL, every object of both is labelled again, a glObjectLabel call each.
    void swapFBORefs(LLRenderTarget& other);

    static LLRenderTarget* sBoundTarget;

protected:
    U32 mResX;
    U32 mResY;
    std::vector<U32> mTex;
    std::vector<U32> mInternalFormat;
    // The format of the texture setColorAttachment attached, which this target does not own and so keeps out of
    // mInternalFormat; 0 otherwise.
    U32 mExternalFormat = 0;
    U32 mFBO;
    LLRenderTarget* mPreviousRT = nullptr;

    U32 mDepth;
    // Which build of the depth attachment mDepth is, unique for the process; 0 for none.
    U64 mDepthSerial = 0;
    // The serial of the depth attached from another target; 0 for none.
    U64 mSharedDepthSerial = 0;
    bool mUseDepth;
    bool mStencil;
    eDepthFormat mDepthFormat;
    eMipGeneration mGenerateMipMaps = MIPS_NONE;
    U32 mMipLevels = 0;

    ALTextureSlot::eTextureType mUsage;

    // The colour attachment each draw buffer writes, -1 for none, as bindTarget, setDrawBuffers and setDrawOutput
    // last set them: the buffers clearColorBuffers clears, one at a time.
    std::array<S8, 4> mDrawBuffers = { -1, -1, -1, -1 };

    // Clear each colour draw buffer to the clear colour by the call its attachment's kind takes, float or integer.
    void clearColorBuffers();

    // Release what the target holds and allocate what `desc` describes, depth included. False leaves it released.
    bool build(const ALRenderTargetDesc& desc);

    // Attach `owner`'s depth, in place of any depth this target shared before.
    void attachSharedDepth(const LLRenderTarget& owner);

    // Give the framebuffer and the depth this target allocated their labels.
    void labelFramebuffer() const;
    // Give the colour attachment this target allocated at `attachment` its label.
    void labelColor(U32 attachment) const;

    std::string mLabel;

    // The serial the last depth build took.
    static U64 sLastDepthSerial;
};

#endif

