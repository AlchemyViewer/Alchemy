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
#include "llimagegl.h"
#include "llrender.h"
#include "llgl.h"
#include "llglslshader.h"
#include "llglstates.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include <fmt/format.h>

LLRenderTarget* LLRenderTarget::sBoundTarget = NULL;
U32 LLRenderTarget::sBytesAllocated = 0;
U64 LLRenderTarget::sLastDepthSerial = 0;

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
            case GL_R8I:
            case GL_R8UI:
                return 1;
            case GL_RG8:
            case GL_R16:
            case GL_R16F:
            case GL_RG8I:
            case GL_RG8UI:
            case GL_R16I:
            case GL_R16UI:
                return 2;
            case GL_RGB8:
            case GL_SRGB8:
            case GL_RGB8I:
            case GL_RGB8UI:
                return 3;
            case GL_RGBA8:
            case GL_SRGB8_ALPHA8:
            case GL_RGB10_A2:
            case GL_R11F_G11F_B10F:
            case GL_RG16:
            case GL_RG16F:
            case GL_R32F:
            case GL_RGB9_E5:
            case GL_RGBA8I:
            case GL_RGBA8UI:
            case GL_RGB10_A2UI:
            case GL_RG16I:
            case GL_RG16UI:
            case GL_R32I:
            case GL_R32UI:
                return 4;
            case GL_RGB16:
            case GL_RGB16F:
            case GL_RGB16I:
            case GL_RGB16UI:
                return 6;
            case GL_RGBA16:
            case GL_RGBA16F:
            case GL_RG32F:
            case GL_RGBA16I:
            case GL_RGBA16UI:
            case GL_RG32I:
            case GL_RG32UI:
                return 8;
            case GL_RGB32F:
            case GL_RGB32I:
            case GL_RGB32UI:
                return 12;
            case GL_RGBA32F:
            case GL_RGBA32I:
            case GL_RGBA32UI:
                return 16;
            default:
                return 4;
        }
    }

    // The clear colour as an integer attachment takes it: each component rounded to the nearest integer and held to
    // the 32-bit range of the attachment's sign, so a negative one clears an unsigned attachment to 0 and a huge one to
    // its top. A narrower format takes the result as GL converts it.
    void integer_clear_value(const LLColor4& colour, GLint (&signed_value)[4], GLuint (&unsigned_value)[4])
    {
        for (U32 c = 0; c < 4; ++c)
        {
            const F64 rounded = std::isfinite(colour.mV[c]) ? std::round((F64)colour.mV[c]) : 0.0;
            signed_value[c]   = (GLint)llclamp(rounded, (F64)std::numeric_limits<GLint>::min(),
                                               (F64)std::numeric_limits<GLint>::max());
            unsigned_value[c] = (GLuint)llclamp(rounded, 0.0, (F64)std::numeric_limits<GLuint>::max());
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

    LLRenderTarget::eMipGeneration mip_generation(ALRenderTargetDesc::Mips mips)
    {
        switch (mips)
        {
            case ALRenderTargetDesc::Mips::Auto: return LLRenderTarget::MIPS_AUTO;
            case ALRenderTargetDesc::Mips::Manual: return LLRenderTarget::MIPS_MANUAL;
            case ALRenderTargetDesc::Mips::None: break;
        }
        return LLRenderTarget::MIPS_NONE;
    }

    ALRenderTargetDesc::Mips desc_mips(LLRenderTarget::eMipGeneration mips)
    {
        switch (mips)
        {
            case LLRenderTarget::MIPS_AUTO: return ALRenderTargetDesc::Mips::Auto;
            case LLRenderTarget::MIPS_MANUAL: return ALRenderTargetDesc::Mips::Manual;
            case LLRenderTarget::MIPS_NONE: break;
        }
        return ALRenderTargetDesc::Mips::None;
    }

    // `desc` as allocate builds it, its size held to the context's largest texture.
    ALRenderTargetDesc held_to_context(ALRenderTargetDesc desc)
    {
        const U32 max_size = (U32)gGLManager.mGLMaxTextureSize;
        desc.width         = llmin(desc.width, max_size);
        desc.height        = llmin(desc.height, max_size);
        return desc;
    }

    // Why no target can be built from `desc`, or null when one can. A target whose depth is shared needs no attachment
    // of its own.
    const char* unbuildable(const ALRenderTargetDesc& desc, bool depth_shared)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            return "it has no size";
        }
        const U32 colors = desc.colorCount();
        for (U32 attachment = colors; attachment < ALRenderTargetDesc::MAX_COLOR; ++attachment)
        {
            if (desc.color[attachment] != 0)
            {
                return "a colour format follows a 0 in its colour list";
            }
        }
        if (colors == 0 && desc.depth == ALRenderTargetDesc::Depth::None && !depth_shared)
        {
            return "it has no attachment";
        }
        if (desc.stencil && desc.depth == ALRenderTargetDesc::Depth::None)
        {
            return "it has stencil without depth";
        }
        if (desc.mips == ALRenderTargetDesc::Mips::Auto && colors == 0)
        {
            return "flush generates its first attachment's mips, and it has no colour attachment";
        }
        if (desc.mips == ALRenderTargetDesc::Mips::Auto && LLImageGL::isIntegerFormat(desc.color[0]))
        {
            return "flush generates its first attachment's mips, and GL generates none of an integer texture's";
        }
        return nullptr;
    }

    // Why `sharer` cannot be what `desc` describes with `owner`'s depth, or null when it can.
    const char* unshareable(const ALRenderTargetDesc& desc, const LLRenderTarget& sharer, const LLRenderTarget& owner)
    {
        if (&sharer == &owner)
        {
            return "it would share its own depth";
        }
        if (!owner.getDepth())
        {
            return "the target it shares depth from has none of its own";
        }
        if (desc.depth != ALRenderTargetDesc::Depth::None || desc.stencil)
        {
            return "it describes depth of its own";
        }
        if (desc.width != owner.getWidth() || desc.height != owner.getHeight())
        {
            return "its size is not the size of the depth it shares";
        }
        return nullptr;
    }

    // Whether GL objects are labelled: under debug GL alone, where the context keeps labels. A label is a driver call
    // that copies the string, and only a debugger reads it.
    bool labels_objects()
    {
        return gDebugGL && gGLManager.mHasDebugOutput && glObjectLabel;
    }

    // Label the GL object `name` `label` followed by `suffix` and, where it is not negative, `index`; an empty `label`
    // takes the object's label away. Nothing where objects are not labelled (labels_objects).
    void label_object(GLenum identifier, U32 name, std::string_view label, std::string_view suffix, S32 index = -1)
    {
        if (!name || !labels_objects())
        {
            return;
        }
        if (label.empty())
        {
            glObjectLabel(identifier, name, 0, nullptr);
            return;
        }
        // GL refuses a label as long as GL_MAX_LABEL_LENGTH, which is at least 256.
        char text[255];
        const auto result = index < 0 ? fmt::format_to_n(text, sizeof(text), "{}{}", label, suffix)
                                      : fmt::format_to_n(text, sizeof(text), "{}{}{}", label, suffix, index);
        glObjectLabel(identifier, name, (GLsizei)(result.out - text), text);
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

    // flush() regenerates attachment 0's mips, and GL builds none of an integer texture's: glGenerateMipmap refuses
    // it. An integer target with mips fills them itself, as MIPS_MANUAL.
    if (generateMipMaps == MIPS_AUTO && LLImageGL::isIntegerFormat(color_fmt))
    {
        LL_WARNS() << "Integer render target format 0x" << std::hex << color_fmt << std::dec
                   << " cannot have its mips generated; allocate it MIPS_MANUAL." << LL_ENDL;
        llassert(false);
        release();
        return false;
    }

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

    // Bound once whatever it holds: a name is a framebuffer only from its first bind, and only a framebuffer takes a
    // label.
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    if (mDepth)
    {
        GLenum attachment = mStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, ALTextureSlot::getInternalType(mUsage), mDepth, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);

    if (!addColorAttachment(color_fmt))
    {
        release();
        return false;
    }

    if (!mLabel.empty())
    {
        labelFramebuffer();
    }
    return true;
}

