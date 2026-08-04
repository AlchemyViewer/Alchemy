/**
 * @file llrendertarget.cpp
 * @brief LLRenderTarget implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#include "llrendertarget.h"
#include "alsamplerstate.h"
#include "llrender.h"
#include "llgl.h"

#include <bit>

LLRenderTarget* LLRenderTarget::sBoundTarget = NULL;
U32 LLRenderTarget::sBytesAllocated = 0;

namespace
{
    // Resolve the GL (internal_format, pixel_format, pixel_type, bytes_per_pixel)
    // for a given depth format and stencil request.
    struct DepthFormatInfo
    {
        U32 internal_format;
        U32 pixel_format;
        U32 pixel_type;
        U32 bytes_per_pixel;
    };

    // Bytes per pixel for the GL color internal formats we actually allocate as
    // render targets. Used for sBytesAllocated accounting. Unknown formats fall
    // back to 4 to preserve the historical estimate.
    U32 get_color_format_bytes_per_pixel(U32 internal_format)
    {
        switch (internal_format)
        {
            case GL_R8:
                return 1;
            case GL_RG8:
            case GL_R16:
            case GL_R16F:
                return 2;
            case GL_RGB8:
            case GL_SRGB8:
                return 3;
            case GL_RGBA8:
            case GL_SRGB8_ALPHA8:
            case GL_RGB10_A2:
            case GL_R11F_G11F_B10F:
            case GL_RG16:
            case GL_RG16F:
            case GL_R32F:
            case GL_RGB9_E5:
                return 4;
            case GL_RGB16:
            case GL_RGB16F:
                return 6;
            case GL_RGBA16:
            case GL_RGBA16F:
            case GL_RG32F:
                return 8;
            case GL_RGB32F:
                return 12;
            case GL_RGBA32F:
                return 16;
            default:
                return 4;
        }
    }

    DepthFormatInfo get_depth_format_info(LLRenderTarget::eDepthFormat fmt, bool stencil)
    {
        if (stencil)
        {
            if (fmt == LLRenderTarget::DEPTH_FMT_32F)
            {
                return { GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 8 };
            }
            return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4 };
        }
        if (fmt == LLRenderTarget::DEPTH_FMT_32F)
        {
            return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4 };
        }
        return { GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 4 };
    }

    void check_framebuffer_status()
    {
        if (gDebugGL)
        {
            GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
            switch (status)
            {
                case GL_FRAMEBUFFER_COMPLETE:
                    break;
                default:
                    LL_WARNS() << "check_framebuffer_status failed -- " << std::hex << status << LL_ENDL;
                    ll_fail("check_framebuffer_status failed");
                    break;
            }
        }
    }
}

bool LLRenderTarget::sUseFBO = false;
U32 LLRenderTarget::sCurFBO = 0;


extern S32 gGLViewport[4];

U32 LLRenderTarget::sCurResX = 0;
U32 LLRenderTarget::sCurResY = 0;

LLRenderTarget::LLRenderTarget() :
    mResX(0),
    mResY(0),
    mFBO(0),
    mDepth(0),
    mUseDepth(false),
    mStencil(false),
    mDepthFormat(DEPTH_FMT_24),
    mUsage(ALTextureSlot::TT_TEXTURE)
{
}

LLRenderTarget::~LLRenderTarget()
{
    release();
}

void LLRenderTarget::resize(U32 resx, U32 resy)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(!isBoundInStack());

    if (resx == mResX && resy == mResY)
    {
        return;
    }

    // Rebuild rather than re-specifying storage underneath the live FBO.
    //
    // The old implementation called glTexImage2D on each attachment while it was still
    // attached to mFBO. That makes the driver free and reallocate storage which in-flight
    // command buffers may still reference, so it either blocks until they retire or forces
    // a pipeline sync, and it dirties the framebuffer's completeness state as well. Doing
    // that per attachment, for several targets in a frame, is enough to cost whole frames.
    //
    // Fresh textures have no in-flight references, and the old ones go out through
    // LLImageGL's deferred delete rather than being freed under the GPU. It is also the
    // only form immutable storage allows, since glTexStorage2D cannot re-specify.
    if (mUseDepth && mDepth == 0)
    {
        // Depth is shared from another target (see shareDepthBuffer). Rebuilding here
        // would silently unshare it, so leave this to the owner instead of guessing.
        LL_WARNS_ONCE() << "Refusing to resize a render target using a shared depth buffer; "
                        << "reallocate it from the target that owns the depth instead." << LL_ENDL;
        return;
    }

    // release() clears these, so capture the configuration before allocate() runs it.
    const std::vector<U32> formats = mInternalFormat;
    const bool                        use_depth  = (mDepth != 0);
    const bool                        stencil    = mStencil;
    const eDepthFormat                depth_fmt  = mDepthFormat;
    const ALTextureSlot::eTextureType     usage      = mUsage;
    const eMipGeneration mips  = mGenerateMipMaps;

    // allocate() releases first, then rebuilds attachment 0 and the depth buffer with the
    // same sampler state addColorAttachment/allocateDepth apply on first creation.
    allocate(resx, resy, formats.empty() ? 0 : formats[0], use_depth, stencil, usage, mips, depth_fmt);

    for (size_t i = 1; i < formats.size(); ++i)
    {
        addColorAttachment(formats[i]);
    }
}

bool LLRenderTarget::allocate(U32 resx, U32 resy, U32 color_fmt, bool depth, bool stencil, ALTextureSlot::eTextureType usage, eMipGeneration generateMipMaps, eDepthFormat depth_fmt)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(usage == ALTextureSlot::TT_TEXTURE);
    llassert(!isBoundInStack());
    llassert(!stencil || depth); // stencil requires depth

    resx = llmin(resx, (U32) gGLManager.mGLMaxTextureSize);
    resy = llmin(resy, (U32) gGLManager.mGLMaxTextureSize);

    release();

    mResX = resx;
    mResY = resy;

    mUsage = usage;
    mUseDepth = depth;
    mStencil = depth && stencil;
    mDepthFormat = depth_fmt;

    mGenerateMipMaps = generateMipMaps;
    mMipLevels = 0;

    if (mGenerateMipMaps != MIPS_NONE) {
        // floor(log2(n)) + 1, computed with integer math to avoid the
        // precision pitfall where log10(2^N)/log10(2) rounds below N.
        mMipLevels = static_cast<U32>(std::bit_width(static_cast<U32>(llmax(mResX, mResY))));
    }

    if (depth)
    {
        if (!allocateDepth())
        {
            LL_WARNS() << "Failed to allocate depth buffer for render target." << LL_ENDL;
            release();
            return false;
        }
    }

    glGenFramebuffers(1, (GLuint *) &mFBO);

    if (mDepth)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

        GLenum attachment = mStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, ALTextureSlot::getInternalType(mUsage), mDepth, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
    }

    if (!addColorAttachment(color_fmt))
    {
        release();
        return false;
    }
    return true;
}

void LLRenderTarget::setColorAttachment(LLImageGL* img, LLGLuint use_name)
{
    LL_PROFILE_ZONE_SCOPED;
    llassert(img != nullptr); // img must not be null
    llassert(sUseFBO); // FBO support must be enabled
    llassert(mDepth == 0); // depth buffers not supported with this mode
    llassert(mTex.empty()); // mTex must be empty with this mode (binding target should be done via LLImageGL)
    llassert(!isBoundInStack());

    if (mFBO == 0)
    {
        glGenFramebuffers(1, (GLuint*)&mFBO);
    }

    mResX = img->getWidth();
    mResY = img->getHeight();
    mUsage = img->getTarget();

    if (use_name == 0)
    {
        use_name = img->getTexName();
    }

    mTex.push_back(use_name);

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            ALTextureSlot::getInternalType(mUsage), use_name, 0);
        stop_glerror();

    check_framebuffer_status();

    glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
}

void LLRenderTarget::releaseColorAttachment()
{
    LL_PROFILE_ZONE_SCOPED;
    llassert(!isBoundInStack());
    llassert(mTex.size() == 1); //cannot use releaseColorAttachment with LLRenderTarget managed color targets
    llassert(mFBO != 0);  // mFBO must be valid

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ALTextureSlot::getInternalType(mUsage), 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);

    mTex.clear();
}

bool LLRenderTarget::addColorAttachment(U32 color_fmt)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(!isBoundInStack());

    if (color_fmt == 0)
    {
        return true;
    }

    U32 offset = static_cast<U32>(mTex.size());

    if( offset >= 4 )
    {
        LL_WARNS() << "Too many color attachments" << LL_ENDL;
        llassert( offset < 4 );
        return false;
    }
    if( offset > 0 && (mFBO == 0) )
    {
        llassert(  mFBO != 0 );
        return false;
    }

    U32 tex;
    LLImageGL::generateTextures(1, &tex);
    gGL.getTextureSlot(0)->bindManual(mUsage, tex);

    stop_glerror();


    {
        clear_glerror();
        // The whole mip pyramid must exist at allocation: storage is immutable, so the
        // glGenerateMipmap in flush() can only fill levels that were allocated here --
        // against a single-level texture it is a spec-defined no-op and MIPS_AUTO targets
        // (the luminance map auto-exposure averages through) silently lose their mips.
        const S32 levels = llmax(1, (S32)mMipLevels);
        LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(mUsage), color_fmt, mResX, mResY, GL_RGBA, GL_UNSIGNED_BYTE, NULL, levels);
        if (glGetError() != GL_NO_ERROR)
        {
            LL_WARNS() << "Could not allocate color buffer for render target." << LL_ENDL;
            LLImageGL::deleteTextures(1, &tex);
            return false;
        }
    }

    sBytesAllocated += mResX * mResY * get_color_format_bytes_per_pixel(color_fmt);

    stop_glerror();


    // No filter or wrap state is written onto the texture object here. It is expressed as a
    // sampler instead -- see getDefaultColorSampler, which encodes the same choices (bilinear
    // for attachment 0, point for the data attachments, mirrored repeat off rectangles) and
    // is what ALTextureSlot::bind hands out to callers that do not name a sampler.
    //
    // This was the last texture-object sampling state in the engine. Leaving it here as well
    // would not be harmless redundancy: it makes filtering a property of the resource, so two
    // passes wanting different filtering off one target have to fight over it, which is the
    // whole reason the renderer moved to sampler objects.

    if (mFBO)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+offset,
            ALTextureSlot::getInternalType(mUsage), tex, 0);

        check_framebuffer_status();

        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
    }

    mTex.push_back(tex);
    mInternalFormat.push_back(color_fmt);

    if (gDebugGL)
    { //bind and unbind to validate target
        bindTarget();
        flush();
    }


    return true;
}

bool LLRenderTarget::allocateDepth()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    LLImageGL::generateTextures(1, &mDepth);
    gGL.getTextureSlot(0)->bindManual(mUsage, mDepth);

    U32 internal_type = ALTextureSlot::getInternalType(mUsage);
    stop_glerror();
    clear_glerror();
    const DepthFormatInfo info = get_depth_format_info(mDepthFormat, mStencil);
    LLImageGL::allocateTexture2D(internal_type, info.internal_format, mResX, mResY, info.pixel_format, info.pixel_type, NULL);
    // Point filtering comes from getDefaultDepthSampler now, not from the texture object.

    sBytesAllocated += mResX * mResY * info.bytes_per_pixel;

    if (glGetError() != GL_NO_ERROR)
    {
        LL_WARNS() << "Unable to allocate depth buffer for render target." << LL_ENDL;
        return false;
    }

    return true;
}

void LLRenderTarget::shareDepthBuffer(LLRenderTarget& target)
{
    llassert(!isBoundInStack());

    if (!mFBO || !target.mFBO)
    {
        LL_ERRS() << "Cannot share depth buffer between non FBO render targets." << LL_ENDL;
    }

    if (target.mDepth)
    {
        LL_ERRS() << "Attempting to override existing depth buffer.  Detach existing buffer first." << LL_ENDL;
    }

    if (target.mUseDepth)
    {
        LL_ERRS() << "Attempting to override existing shared depth buffer. Detach existing buffer first." << LL_ENDL;
    }

    if (mDepth)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, target.mFBO);

        GLenum attachment = mStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, ALTextureSlot::getInternalType(mUsage), mDepth, 0);

        check_framebuffer_status();

        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);

        target.mUseDepth = true;
        target.mStencil = mStencil;
        target.mDepthFormat = mDepthFormat;
    }
}

void LLRenderTarget::release()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(!isBoundInStack());

    if (mDepth)
    {
        LLImageGL::deleteTextures(1, &mDepth);

        const DepthFormatInfo info = get_depth_format_info(mDepthFormat, mStencil);
        sBytesAllocated -= mResX * mResY * info.bytes_per_pixel;

        mDepth = 0;
        mStencil = false;
        mDepthFormat = DEPTH_FMT_24;
    }
    else if (mFBO)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

        if (mUseDepth)
        { //detach shared depth buffer
            GLenum attachment = mStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, ALTextureSlot::getInternalType(mUsage), 0, 0);
            mUseDepth = false;
            mStencil = false;
            mDepthFormat = DEPTH_FMT_24;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
    }

    // Detach any extra color buffers (e.g. SRGB spec buffers)
    //
    if (mFBO && (mTex.size() > 1))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
        size_t z;
        for (z = mTex.size() - 1; z >= 1; z--)
        {
            if (z < mInternalFormat.size())
            {
                sBytesAllocated -= mResX * mResY * get_color_format_bytes_per_pixel(mInternalFormat[z]);
            }
            glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0+z), ALTextureSlot::getInternalType(mUsage), 0, 0);
            LLImageGL::deleteTextures(1, &mTex[z]);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
    }

    if (mFBO)
    {
        if (mFBO == sCurFBO)
        {
            sCurFBO = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        glDeleteFramebuffers(1, (GLuint *) &mFBO);
        mFBO = 0;
    }

    // mTex[0] is owned only when addColorAttachment populated mInternalFormat.
    // setColorAttachment leaves mInternalFormat empty for an externally-owned
    // texture and relies on the caller to manage that lifetime.
    if (!mTex.empty() && !mInternalFormat.empty())
    {
        sBytesAllocated -= mResX * mResY * get_color_format_bytes_per_pixel(mInternalFormat[0]);
        LLImageGL::deleteTextures(1, &mTex[0]);
    }

    mTex.clear();
    mInternalFormat.clear();

    mResX = mResY = 0;
}

void LLRenderTarget::bindTarget()
{
    LL_PROFILE_GPU_ZONE("bindTarget");
    llassert(mFBO);
    llassert(!isBoundInStack());

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    sCurFBO = mFBO;

    //setup multiple render targets
    GLenum drawbuffers[] = {GL_COLOR_ATTACHMENT0,
                            GL_COLOR_ATTACHMENT1,
                            GL_COLOR_ATTACHMENT2,
                            GL_COLOR_ATTACHMENT3};

    if (mTex.empty())
    { //no color buffer to draw to
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    else
    {
        glDrawBuffers(static_cast<GLsizei>(mTex.size()), drawbuffers);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    check_framebuffer_status();

    glViewport(0, 0, mResX, mResY);
    sCurResX = mResX;
    sCurResY = mResY;

    mPreviousRT = sBoundTarget;
    sBoundTarget = this;
}

void LLRenderTarget::clear(U32 mask_in)
{
    LL_PROFILE_GPU_ZONE("clear");
    llassert(mFBO);
    U32 mask = GL_COLOR_BUFFER_BIT;
    if (mUseDepth)
    {
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (mStencil)
    {
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (mFBO)
    {
        check_framebuffer_status();
        stop_glerror();
        glClear(mask & mask_in);
        stop_glerror();
    }
    else
    {
        LLGLEnable scissor(GL_SCISSOR_TEST);
        glScissor(0, 0, mResX, mResY);
        stop_glerror();
        glClear(mask & mask_in);
    }
}

U32 LLRenderTarget::getTexture(U32 attachment) const
{
    if (attachment >= mTex.size())
    {
        LL_WARNS() << "Invalid attachment index " << attachment << " for size " << mTex.size() << LL_ENDL;
        llassert(false);
        return 0;
    }
    return mTex[attachment];
}

U32 LLRenderTarget::getNumTextures() const
{
    return static_cast<U32>(mTex.size());
}

void LLRenderTarget::bindTexture(U32 index, S32 channel, ALSampler key)
{
    // The caller's key is taken as stated. Two adjustments used to happen here and neither
    // is needed now: a mipmapping filter was downgraded when the target had no mip chain
    // (immutable storage makes that combination legal), and TAM_MIRROR was rewritten to
    // TAM_CLAMP for rectangle textures, which cannot do mirrored repeat -- no target is
    // allocated as TT_RECT_TEXTURE anywhere in the viewer, so that branch never ran.
    //
    // An unnamed key defers to the per-attachment policy: bilinear only for the attachment
    // a pass reads as an image, point for the data attachments behind it, whose values mean
    // nothing interpolated.
    const U32 sampler = (key == ALSamplers::TargetDefault) ? getDefaultColorSampler(index)
                                                           : gGL.getSampler(key);
    gGL.getTextureSlot(channel)->bindManual(mUsage, getTexture(index), sampler);
}

U32 LLRenderTarget::getDefaultColorSampler(U32 attachment) const
{
    // Attachment 0 is the one a pass normally reads as an image, so it filters. Everything
    // behind it carries data -- normals, ORM, indices -- where interpolating between texels
    // produces values that mean nothing.
    //
    // Mirror either way. A caller that wants something else asks for it through bindTexture.
    return gGL.getSampler((attachment == 0) ? ALSamplers::BilinearMirror
                                            : ALSamplers::PointMirror);
}

U32 LLRenderTarget::getDefaultDepthSampler() const
{
    // Point + clamp, which is what a depth attachment should always have been sampled with.
    //
    // These textures carried GL_REPEAT for as long as they have existed, not by choice but
    // because allocateDepth only ever set a filter and left the wrap mode at GL's default. A
    // repeat wrap on a depth buffer is meaningless: a fetch that strays outside [0,1] -- which
    // any reprojection, blur tap or ray march can do at the screen edge -- comes back with the
    // depth of the OPPOSITE edge of the screen rather than the nearest one, so the sample is
    // not merely clamped-wrong, it is geometry from somewhere else entirely.
    //
    // The rest of the renderer already agreed: shadow maps sample clamped, and the SMAA
    // predication pass binds this very texture point-clamped. The default was the odd one
    // out.
    return gGL.getSampler(ALSamplers::PointClamp);
}

void LLRenderTarget::flush()
{
    LL_PROFILE_GPU_ZONE("rt flush");
    gGL.flush();
    llassert(mFBO);
    llassert(sCurFBO == mFBO);
    llassert(sBoundTarget == this);

    if (mGenerateMipMaps == MIPS_AUTO)
    {
        LL_PROFILE_GPU_ZONE("rt generate mipmaps");
        bindTexture(0, 0, ALSamplers::TrilinearMirror);
        LLImageGL::generateMipmaps(GL_TEXTURE_2D);
    }

    if (mPreviousRT)
    {
        // a bit hacky -- pop the RT stack back two frames and push
        // the previous frame back on to play nice with the GL state machine
        sBoundTarget = mPreviousRT->mPreviousRT;
        mPreviousRT->bindTarget();
    }
    else
    {
        sBoundTarget = nullptr;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        sCurFBO = 0;
        glViewport(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]);
        sCurResX = gGLViewport[2];
        sCurResY = gGLViewport[3];
        glReadBuffer(GL_BACK);
        glDrawBuffer(GL_BACK);
    }
}

void LLRenderTarget::copyContents(LLRenderTarget& source, S32 srcX0, S32 srcY0, S32 srcX1, S32 srcY1, S32 dstX0, S32 dstY0, S32 dstX1,
                                  S32 dstY1, U32 mask, U32 filter)
{
    LL_PROFILE_GPU_ZONE("LLRenderTarget::copyContents");

    GLboolean write_depth = mask & GL_DEPTH_BUFFER_BIT ? GL_TRUE : GL_FALSE;

    LLGLDepthTest depth(write_depth, write_depth);

    gGL.flush();
    if (!source.mFBO || !mFBO)
    {
        LL_WARNS() << "Cannot copy framebuffer contents for non FBO render targets." << LL_ENDL;
        return;
    }

    if (mask == GL_DEPTH_BUFFER_BIT && source.mStencil != mStencil)
    {
        stop_glerror();

        glBindFramebuffer(GL_FRAMEBUFFER, source.mFBO);
        check_framebuffer_status();
        gGL.getTextureSlot(0)->bind(this, true);
        stop_glerror();
        // glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height):
        // xoffset/yoffset are the destination texel offset, x/y the source framebuffer
        // origin, and the last two are dimensions (not endpoints).
        glCopyTexSubImage2D(ALTextureSlot::getInternalType(mUsage), 0, dstX0, dstY0, srcX0, srcY0, srcX1 - srcX0, srcY1 - srcY0);
        stop_glerror();
        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
        stop_glerror();
    }
    else
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source.mFBO);
        stop_glerror();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFBO);
        stop_glerror();
        check_framebuffer_status();
        stop_glerror();
        glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
        stop_glerror();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        stop_glerror();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        stop_glerror();
        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
        stop_glerror();
    }
}

// static
// WARNING: passing GL_DEPTH_BUFFER_BIT here blits into the default framebuffer's 24-bit
// depth. Under reverse-Z the scene depth targets are DEPTH_COMPONENT32F, and glBlitFramebuffer
// requires matching depth formats -- a 32F->24 depth blit is invalid. There is currently no
// live depth caller (the only one is commented out), so this is a latent hazard, not a bug.
void LLRenderTarget::copyContentsToFramebuffer(LLRenderTarget& source, S32 srcX0, S32 srcY0, S32 srcX1, S32 srcY1, S32 dstX0, S32 dstY0,
                                               S32 dstX1, S32 dstY1, U32 mask, U32 filter)
{
    if (!source.mFBO)
    {
        LL_WARNS() << "Cannot copy framebuffer contents for non FBO render targets." << LL_ENDL;
        return;
    }

    {
        LL_PROFILE_GPU_ZONE("copyContentsToFramebuffer");
        GLboolean write_depth = mask & GL_DEPTH_BUFFER_BIT ? GL_TRUE : GL_FALSE;

        LLGLDepthTest depth(write_depth, write_depth);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, source.mFBO);
        stop_glerror();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        stop_glerror();
        check_framebuffer_status();
        stop_glerror();
        glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
        stop_glerror();
        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
        stop_glerror();
    }
}

bool LLRenderTarget::isComplete() const
{
    return !mTex.empty() || mDepth;
}

void LLRenderTarget::getViewport(S32* viewport)
{
    viewport[0] = 0;
    viewport[1] = 0;
    viewport[2] = mResX;
    viewport[3] = mResY;
}

void LLRenderTarget::setDrawBuffers(U32 count)
{
    llassert(sBoundTarget == this);

    if (mTex.empty())
    {   // no colour buffer to narrow -- bindTarget already selected GL_NONE
        return;
    }

    // Same table bindTarget uses; kept in step by the assert below rather than by hope.
    static const GLenum drawbuffers[] = {GL_COLOR_ATTACHMENT0,
                                         GL_COLOR_ATTACHMENT1,
                                         GL_COLOR_ATTACHMENT2,
                                         GL_COLOR_ATTACHMENT3};

    const U32 attachments = (U32)mTex.size();
    llassert(attachments <= LL_ARRAY_SIZE(drawbuffers));

    const U32 wanted = (count == 0) ? attachments : llmin(count, attachments);

    glDrawBuffers(static_cast<GLsizei>(wanted), drawbuffers);
}

bool LLRenderTarget::isBoundInStack() const
{
    LLRenderTarget* cur = sBoundTarget;
    while (cur && cur != this)
    {
        cur = cur->mPreviousRT;
    }

    return cur == this;
}

void LLRenderTarget::swapFBORefs(LLRenderTarget& other)
{
    // Must be initialized
    llassert(mFBO);
    llassert(other.mFBO);

    // Must be unbound
    // *NOTE: mPreviousRT can be non-null even if this target is unbound - presumably for debugging purposes?
    llassert(sCurFBO != mFBO);
    llassert(sCurFBO != other.mFBO);
    llassert(!isBoundInStack());
    llassert(!other.isBoundInStack());

    // Must be same type
    llassert(sUseFBO == other.sUseFBO);
    llassert(mResX == other.mResX);
    llassert(mResY == other.mResY);
    llassert(mInternalFormat == other.mInternalFormat);
    llassert(mTex.size() == other.mTex.size());
    llassert(mDepth == other.mDepth);
    llassert(mUseDepth == other.mUseDepth);
    llassert(mStencil == other.mStencil);
    llassert(mDepthFormat == other.mDepthFormat);
    llassert(mGenerateMipMaps == other.mGenerateMipMaps);
    llassert(mMipLevels == other.mMipLevels);
    llassert(mUsage == other.mUsage);

    std::swap(mFBO, other.mFBO);
    std::swap(mTex, other.mTex);
}
