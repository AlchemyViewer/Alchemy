/**
 * @file llrendertarget.cpp
 * @brief LLRenderTarget implementation
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

#include "linden_common.h"

#include "llrendertarget.h"
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
    mUsage(LLTexUnit::TT_TEXTURE)
{
}

LLRenderTarget::~LLRenderTarget()
{
    release();
}

void LLRenderTarget::resize(U32 resx, U32 resy)
{
    //for accounting, get the number of pixels added/subtracted
    S64 pix_diff = static_cast<S64>(resx) * resy - static_cast<S64>(mResX) * mResY;

    mResX = resx;
    mResY = resy;

    llassert(mInternalFormat.size() == mTex.size());

    for (U32 i = 0; i < mTex.size(); ++i)
    { //resize color attachments
        gGL.getTexUnit(0)->bindManual(mUsage, mTex[i]);
        LLImageGL::setManualImage(LLTexUnit::getInternalType(mUsage), 0, mInternalFormat[i], mResX, mResY, GL_RGBA, GL_UNSIGNED_BYTE, NULL, false);
        sBytesAllocated += static_cast<S32>(pix_diff * get_color_format_bytes_per_pixel(mInternalFormat[i]));
    }

    if (mDepth)
    {
        gGL.getTexUnit(0)->bindManual(mUsage, mDepth);
        U32 internal_type = LLTexUnit::getInternalType(mUsage);
        const DepthFormatInfo info = get_depth_format_info(mDepthFormat, mStencil);
        LLImageGL::setManualImage(internal_type, 0, info.internal_format, mResX, mResY, info.pixel_format, info.pixel_type, NULL, false);

        sBytesAllocated += static_cast<S32>(pix_diff * info.bytes_per_pixel);
    }
}


bool LLRenderTarget::allocate(U32 resx, U32 resy, U32 color_fmt, bool depth, bool stencil, LLTexUnit::eTextureType usage, LLTexUnit::eTextureMipGeneration generateMipMaps, eDepthFormat depth_fmt)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(usage == LLTexUnit::TT_TEXTURE);
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

    if (mGenerateMipMaps != LLTexUnit::TMG_NONE) {
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
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, LLTexUnit::getInternalType(mUsage), mDepth, 0);

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
            LLTexUnit::getInternalType(mUsage), use_name, 0);
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
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, LLTexUnit::getInternalType(mUsage), 0, 0);
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
    gGL.getTexUnit(0)->bindManual(mUsage, tex);

    stop_glerror();


    {
        clear_glerror();
        LLImageGL::setManualImage(LLTexUnit::getInternalType(mUsage), 0, color_fmt, mResX, mResY, GL_RGBA, GL_UNSIGNED_BYTE, NULL, false);
        if (glGetError() != GL_NO_ERROR)
        {
            LL_WARNS() << "Could not allocate color buffer for render target." << LL_ENDL;
            LLImageGL::deleteTextures(1, &tex);
            return false;
        }
    }

    sBytesAllocated += mResX * mResY * get_color_format_bytes_per_pixel(color_fmt);

    stop_glerror();


    if (offset == 0)
    { //use bilinear filtering on single texture render targets that aren't multisampled
        gGL.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_BILINEAR);
        stop_glerror();
    }
    else
    { //don't filter data attachments
        gGL.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);
        stop_glerror();
    }

    if (mUsage != LLTexUnit::TT_RECT_TEXTURE)
    {
        gGL.getTexUnit(0)->setTextureAddressMode(LLTexUnit::TAM_MIRROR);
        stop_glerror();
    }
    else
    {
        // ATI doesn't support mirrored repeat for rectangular textures.
        gGL.getTexUnit(0)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
        stop_glerror();
    }

    if (mFBO)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+offset,
            LLTexUnit::getInternalType(mUsage), tex, 0);

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
    gGL.getTexUnit(0)->bindManual(mUsage, mDepth);

    U32 internal_type = LLTexUnit::getInternalType(mUsage);
    stop_glerror();
    clear_glerror();
    const DepthFormatInfo info = get_depth_format_info(mDepthFormat, mStencil);
    LLImageGL::setManualImage(internal_type, 0, info.internal_format, mResX, mResY, info.pixel_format, info.pixel_type, NULL, false);
    gGL.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);

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
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, LLTexUnit::getInternalType(mUsage), mDepth, 0);

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
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, LLTexUnit::getInternalType(mUsage), 0, 0);
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
            glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0+z), LLTexUnit::getInternalType(mUsage), 0, 0);
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

void LLRenderTarget::bindTexture(U32 index, S32 channel, LLTexUnit::eTextureFilterOptions filter_options)
{
    gGL.getTexUnit(channel)->bindManual(mUsage, getTexture(index), filter_options == LLTexUnit::TFO_TRILINEAR || filter_options == LLTexUnit::TFO_ANISOTROPIC);
    gGL.getTexUnit(channel)->setTextureFilteringOption(filter_options);
}

void LLRenderTarget::flush()
{
    LL_PROFILE_GPU_ZONE("rt flush");
    gGL.flush();
    llassert(mFBO);
    llassert(sCurFBO == mFBO);
    llassert(sBoundTarget == this);

    if (mGenerateMipMaps == LLTexUnit::TMG_AUTO)
    {
        LL_PROFILE_GPU_ZONE("rt generate mipmaps");
        bindTexture(0, 0, LLTexUnit::TFO_TRILINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
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
        gGL.getTexUnit(0)->bind(this, true);
        stop_glerror();
        glCopyTexSubImage2D(LLTexUnit::getInternalType(mUsage), 0, srcX0, srcY0, dstX0, dstY0, dstX1, dstY1);
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