// static
U64 LLRenderTarget::bytesFor(const ALRenderTargetDesc& desc)
{
    U64 per_pixel = 0;
    for (U32 attachment = 0; attachment < desc.colorCount(); ++attachment)
    {
        per_pixel += get_color_format_bytes_per_pixel(desc.color[attachment]);
    }
    if (desc.depth != ALRenderTargetDesc::Depth::None)
    {
        const eDepthFormat format = desc.depth == ALRenderTargetDesc::Depth::D32F ? DEPTH_FMT_32F : DEPTH_FMT_24;
        per_pixel += get_depth_format_info(format, desc.stencil).bytes_per_pixel;
    }
    return (U64)desc.width * desc.height * per_pixel;
}

ALRenderTargetDesc LLRenderTarget::desc() const
{
    if (!mFBO)
    {
        return {};
    }

    ALRenderTargetDesc desc{ .width = mResX, .height = mResY, .mips = desc_mips(mGenerateMipMaps) };
    for (size_t attachment = 0; attachment < mInternalFormat.size() && attachment < ALRenderTargetDesc::MAX_COLOR;
         ++attachment)
    {
        desc.color[attachment] = mInternalFormat[attachment];
    }
    if (mDepth)
    {
        desc.depth   = mDepthFormat == DEPTH_FMT_32F ? ALRenderTargetDesc::Depth::D32F : ALRenderTargetDesc::Depth::D24;
        desc.stencil = mStencil;
    }
    return desc;
}

LLRenderTarget::EnsureResult LLRenderTarget::ensure(const ALRenderTargetDesc& wanted)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(!isBoundInStack());

    const ALRenderTargetDesc held = held_to_context(wanted);
    if (const char* reason = unbuildable(held, /*depth_shared=*/false))
    {
        LL_WARNS("RenderTarget") << "Render target '" << mLabel << "' cannot be built as described: " << reason
                                 << LL_ENDL;
        release();
        return EnsureResult::Failed;
    }

    // A depth attachment shared from another target, and a lent colour texture, are storage the description says
    // nothing of, so a target holding either is built again.
    const bool holds_only_its_own = !sharesDepth() && mTex.size() == mInternalFormat.size();
    if (holds_only_its_own && held == desc())
    {
        return EnsureResult::Kept;
    }

    return build(held) ? EnsureResult::Built : EnsureResult::Failed;
}

LLRenderTarget::EnsureResult LLRenderTarget::ensure(const ALRenderTargetDesc& wanted, const LLRenderTarget& depth_from)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;
    llassert(!isBoundInStack());

    const ALRenderTargetDesc held = held_to_context(wanted);
    const char*              reason = unbuildable(held, /*depth_shared=*/true);
    if (!reason)
    {
        reason = unshareable(held, *this, depth_from);
    }
    if (reason)
    {
        LL_WARNS("RenderTarget") << "Render target '" << mLabel << "' cannot be built as described: " << reason
                                 << LL_ENDL;
        release();
        return EnsureResult::Failed;
    }

    bool built = false;
    if (mTex.size() != mInternalFormat.size() || held != desc())
    {
        if (!build(held))
        {
            return EnsureResult::Failed;
        }
        built = true;
    }

    if (!built && mSharedDepthSerial == depth_from.mDepthSerial)
    {
        return EnsureResult::Kept;
    }

    attachSharedDepth(depth_from);
    return EnsureResult::Built;
}

bool LLRenderTarget::build(const ALRenderTargetDesc& desc)
{
    const bool         depth     = desc.depth != ALRenderTargetDesc::Depth::None;
    const eDepthFormat depth_fmt = desc.depth == ALRenderTargetDesc::Depth::D32F ? DEPTH_FMT_32F : DEPTH_FMT_24;
    if (!allocate(desc.width, desc.height, desc.color[0], depth, desc.stencil, ALTextureSlot::TT_TEXTURE,
                  mip_generation(desc.mips), depth_fmt))
    {
        return false;
    }

    for (U32 attachment = 1; attachment < desc.colorCount(); ++attachment)
    {
        if (!addColorAttachment(desc.color[attachment]))
        {
            release();
            return false;
        }
    }
    return true;
}

void LLRenderTarget::setColorAttachment(LLImageGL* img, LLGLuint use_name)
{
    LL_PROFILE_ZONE_SCOPED;
    llassert(img != nullptr); // img must not be null
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
    mExternalFormat = (U32)img->getStorageInternalFormat();

    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            ALTextureSlot::getInternalType(mUsage), use_name, 0);
        stop_glerror();

    check_framebuffer_status();

    glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);

    // The lent texture keeps the name its owner gave it.
    if (!mLabel.empty())
    {
        labelFramebuffer();
    }
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
    mExternalFormat = 0;
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
        // No pixels go with the storage, but the format they would come in agrees with it.
        const U32 integer_format = LLImageGL::integerPixelFormat(color_fmt);
        LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(mUsage), color_fmt, mResX, mResY,
                                     integer_format ? integer_format : GL_RGBA, GL_UNSIGNED_BYTE, NULL, levels);
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
    // for attachment 0, point for the data attachments and any integer one, mirrored repeat
    // off rectangles) and is what ALTextureSlot::bind hands out to callers that do not name a
    // sampler.
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

    if (!mLabel.empty())
    {
        labelColor(offset);
    }

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
    mDepthSerial = ++sLastDepthSerial;
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
        target.attachSharedDepth(*this);
    }
}

void LLRenderTarget::setLabel(std::string_view label)
{
    mLabel = label;
    labelFramebuffer();
    for (U32 attachment = 0; attachment < mInternalFormat.size(); ++attachment)
    {
        labelColor(attachment);
    }
}

void LLRenderTarget::labelFramebuffer() const
{
    label_object(GL_FRAMEBUFFER, mFBO, mLabel, "");
    label_object(GL_TEXTURE, mDepth, mLabel, ".depth");
}

void LLRenderTarget::labelColor(U32 attachment) const
{
    if (attachment < mInternalFormat.size())
    {
        label_object(GL_TEXTURE, mTex[attachment], mLabel, ".color", (S32)attachment);
    }
}

void LLRenderTarget::attachSharedDepth(const LLRenderTarget& owner)
{
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

    if (mUseDepth)
    {
        // From the point it was attached at: a depth-stencil texture fills the depth and the stencil point, a depth
        // texture the depth point alone, so attaching at the other point would leave it on one of them.
        GLenum attachment = mStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, ALTextureSlot::getInternalType(mUsage), 0, 0);
    }

    GLenum attachment = owner.mStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, ALTextureSlot::getInternalType(owner.mUsage), owner.mDepth, 0);

    check_framebuffer_status();

    glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);

    mUseDepth          = true;
    mStencil           = owner.mStencil;
    mDepthFormat       = owner.mDepthFormat;
    mSharedDepthSerial = owner.mDepthSerial;
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
        mDepthSerial = 0;
    }
    else if (mFBO)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

        if (mUseDepth)
        { //detach shared depth buffer
            GLenum attachment = mStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, ALTextureSlot::getInternalType(mUsage), 0, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
    }
    mUseDepth = false;
    mStencil = false;
    mDepthFormat = DEPTH_FMT_24;
    mSharedDepthSerial = 0;

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
    mExternalFormat = 0;

    mResX = mResY = 0;
}

void LLRenderTarget::bindTarget()
{
    LL_PROFILE_GPU_ZONE("bindTarget");
    // What gGL has batched belongs to the framebuffer bound before this one.
    gGL.flush();
    llassert(mFBO);
    llassert(!isBoundInStack());

    // The pass drawing into the framebuffer bound before ends here, and a shader profile's queries with it; they count
    // on for the pass that follows.
    LLGLSLShader* profiled = LLGLSLShader::suspendProfileQuery();
    gGL.queries().checkPassBoundary();
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
    for (U32 draw_buffer = 0; draw_buffer < mDrawBuffers.size(); ++draw_buffer)
    {
        mDrawBuffers[draw_buffer] = draw_buffer < mTex.size() ? (S8)draw_buffer : -1;
    }
    check_framebuffer_status();

    glViewport(0, 0, mResX, mResY);
    sCurResX = mResX;
    sCurResY = mResY;

    mPreviousRT = sBoundTarget;
    sBoundTarget = this;

    LLGLSLShader::resumeProfileQuery(profiled);
}

void LLRenderTarget::clear(U32 mask_in)
{
    LL_PROFILE_GPU_ZONE("clear");
    // What gGL has batched is drawn before the clear, not after it.
    gGL.flush();
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
    mask &= mask_in;
    if (mFBO)
    {
        check_framebuffer_status();
        stop_glerror();
        // glClear of an integer colour buffer is undefined, so a target holding one clears its colour a draw buffer at
        // a time, each by the call its kind takes. Depth and stencil still go through glClear.
        bool integer_colour = false;
        for (U32 attachment = 0; attachment < mTex.size(); ++attachment)
        {
            integer_colour |= LLImageGL::isIntegerFormat(getInternalFormat(attachment));
        }
        if ((mask & GL_COLOR_BUFFER_BIT) && integer_colour)
        {
            clearColorBuffers();
            mask &= ~GL_COLOR_BUFFER_BIT;
        }
        if (mask != 0)
        {
            glClear(mask);
        }
        stop_glerror();
    }
    else
    {
        LLGLSScissor scissor(0, 0, mResX, mResY);
        stop_glerror();
        glClear(mask);
    }
}

void LLRenderTarget::clearColorBuffers()
{
    const LLColor4& colour = gGL.getClearColor();
    GLint  signed_value[4];
    GLuint unsigned_value[4];
    integer_clear_value(colour, signed_value, unsigned_value);

    for (U32 draw_buffer = 0; draw_buffer < mDrawBuffers.size(); ++draw_buffer)
    {
        const S32 attachment = mDrawBuffers[draw_buffer];
        if (attachment < 0 || attachment >= (S32)mTex.size())
        {
            continue;
        }

        const U32 format = getInternalFormat((U32)attachment);
        if (!LLImageGL::isIntegerFormat(format))
        {
            glClearBufferfv(GL_COLOR, (GLint)draw_buffer, colour.mV);
        }
        else if (LLImageGL::isSignedIntegerFormat(format))
        {
            glClearBufferiv(GL_COLOR, (GLint)draw_buffer, signed_value);
        }
        else
        {
            glClearBufferuiv(GL_COLOR, (GLint)draw_buffer, unsigned_value);
        }
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

U32 LLRenderTarget::getInternalFormat(U32 attachment) const
{
    if (attachment < mInternalFormat.size())
    {
        return mInternalFormat[attachment];
    }
    // setColorAttachment's texture, the one attachment of a target that leaves mInternalFormat empty.
    return attachment == 0 && mInternalFormat.empty() && !mTex.empty() ? mExternalFormat : 0;
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
    // produces values that mean nothing. An integer attachment is read texel by texel wherever
    // it is: GL filters no integers, and a filtering sampler leaves it incomplete, reading 0.
    //
    // Mirror either way. A caller that wants something else asks for it through bindTexture.
    const bool filtered = attachment == 0 && !LLImageGL::isIntegerFormat(getInternalFormat(attachment));
    return gGL.getSampler(filtered ? ALSamplers::BilinearMirror : ALSamplers::PointMirror);
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
        LLGLSLShader* profiled = LLGLSLShader::suspendProfileQuery();
        gGL.queries().checkPassBoundary();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        sCurFBO = 0;
        glViewport(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]);
        sCurResX = gGLViewport[2];
        sCurResY = gGLViewport[3];
        glReadBuffer(GL_BACK);
        glDrawBuffer(GL_BACK);
        LLGLSLShader::resumeProfileQuery(profiled);
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
    // What gGL has batched is drawn before the blit reads or overwrites it.
    gGL.flush();

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

void LLRenderTarget::copyFromBound(S32 srcX0, S32 srcY0, S32 srcX1, S32 srcY1, S32 dstX0, S32 dstY0, S32 dstX1, S32 dstY1,
                                   U32 filter)
{
    // What gGL has batched is drawn into the bound framebuffer before the blit reads it.
    gGL.flush();

    if (!mFBO)
    {
        LL_WARNS() << "Cannot copy into a render target without a framebuffer." << LL_ENDL;
        return;
    }

    LL_PROFILE_GPU_ZONE("LLRenderTarget::copyFromBound");
    // glBlitFramebuffer writes only where the scissor test passes.
    LLGLDisable scissor(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sCurFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFBO);
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, filter);
    stop_glerror();
    glBindFramebuffer(GL_FRAMEBUFFER, sCurFBO);
}

bool LLRenderTarget::isComplete() const
{
    // A sharer with no colour of its own renders into its owner's depth.
    return !mTex.empty() || mDepth || sharesDepth();
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

    // What gGL has batched goes to the attachments selected when it was drawn.
    gGL.flush();
    glDrawBuffers(static_cast<GLsizei>(wanted), drawbuffers);
    for (U32 draw_buffer = 0; draw_buffer < mDrawBuffers.size(); ++draw_buffer)
    {
        mDrawBuffers[draw_buffer] = draw_buffer < wanted ? (S8)draw_buffer : -1;
    }
}

void LLRenderTarget::setDrawOutput(U32 output)
{
    llassert(sBoundTarget == this);
    llassert(!mTex.empty());

    // As many outputs as the G-buffer writes; GL guarantees at least eight draw buffers.
    GLenum drawbuffers[] = { GL_NONE, GL_NONE, GL_NONE, GL_NONE };
    llassert(output < LL_ARRAY_SIZE(drawbuffers));
    output = llmin(output, (U32)LL_ARRAY_SIZE(drawbuffers) - 1);
    drawbuffers[output] = GL_COLOR_ATTACHMENT0;

    gGL.flush();
    glDrawBuffers(static_cast<GLsizei>(output + 1), drawbuffers);
    mDrawBuffers.fill(-1);
    mDrawBuffers[output] = 0;
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
    // A sharer's depth is attached to its framebuffer, so which build of it is attached goes with the framebuffer.
    std::swap(mSharedDepthSerial, other.mSharedDepthSerial);

    // A label names the target, and the objects each holds now were named for the other.
    if (labels_objects() && mLabel != other.mLabel)
    {
        for (const LLRenderTarget* target : { this, &other })
        {
            target->labelFramebuffer();
            for (U32 attachment = 0; attachment < target->mInternalFormat.size(); ++attachment)
            {
                target->labelColor(attachment);
            }
        }
    }
}
