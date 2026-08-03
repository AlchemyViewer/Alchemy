/**
 * @file llimagegl.cpp
 * @brief Generic GL image handler
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


// TODO: create 2 classes for images w/ and w/o discard levels?

#include "linden_common.h"

#include "llimagegl.h"

#include "llerror.h"
#include "llfasttimer.h"
#include "llimage.h"

#include "llmath.h"
#include "llgl.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llwindow.h"
#include "llframetimer.h"
#include <bit>
#include <boost/unordered_map.hpp>

extern LL_COMMON_API bool on_main_thread();

#if !LL_IMAGEGL_THREAD_CHECK
#define checkActiveThread()
#endif

//----------------------------------------------------------------------------
const F32 MIN_TEXTURE_LIFETIME = 10.f;

constexpr int DELETE_DELAY = 3; // number of frames to wait before deleting textures
static std::vector<U32> sFreeList[DELETE_DELAY+1];

// Number of mip levels in a full pyramid for the given level-0 dimensions, counting
// level 0 itself: 256x256 -> 9 (256,128,...,1). This is a COUNT. Note llvertexbuffer's
// wpo2() returns log2, i.e. the highest mip *index*, which is one less -- it used to be
// assigned to mMipLevels here, and must not be substituted for this.
S32 LLImageGL::calcMipLevelCount(S32 width, S32 height)
{
    S32 levels = 1;
    while (width > 1 || height > 1)
    {
        width = llmax(1, width >> 1);
        height = llmax(1, height >> 1);
        ++levels;
    }
    return levels;
}


U32 LLImageGL::sFrameCount = 0;


// texture memory accounting (for macOS)
static LLMutex sTexMemMutex;
static boost::unordered_map<U32, U64> sTextureAllocs;
static U64 sTextureBytes = 0;

// track a texture alloc on the currently bound texture.
// asserts that no currently tracked alloc exists
void LLImageGLMemory::alloc_tex_image(U32 width, U32 height, U32 intformat, U32 count, bool has_mips)
{
    U32 texUnit = gGL.getCurrentTexUnitIndex();
    llassert(texUnit == 0); // allocations should always be done on tex unit 0
    U32 texName = gGL.getTextureSlot(texUnit)->getCurrTexture();
    U64 size = LLImageGL::dataFormatVRAMBytes(intformat, width, height);
    if (has_mips)
    {
        // Sum the mip pyramid down to 1x1 the same way getMipBytes does,
        // so non-power-of-two and non-square cases stay exact rather than
        // relying on the 4/3 geometric-series approximation.
        S32 w = (S32)width;
        S32 h = (S32)height;
        while (w > 1 && h > 1)
        {
            w >>= 1; if (w == 0) w = 1;
            h >>= 1; if (h == 0) h = 1;
            size += LLImageGL::dataFormatVRAMBytes(intformat, w, h);
        }
    }
    size *= count;

    llassert(size >= 0);

    sTexMemMutex.lock();

    // it is a precondition that no existing allocation exists for this texture
    llassert(sTextureAllocs.find(texName) == sTextureAllocs.end());

    sTextureAllocs[texName] = size;
    sTextureBytes += size;

    sTexMemMutex.unlock();
}

// track texture free on given texName
void LLImageGLMemory::free_tex_image(U32 texName)
{
    sTexMemMutex.lock();
    auto iter = sTextureAllocs.find(texName);
    if (iter != sTextureAllocs.end()) // sometimes a texName will be "freed" before allocated (e.g. first call to setManualImage for a given texName)
    {
        llassert(iter->second <= sTextureBytes); // sTextureBytes MUST NOT go below zero

        sTextureBytes -= iter->second;

        sTextureAllocs.erase(iter);
    }

    sTexMemMutex.unlock();
}

// track texture free on given texNames
void LLImageGLMemory::free_tex_images(U32 count, const U32* texNames)
{
    for (U32 i = 0; i < count; ++i)
    {
        free_tex_image(texNames[i]);
    }
}

// track texture free on currently bound texture
void LLImageGLMemory::free_cur_tex_image()
{
    U32 texUnit = gGL.getCurrentTexUnitIndex();
    llassert(texUnit == 0); // frees should always be done on tex unit 0
    U32 texName = gGL.getTextureSlot(texUnit)->getCurrTexture();
    free_tex_image(texName);
}

using namespace LLImageGLMemory;

// static
U64 LLImageGL::getTextureBytesAllocated()
{
    return sTextureBytes;
}

//statics

U32 LLImageGL::sUniqueCount             = 0;
U32 LLImageGL::sBindCount               = 0;
S32 LLImageGL::sCount                   = 0;

F32 LLImageGL::sLastFrameTime           = 0.f;
LLImageGL* LLImageGL::sDefaultGLTexture = NULL ;
boost::unordered_set<LLImageGL*> LLImageGL::sImageList;


bool LLImageGLThread::sEnabledTextures = false;
bool LLImageGLThread::sEnabledMedia = false;

//****************************************************************************************************
//The below for texture auditing use only
//****************************************************************************************************
//-----------------------
//debug use
S32 LLImageGL::sCurTexSizeBar = -1 ;
S32 LLImageGL::sCurTexPickSize = -1 ;
S32 LLImageGL::sMaxCategories = 1 ;

//optimization for when we don't need to calculate mIsMask
bool LLImageGL::sSkipAnalyzeAlpha;
U32  LLImageGL::sScratchPBO = 0;
U32  LLImageGL::sScratchPBOSize = 0;


//------------------------
//****************************************************************************************************
//End for texture auditing use only
//****************************************************************************************************

//**************************************************************************************
//below are functions for debug use
//do not delete them even though they are not currently being used.

void LLImageGL::checkTexSize(bool forced) const
{
    if ((forced || gDebugGL) && mTarget == GL_TEXTURE_2D)
    {
        {
            //check viewport
            GLint vp[4] ;
            glGetIntegerv(GL_VIEWPORT, vp) ;
        }

        GLint texname;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texname);
        bool error = false;
        if (texname != mTexName)
        {
            LL_INFOS() << "Bound: " << texname << " Should bind: " << mTexName << " Default: " << (sDefaultGLTexture ? sDefaultGLTexture->getTexName() : 0 ) << LL_ENDL;

            error = true;
            if (gDebugSession)
            {
                gFailLog << "Invalid texture bound!" << std::endl;
            }
            else
            {
                LL_ERRS() << "Invalid texture bound!" << LL_ENDL;
            }
        }
        stop_glerror() ;
        LLGLint x = 0, y = 0 ;
        glGetTexLevelParameteriv(mTarget, 0, GL_TEXTURE_WIDTH, (GLint*)&x);
        glGetTexLevelParameteriv(mTarget, 0, GL_TEXTURE_HEIGHT, (GLint*)&y) ;
        stop_glerror() ;

        if(!x || !y)
        {
            return ;
        }
        // Clamp like getWidth/getHeight: mCurrentDiscardLevel can be -1
        // when no upload has happened yet; shifting by a negative value
        // is UB. Treat "no discard set" as full resolution.
        const S32 cur_discard = llmax<S32>(mCurrentDiscardLevel, 0);
        if(x != (mWidth >> cur_discard) || y != (mHeight >> cur_discard))
        {
            error = true;
            if (gDebugSession)
            {
                gFailLog << "wrong texture size and discard level!" <<
                    mWidth << " Height: " << mHeight << " Current Level: " << (S32)mCurrentDiscardLevel << std::endl;
            }
            else
            {
                LL_ERRS() << "wrong texture size and discard level: width: " <<
                    mWidth << " Height: " << mHeight << " Current Level: " << (S32)mCurrentDiscardLevel << LL_ENDL ;
            }
        }

        if (error)
        {
            ll_fail("LLImageGL::checkTexSize failed.");
        }
    }
}
//end of debug functions
//**************************************************************************************

//----------------------------------------------------------------------------
constexpr bool is_little_endian()
{
    return std::endian::native == std::endian::little;
}

//static
void LLImageGL::initClass(LLWindow* window, S32 num_catagories, bool skip_analyze_alpha /* = false */, bool thread_texture_loads /* = false */, bool thread_media_updates /* = false */)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    sSkipAnalyzeAlpha = skip_analyze_alpha;

    if (sScratchPBO == 0)
    {
        glGenBuffers(1, &sScratchPBO);
    }

    if (thread_texture_loads || thread_media_updates)
    {
        LLImageGLThread::createInstance(window);
        LLImageGLThread::sEnabledTextures = thread_texture_loads;
        LLImageGLThread::sEnabledMedia = thread_media_updates;
    }
}

//static
void LLImageGL::cleanupClass()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLImageGLThread::deleteSingleton();

    if (sScratchPBO != 0)
    {
        glDeleteBuffers(1, &sScratchPBO);
        sScratchPBO = 0;
        sScratchPBOSize = 0;
    }

    // Drain the deferred-delete ring so leftover texture names don't dangle
    // in sTextureAllocs across re-init (the test fixture calls cleanupClass
    // between tests). Always clear the C++ bookkeeping; only issue
    // glDeleteTextures if GL is still around.
    const bool gl_alive = gGLManager.mInited;
    for (S32 i = 0; i < DELETE_DELAY + 1; ++i)
    {
        if (!sFreeList[i].empty())
        {
            free_tex_images((GLsizei)sFreeList[i].size(), sFreeList[i].data());
            if (gl_alive)
            {
                glDeleteTextures((GLsizei)sFreeList[i].size(), sFreeList[i].data());
            }
            sFreeList[i].resize(0);
        }
    }

}


// Whether an internal format is *sized*, i.e. legal for glTexStorage2D. Unsized
// formats (GL_RGBA, GL_RGB, GL_RED, the legacy GL_LUMINANCE family, generic
// GL_COMPRESSED_*) let the driver choose the actual storage, which immutable
// allocation cannot express -- glTexStorage2D rejects them with GL_INVALID_ENUM.
//
// Whitelist rather than blacklist: an unrecognised format trips the assertion below
// rather than being quietly handed to glTexStorage2D.
static bool isSizedInternalFormat(S32 intformat)
{
    switch (intformat)
    {
    case GL_R8:
    case GL_RG8:
    case GL_RGB8:
    case GL_RGBA8:
    case GL_SRGB8:
    case GL_SRGB8_ALPHA8:
    case GL_RGB10_A2:
    case GL_R11F_G11F_B10F:
    case GL_RGBA16:
    case GL_R16F:
    case GL_RG16F:
    case GL_RGB16F:
    case GL_RGBA16F:
    case GL_R32F:
    case GL_RG32F:
    case GL_RGB32F:
    case GL_RGBA32F:
    // Depth and depth-stencil, as used by render target attachments. GL_DEPTH_COMPONENT
    // on its own is unsized and is correctly rejected below.
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32:
    case GL_DEPTH_COMPONENT32F:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
    // Block-compressed formats are sized and glTexStorage2D accepts them; their uploads
    // just have to go through glCompressedTexSubImage2D rather than glTexSubImage2D.
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        return true;
    default:
        return false;
    }
}

// The preconditions glTexStorage2D imposes, asserted rather than branched on.
//
// There is no mutable path any more. Immutable storage is required alongside GL 4.1 (see
// LLGLManager::initExtensions), so nothing here chooses between two ways of allocating --
// it catches a call site that would violate the requirement. Both cases below used to
// report and silently fall back to glTexImage2D; a session exercising the render paths hit
// neither, which is what made removing the fallback possible.
//
// Warned as well as asserted, because llassert compiles out unless SHOW_ASSERT is defined
// and a plain Release build does not define it -- so on the builds that actually ship, the
// assertion alone would say nothing and the only symptom would be a GL error and a texture
// that never got storage. The warning is once-per-site and costs nothing on the path where
// the invariant holds.
//
// The failure is deliberately not degraded: a texture that cannot be allocated immutably
// cannot be expressed in D3D11/12 or Vulkan either, so there is nothing to fall back to.
static void assertStorageAllocatable(U32 target, S32 intformat)
{
    // A non-2D target needs its own glTexStorage variant, called ONCE for the whole object
    // -- six cube faces are one allocation, not six -- after which the members write
    // sub-images. LLCubeMap, LLCubeMapArray and ALTexture3D each do that themselves and
    // call markStorageAllocated(), so they never arrive here.
    if (target != GL_TEXTURE_2D)
    {
        LL_WARNS_ONCE("RenderInit") << "Non-2D target 0x" << std::hex << target << std::dec
                                    << " reached the 2D allocator. Allocate it once for the "
                                    << "whole object, then call markStorageAllocated()."
                                    << LL_ENDL;
        llassert(false);
    }

    // glTexStorage2D cannot express a format that lets the driver pick the storage, and
    // D3D11 has no unsized formats at all. Name a sized one (GL_RGBA8, not GL_RGBA).
    if (!isSizedInternalFormat(intformat))
    {
        LL_WARNS_ONCE("RenderInit") << "Internal format 0x" << std::hex << intformat << std::dec
                                    << " is unsized, so glTexStorage2D cannot express it. Name "
                                    << "a sized format (GL_RGBA8, not GL_RGBA)." << LL_ENDL;
        llassert(false);
    }
}


//static
S32 LLImageGL::dataFormatBits(S32 dataformat)
{
    switch (dataformat)
    {
    case GL_COMPRESSED_RED:                         return 8;
    case GL_COMPRESSED_RG:                          return 16;
    case GL_COMPRESSED_RGB:                         return 24;
    case GL_COMPRESSED_SRGB:                        return 24;
    case GL_COMPRESSED_RGBA:                        return 32;
    case GL_COMPRESSED_SRGB_ALPHA:                  return 32;
    case GL_COMPRESSED_LUMINANCE:                   return 8;
    case GL_COMPRESSED_LUMINANCE_ALPHA:             return 16;
    case GL_COMPRESSED_ALPHA:                       return 8;
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:          return 4;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:    return 4;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:          return 8;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:    return 8;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:          return 8;
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:    return 8;
    case GL_LUMINANCE:                              return 8;
    case GL_LUMINANCE8:                             return 8;
    case GL_ALPHA:                                  return 8;
    case GL_ALPHA8:                                 return 8;
    case GL_RED:                                    return 8;
    case GL_R8:                                     return 8;
    case GL_COLOR_INDEX:                            return 8;
    case GL_LUMINANCE_ALPHA:                        return 16;
    case GL_LUMINANCE8_ALPHA8:                      return 16;
    case GL_RG:                                     return 16;
    case GL_RG8:                                    return 16;
    case GL_RGB:                                    return 24;
    case GL_SRGB:                                   return 24;
    case GL_RGB8:                                   return 24;
    case GL_SRGB8:                                  return 24;
    case GL_R11F_G11F_B10F:                         return 32;
    case GL_RGBA:                                   return 32;
    case GL_RGBA8:                                  return 32;
    case GL_RGB10_A2:                               return 32;
    case GL_SRGB_ALPHA:                             return 32;
    case GL_SRGB8_ALPHA8:                           return 32;
    case GL_BGRA:                                   return 32;      // Used for QuickTime media textures on the Mac
    case GL_DEPTH_COMPONENT:                        return 24;
    case GL_DEPTH_COMPONENT24:                      return 24;
    case GL_DEPTH_COMPONENT32F:                     return 32;
    case GL_DEPTH24_STENCIL8:                       return 32;
    case GL_DEPTH32F_STENCIL8:                      return 64; // 32 for depth, 8 for stencil, stencil is still allocated as 24+8 internally GL_FLOAT_32_UNSIGNED_INT_24_8_REV
    case GL_RGBA16:                                 return 64;
    case GL_R16F:                                   return 16;
    case GL_RG16F:                                  return 32;
    case GL_RGB16F:                                 return 48;
    case GL_RGBA16F:                                return 64;
    case GL_R32F:                                   return 32;
    case GL_RG32F:                                  return 64;
    case GL_RGB32F:                                 return 96;
    case GL_RGBA32F:                                return 128;
    default:
        LL_ERRS() << "LLImageGL::Unknown format: " << std::hex << dataformat << std::dec << LL_ENDL;
        return 0;
    }
}

//static
S64 LLImageGL::dataFormatBytes(S32 dataformat, S32 width, S32 height)
{
    switch (dataformat)
    {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        if (width < 4) width = 4;
        if (height < 4) height = 4;
        break;
    default:
        break;
    }
    S64 bytes (((S64)width * (S64)height * (S64)dataFormatBits(dataformat)+7)>>3);
    S64 aligned = (bytes+3)&~3;
    return aligned;
}

//static
S32 LLImageGL::dataFormatVRAMBits(S32 dataformat)
{
    // For formats where driver-side storage diverges from the tight
    // host layout, return the padded width. Everything else delegates
    // to dataFormatBits so adding a new format to the host table also
    // covers VRAM accounting by default.
    switch (dataformat)
    {
    case GL_RGB8:                   return 32;  // padded to RGBX
    case GL_SRGB8:                  return 32;  // padded, as GL_RGB8
    case GL_RGB16F:                 return 64;  // padded to RGBA16F
    case GL_RGB32F:                 return 128; // padded to RGBA32F
    case GL_DEPTH_COMPONENT24:      return 32;  // padded to 32-bit
    default:                        return dataFormatBits(dataformat);
    }
}

//static
S64 LLImageGL::dataFormatVRAMBytes(S32 dataformat, S32 width, S32 height)
{
    switch (dataformat)
    {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        if (width < 4) width = 4;
        if (height < 4) height = 4;
        break;
    default:
        break;
    }
    S64 bytes (((S64)width * (S64)height * (S64)dataFormatVRAMBits(dataformat)+7)>>3);
    S64 aligned = (bytes+3)&~3;
    return aligned;
}

//static
S32 LLImageGL::dataFormatComponents(S32 dataformat)
{
    switch (dataformat)
    {
      case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:    return 3;
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: return 3;
      case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:    return 4;
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT: return 4;
      case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:    return 4;
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: return 4;
      case GL_LUMINANCE:                        return 1;
      case GL_ALPHA:                            return 1;
      case GL_RED:                              return 1;
      case GL_COLOR_INDEX:                      return 1;
      case GL_LUMINANCE_ALPHA:                  return 2;
      case GL_RG:                               return 2;
      case GL_RGB:                              return 3;
      case GL_SRGB:                             return 3;
      case GL_RGBA:                             return 4;
      case GL_SRGB_ALPHA:                       return 4;
      case GL_BGRA:                             return 4;       // Used for QuickTime media textures on the Mac
      default:
        LL_ERRS() << "LLImageGL::Unknown format: " << std::hex << dataformat << std::dec << LL_ENDL;
        return 0;
    }
}

//----------------------------------------------------------------------------

// static
void LLImageGL::updateStats(F32 current_time)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    sLastFrameTime = current_time;

    // Per-frame counters. sBindCount/sUniqueCount are labelled per-frame and read that way by
    // the HUD, but went years without this reset -- they were lifetime totals.
    sBindCount   = 0;
    sUniqueCount = 0;

    ALTextureSlot::sSamplerBinds = 0;
    ALTextureSlot::sSamplerSkips = 0;
    ALTextureSlot::sTextureBinds = 0;
    ALTextureSlot::sSamplerBindsFlushed = 0;
    ALTextureSlot::sSamplerBindsSplitBatch = 0;
    ALTextureSlot::sSamplerBindsShadowCycle = 0;
}

//----------------------------------------------------------------------------

//static
void LLImageGL::destroyGL()
{
    for (S32 stage = 0; stage < gGLManager.mNumTextureImageUnits; stage++)
    {
        gGL.getTextureSlot(stage)->unbind();
    }
}

//static
//----------------------------------------------------------------------------

//for server side use only.
//static
bool LLImageGL::create(LLPointer<LLImageGL>& dest, bool usemipmaps)
{
    dest = new LLImageGL(usemipmaps);
    return true;
}

//for server side use only.
bool LLImageGL::create(LLPointer<LLImageGL>& dest, U32 width, U32 height, U8 components, bool usemipmaps)
{
    dest = new LLImageGL(width, height, components, usemipmaps);
    return true;
}

//for server side use only.
bool LLImageGL::create(LLPointer<LLImageGL>& dest, const LLImageRaw* imageraw, bool usemipmaps)
{
    dest = new LLImageGL(imageraw, usemipmaps);
    return true;
}

//----------------------------------------------------------------------------

LLImageGL::LLImageGL(bool usemipmaps/* = true*/)
:   mSaveData(0), mExternalTexture(false)
{
    init(usemipmaps);
    setSize(0, 0, 0);
    sImageList.insert(this);
    sCount++;
}

LLImageGL::LLImageGL(U32 width, U32 height, U8 components, bool usemipmaps/* = true*/)
:   mSaveData(0), mExternalTexture(false)
{
    llassert( components <= 4 );
    init(usemipmaps);
    setSize(width, height, components);
    sImageList.insert(this);
    sCount++;
}

LLImageGL::LLImageGL(const LLImageRaw* imageraw, bool usemipmaps/* = true*/)
:   mSaveData(0), mExternalTexture(false)
{
    init(usemipmaps);
    setSize(0, 0, 0);
    sImageList.insert(this);
    sCount++;

    createGLTexture(0, imageraw);
}

LLImageGL::LLImageGL(
    LLGLuint texName,
    U32 components,
    LLGLenum target,
    LLGLint  formatInternal,
    LLGLenum formatPrimary,
    LLGLenum formatType)
:   mExternalTexture(true)  // ctor previously left this uninitialized,
                            // making the dtor's `!mExternalTexture && ...`
                            // gate read indeterminate memory — could go
                            // either way on any given run, in the wrong
                            // direction either calling glDeleteTextures on
                            // a texture we don't own or skipping the
                            // sImageList.erase / sCount-- the other ctors
                            // would do (sCount drifts negative over time).
{
    init(false);
    mTexName = texName;
    mTarget = target;
    mComponents = components;
    mFormatType = formatType;
    mFormatInternal = formatInternal;
    mFormatPrimary = formatPrimary;
}


LLImageGL::~LLImageGL()
{
    if (!mExternalTexture)
    {
        // Always run the C++ bookkeeping (sImageList/sCount/freePickMask)
        // even if GL is gone. The previous gate on gGLManager.mInited left
        // stale `this` pointers in sImageList when an LLImageGL outlived GL
        // teardown — a later iteration (e.g. dirtyTexOptions) would UAF.
        // cleanup() is C++-safe on its own: destroyGLTexture is gated on
        // mIsDisabled, and deleteTextures no-ops when mInited is false.
        cleanup();
        sImageList.erase(this);
        freePickMask();
        sCount--;
    }
}

void LLImageGL::init(bool usemipmaps)
{
#if LL_IMAGEGL_THREAD_CHECK
    mActiveThread = LLThread::currentID();
#endif

    // keep these members in the same order as declared in llimagehl.h
    // so that it is obvious by visual inspection if we forgot to
    // init a field.

    mTextureMemory = S64Bytes(0);
    mLastBindTime = 0.f;

    mPickMask = NULL;
    mPickMaskWidth = 0;
    mPickMaskHeight = 0;
    mUseMipMaps = usemipmaps;
    mHasExplicitFormat = false;

    mIsMask = false;
    mNeedsAlphaAndPickMask = true ;
    mAlphaStride = 0 ;
    mAlphaOffset = 0 ;

    mGLTextureCreated = false ;
    mTexName = 0;
    mWidth = 0;
    mHeight = 0;
    mCurrentDiscardLevel = -1;

    mTarget = GL_TEXTURE_2D;
    mBindTarget = ALTextureSlot::TT_TEXTURE;
    mHasMipMaps = false;
    mMipLevels = 0;

    mIsResident = 0;

    mComponents = 0;
    mMaxDiscardLevel = MAX_DISCARD_LEVEL;

    mFormatInternal = -1;
    mFormatPrimary = (LLGLenum) 0;
    mFormatType = GL_UNSIGNED_BYTE;
    mFormatSwapBytes = false;

    mDeprecatedSourceFormat = 0;

#ifdef DEBUG_MISS
    mMissed = false;
#endif

    mCategory = -1;

    // Sometimes we have to post work for the main thread.
    mMainQueue = LL::WorkQueue::getInstance("mainloop");
}

void LLImageGL::cleanup()
{
    if (!gGLManager.mIsDisabled)
    {
        destroyGLTexture();
    }
    freePickMask();

    mSaveData = NULL; // deletes data
}

//----------------------------------------------------------------------------

//static
bool LLImageGL::checkSize(S32 width, S32 height)
{
    if (width < 0 || height < 0)
    {
        return false;
    }
    return true;
}

bool LLImageGL::setSize(S32 width, S32 height, S32 ncomponents, S32 discard_level)
{
    if (width != mWidth || height != mHeight || ncomponents != mComponents)
    {
        // checkSize only rejects negative dimensions today (NPOT support
        // is universal in the GL versions we target); the comment used
        // to claim a power-of-two check that doesn't actually run.
        if (!checkSize(width, height))
        {
            LL_WARNS() << llformat("Texture has negative dimension: %dx%d",width,height) << LL_ENDL;
            return false;
        }

        mWidth = width;
        mHeight = height;
        mComponents = ncomponents;
        if (ncomponents > 0)
        {
            mMaxDiscardLevel = 0;
            while (width > 1 && height > 1 && mMaxDiscardLevel < MAX_DISCARD_LEVEL)
            {
                mMaxDiscardLevel++;
                width >>= 1;
                height >>= 1;
            }

            if(discard_level > 0)
            {
                mMaxDiscardLevel = llmax(mMaxDiscardLevel, (S8)discard_level);
            }
        }
        else
        {
            mMaxDiscardLevel = MAX_DISCARD_LEVEL;
        }
    }

    return true;
}

//----------------------------------------------------------------------------

// virtual
void LLImageGL::dump()
{
    LL_INFOS() << "mMaxDiscardLevel " << S32(mMaxDiscardLevel)
            << " mLastBindTime " << mLastBindTime
            << " mTarget " << S32(mTarget)
            << " mBindTarget " << S32(mBindTarget)
            << " mUseMipMaps " << S32(mUseMipMaps)
            << " mHasMipMaps " << S32(mHasMipMaps)
            << " mCurrentDiscardLevel " << S32(mCurrentDiscardLevel)
            << " mFormatInternal " << S32(mFormatInternal)
            << " mFormatPrimary " << S32(mFormatPrimary)
            << " mFormatType " << S32(mFormatType)
            << " mFormatSwapBytes " << S32(mFormatSwapBytes)
            << " mHasExplicitFormat " << S32(mHasExplicitFormat)
#if DEBUG_MISS
            << " mMissed " << mMissed
#endif
            << LL_ENDL;

    LL_INFOS() << " mTextureMemory " << mTextureMemory
            << " mTexNames " << mTexName
            << " mIsResident " << S32(mIsResident)
            << LL_ENDL;
}

//----------------------------------------------------------------------------
void LLImageGL::forceUpdateBindStats(void) const
{
    mLastBindTime = sLastFrameTime;
}

bool LLImageGL::updateBindStats() const
{
    if (mTexName != 0)
    {
#ifdef DEBUG_MISS
        mMissed = ! getIsResident(true);
#endif
        sBindCount++;
        if (mLastBindTime != sLastFrameTime)
        {
            // we haven't accounted for this texture yet this frame
            sUniqueCount++;
            mLastBindTime = sLastFrameTime;

            return true ;
        }
    }
    return false ;
}

F32 LLImageGL::getTimePassedSinceLastBound()
{
    return sLastFrameTime - mLastBindTime ;
}

void LLImageGL::setExplicitFormat( LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format, bool swap_bytes )
{
    // Note: must be called before createTexture()
    // Note: it's up to the caller to ensure that the format matches the number of components.
    mHasExplicitFormat = true;
    mFormatInternal = internal_format;
    mFormatPrimary = primary_format;
    if(type_format == 0)
        mFormatType = GL_UNSIGNED_BYTE;
    else
        mFormatType = type_format;
    mFormatSwapBytes = swap_bytes;

    // Order matters: alpha-stride/offset depends on the deprecated format
    // names (LUMINANCE_ALPHA → stride 2, etc.). Compute that first, then
    // rewrite to core-profile-valid forms.
    calcAlphaChannelOffsetAndStride() ;
    resolveDeprecatedFormat();
}

//----------------------------------------------------------------------------

void LLImageGL::setImage(const LLImageRaw* imageraw)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    llassert((imageraw->getWidth() == liveWidth(mCurrentDiscardLevel)) &&
             (imageraw->getHeight() == liveHeight(mCurrentDiscardLevel)) &&
             (imageraw->getComponents() == mComponents));
    const U8* rawdata = imageraw->getData();
    setImage(rawdata, false);
}

bool LLImageGL::setImage(const U8* data_in, bool data_hasmips /* = false */, S32 usename /* = 0 */)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    const bool is_compressed = isCompressed();

    if (mUseMipMaps)
    {
        //set has mip maps to true before binding image so tex parameters get set properly
        gGL.getTextureSlot(0)->unbind();

        mHasMipMaps = true;
    }
    else
    {
        mHasMipMaps = false;
    }

    gGL.getTextureSlot(0)->bind(this, false, false, usename);

    // Allocate the whole texture up front, so every write below is a sub-image.
    // glTexStorage2D must be called exactly once for the object and needs the level count
    // before any pixels exist, which is why this cannot live inside the per-level loops.
    //
    // Skipped when storage already exists: either allocated here on an earlier pass (the
    // early re-upload path, where createGLTexture writes into the live mTexName because the
    // size has not changed), or by the owner of a shared texture object -- see
    // markStorageAllocated, which is how cube maps work. Reallocating is illegal either way.
    // Live members, not the getters: during an off-thread createGLTexture the getters
    // answer for the still-published texture (mUploadInFlight), and storage sized from
    // that would build the new texture at the old texture's dimensions.
    if (!mStorageAllocated)
    {
        free_cur_tex_image();
        allocateTextureStorage(liveWidth(mCurrentDiscardLevel), liveHeight(mCurrentDiscardLevel), mUseMipMaps);
    }

    if (data_in == nullptr)
    {
        // Storage is the whole point of this call; there are no pixels to write.
    }
    else if (mUseMipMaps)
    {
        if (data_hasmips)
        {
            // NOTE: data_in points to largest image; smaller images
            // are stored BEFORE the largest image
            for (S32 d=mCurrentDiscardLevel; d<=mMaxDiscardLevel; d++)
            {

                S32 w = liveWidth(d);
                S32 h = liveHeight(d);
                S32 gl_level = d-mCurrentDiscardLevel;

                mMipLevels = llmax(mMipLevels, gl_level + 1);

                if (d > mCurrentDiscardLevel)
                {
                    data_in -= dataFormatBytes(mFormatPrimary, w, h); // see above comment
                }
                if (is_compressed)
                {
                    GLsizei tex_size = (GLsizei)dataFormatBytes(mFormatPrimary, w, h);
                    glCompressedTexSubImage2D(mTarget, gl_level, 0, 0, w, h, mFormatPrimary, tex_size, (GLvoid *)data_in);
                    stop_glerror();
                }
                else
                {
                    if(mFormatSwapBytes)
                    {
                        glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
                        stop_glerror();
                    }

                    LLImageGL::setManualSubImage(mTarget, gl_level, w, h, mFormatPrimary, mFormatType, (GLvoid*)data_in);
                    if (gl_level == 0)
                    {
                        analyzeAlpha(data_in, w, h);
                        // Match the auto-mip and manual-mip paths: pick mask
                        // is built once from the largest level. Calling it
                        // every iteration overwrote the mask with each
                        // smaller mip's data, leaving the final mask at the
                        // coarsest resolution.
                        updatePickMask(w, h, data_in);
                    }

                    if(mFormatSwapBytes)
                    {
                        glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
                        stop_glerror();
                    }

                    stop_glerror();
                }
                stop_glerror();
            }
        }
        else if (!is_compressed)
        {
            if (mAutoGenMips)
            {
                stop_glerror();
                {
                    if(mFormatSwapBytes)
                    {
                        glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
                        stop_glerror();
                    }

                    S32 w = liveWidth(mCurrentDiscardLevel);
                    S32 h = liveHeight(mCurrentDiscardLevel);

                    mMipLevels = calcMipLevelCount(w, h);

                    LLImageGL::setManualSubImage(mTarget, 0, w, h, mFormatPrimary, mFormatType, data_in);
                    analyzeAlpha(data_in, w, h);
                    stop_glerror();

                    updatePickMask(w, h, data_in);

                    if(mFormatSwapBytes)
                    {
                        glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
                        stop_glerror();
                    }

                    {
                        LL_PROFILE_GPU_ZONE("generate mip map");
                        // generateMipmaps clears the slot's sampler first, which matters
                        // now that storage can be sRGB: mip generation follows the slot's
                        // TEXTURE_SRGB_DECODE -- sampler first if one is bound, else the
                        // texture's (EXT_texture_sRGB_decode issue 10) -- and the bind()
                        // above deliberately left whatever the last draw used. Clearing it
                        // lets the texture's own SKIP_DECODE govern, so sRGB-stored data
                        // always mips as raw bytes rather than by frame timing.
                        generateMipmaps(mTarget);
                    }
                    stop_glerror();
                }
            }
            else
            {
                // Create mips by hand
                // ~4x faster than gluBuild2DMipmaps
                S32 width = liveWidth(mCurrentDiscardLevel);
                S32 height = liveHeight(mCurrentDiscardLevel);
                S32 nummips = mMaxDiscardLevel - mCurrentDiscardLevel + 1;
                S32 w = width, h = height;


                const U8* prev_mip_data = 0;
                const U8* cur_mip_data = 0;
#ifdef SHOW_ASSERT
                S32 cur_mip_size = 0;
#endif
                mMipLevels = nummips;

                for (int m=0; m<nummips; m++)
                {
                    if (m==0)
                    {
                        cur_mip_data = data_in;
#ifdef SHOW_ASSERT
                        cur_mip_size = width * height * mComponents;
#endif
                    }
                    else
                    {
                        S32 bytes = w * h * mComponents;
#ifdef SHOW_ASSERT
                        llassert(prev_mip_data);
                        llassert(cur_mip_size == bytes*4);
#endif
                        U8* new_data = new(std::nothrow) U8[bytes];
                        if (!new_data)
                        {
                            stop_glerror();

                            // At m == 1, both prev_mip_data and cur_mip_data
                            // are still aliased to the caller's data_in (the
                            // m==0 iteration set prev = cur = data_in). Guard
                            // every delete against the caller-owned buffer.
                            if (prev_mip_data && prev_mip_data != data_in
                                && prev_mip_data != cur_mip_data)
                            {
                                delete[] prev_mip_data;
                            }
                            prev_mip_data = nullptr;
                            if (cur_mip_data && cur_mip_data != data_in)
                            {
                                delete[] cur_mip_data;
                            }
                            cur_mip_data = nullptr;

                            mGLTextureCreated = false;
                            return false;
                        }
                        else
                        {

#ifdef SHOW_ASSERT
                            llassert(prev_mip_data);
                            llassert(cur_mip_size == bytes * 4);
#endif

                            LLImageBase::generateMip(prev_mip_data, new_data, w, h, mComponents);
                            cur_mip_data = new_data;
#ifdef SHOW_ASSERT
                            cur_mip_size = bytes;
#endif
                        }

                    }
                    llassert(w > 0 && h > 0 && cur_mip_data);
                    (void)cur_mip_data;
                    {
                        if(mFormatSwapBytes)
                        {
                            glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
                            stop_glerror();
                        }

                        LLImageGL::setManualSubImage(mTarget, m, w, h, mFormatPrimary, mFormatType, cur_mip_data);
                        if (m == 0)
                        {
                            analyzeAlpha(data_in, w, h);
                        }
                        stop_glerror();
                        if (m == 0)
                        {
                            updatePickMask(w, h, cur_mip_data);
                        }

                        if(mFormatSwapBytes)
                        {
                            glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
                            stop_glerror();
                        }
                    }
                    if (prev_mip_data && prev_mip_data != data_in)
                    {
                        delete[] prev_mip_data;
                    }
                    prev_mip_data = cur_mip_data;
                    w >>= 1;
                    h >>= 1;
                }
                if (prev_mip_data && prev_mip_data != data_in)
                {
                    delete[] prev_mip_data;
                    prev_mip_data = NULL;
                }
            }
        }
        else
        {
            LL_ERRS() << "Compressed Image has mipmaps but data does not (can not auto generate compressed mips)" << LL_ENDL;
        }
    }
    else
    {
        mMipLevels = 1;
        S32 w = liveWidth(mCurrentDiscardLevel);
        S32 h = liveHeight(mCurrentDiscardLevel);
        if (is_compressed)
        {
            GLsizei tex_size = (GLsizei)dataFormatBytes(mFormatPrimary, w, h);
            glCompressedTexSubImage2D(mTarget, 0, 0, 0, w, h, mFormatPrimary, tex_size, (GLvoid *)data_in);
            stop_glerror();
        }
        else
        {
            if(mFormatSwapBytes)
            {
                glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
                stop_glerror();
            }

            LLImageGL::setManualSubImage(mTarget, 0, w, h, mFormatPrimary, mFormatType, (GLvoid *)data_in);
            stop_glerror();

            analyzeAlpha(data_in, w, h);
            stop_glerror();

            updatePickMask(w, h, data_in);

            stop_glerror();

            if(mFormatSwapBytes)
            {
                glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
                stop_glerror();
            }

        }
    }
    stop_glerror();
    mGLTextureCreated = true;
    return true;
}

U32 type_width_from_pixtype(U32 pixtype)
{
    U32 type_width = 0;
    switch (pixtype)
    {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
        type_width = 1;
        break;
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
        type_width = 2;
        break;
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_FLOAT:
        type_width = 4;
        break;
    default:
        LL_ERRS() << "Unknown type: " << pixtype << LL_ENDL;
    }
    return type_width;
}

// Whether to break an upload into sub_image_lines slices. This is latency smoothing,
// not throughput: it only ever applies on the main thread, where one large
// glTexSubImage2D can stall long enough to cost a frame. Off-thread uploads issue a
// single call.
//
// The compressed guard is structural, not a driver workaround: sub_image_lines slices
// by scanline stride, which is meaningless for block-compressed data. (The comment that
// used to be here blamed an NVIDIA/Win10 glTexSubImage2D bug, long since fixed -- but the
// guard would still be required without it.) setSubImage can pass a genuinely
// block-compressed texture; the allocation paths always pass false, since driver-side
// generic compression is gone.
bool should_stagger_image_set(bool compressed)
{
#if LL_MESA_HEADLESS
    return false;
#elif LL_LINUX
    return !compressed && on_main_thread() && gGLManager.mIsNVIDIA;
#elif LL_DARWIN
    return !compressed && on_main_thread() && gGLManager.mIsAMD;
#else
    // Setting media textures off-thread seems faster when not using sub_image_lines (Nvidia/Windows 10) -Cosmic,2023-03-31
    return !compressed && on_main_thread() && !gGLManager.mIsIntel;
#endif
}

// Equivalent to calling glSetSubImage2D(target, miplevel, x_offset, y_offset, width, height, pixformat, pixtype, src), assuming the total width of the image is data_width
// However, instead there are multiple calls to glSetSubImage2D on smaller slices of the image
void sub_image_lines(U32 target, S32 miplevel, S32 x_offset, S32 y_offset, S32 width, S32 height, U32 pixformat, U32 pixtype, const U8* src, S32 data_width)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    LL_PROFILE_ZONE_NUM(width);
    LL_PROFILE_ZONE_NUM(height);

    U32 components = LLImageGL::dataFormatComponents(pixformat);
    U32 type_width = type_width_from_pixtype(pixtype);

    const U32 line_width = data_width * components * type_width;
    const U32 y_offset_end = y_offset + height;

    if (width == data_width && height % 32 == 0)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("subimage - batched lines");

        // full width, batch multiple lines at a time
        // set batch size based on width
        U32 batch_size = 32;

        if (width > 1024)
        {
            batch_size = 8;
        }
        else if (width > 512)
        {
            batch_size = 16;
        }

        // full width texture, do 32 lines at a time
        for (U32 y_pos = y_offset; y_pos < y_offset_end; y_pos += batch_size)
        {
            // If this keeps crashing, pass down data_size, looks like it is using
            // imageraw->getData(); for data, but goes way over allocated size limit
            glTexSubImage2D(target, miplevel, x_offset, y_pos, width, batch_size, pixformat, pixtype, src);
            src += line_width * batch_size;
        }
    }
    else
    {
        // partial width or strange height
        for (U32 y_pos = y_offset; y_pos < y_offset_end; y_pos += 1)
        {
            // If this keeps crashing, pass down data_size, looks like it is using
            // imageraw->getData(); for data, but goes way over allocated size limit
            glTexSubImage2D(target, miplevel, x_offset, y_pos, width, 1, pixformat, pixtype, src);
            src += line_width;
        }
    }
}

bool LLImageGL::setSubImage(const U8* datap, S32 data_width, S32 data_height, S32 x_pos, S32 y_pos, S32 width, S32 height, bool force_fast_update /* = false */, LLGLuint use_name /* = 0 */, bool skip_unbind /* = false */)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (!width || !height)
    {
        return true;
    }
    LLGLuint tex_name = use_name != 0 ? use_name : mTexName;
    if (0 == tex_name)
    {
        // *TODO: Re-enable warning?  Ran into thread locking issues? DK 2011-02-18
        //LL_WARNS() << "Setting subimage on image without GL texture" << LL_ENDL;
        return false;
    }
    if (datap == NULL)
    {
        // *TODO: Re-enable warning?  Ran into thread locking issues? DK 2011-02-18
        //LL_WARNS() << "Setting subimage on image with NULL datap" << LL_ENDL;
        return false;
    }

    // HACK: allow the caller to explicitly force the fast path (i.e. using glTexSubImage2D here instead of calling setImage) even when updating the full texture.
    if (!force_fast_update && x_pos == 0 && y_pos == 0 && width == getWidth() && height == getHeight() && data_width == width && data_height == height)
    {
        // Propagate setImage failure (e.g. manual mip alloc failure) so the
        // caller doesn't see "success" with a half-uploaded texture.
        return setImage(datap, false, tex_name);
    }
    else
    {
        if (mUseMipMaps)
        {
            dump();
            LL_ERRS() << "setSubImage called with mipmapped image (not supported)" << LL_ENDL;
        }
        llassert_always(mCurrentDiscardLevel == 0);
        llassert_always(x_pos >= 0 && y_pos >= 0);

        if (((x_pos + width) > getWidth()) ||
            (y_pos + height) > getHeight())
        {
            dump();
            LL_ERRS() << "Subimage not wholly in target image!"
                   << " x_pos " << x_pos
                   << " y_pos " << y_pos
                   << " width " << width
                   << " height " << height
                   << " getWidth() " << getWidth()
                   << " getHeight() " << getHeight()
                   << LL_ENDL;
        }

        if ((x_pos + width) > data_width ||
            (y_pos + height) > data_height)
        {
            dump();
            LL_ERRS() << "Subimage not wholly in source image!"
                   << " x_pos " << x_pos
                   << " y_pos " << y_pos
                   << " width " << width
                   << " height " << height
                   << " source_width " << data_width
                   << " source_height " << data_height
                   << LL_ENDL;
        }


        glPixelStorei(GL_UNPACK_ROW_LENGTH, data_width);
        stop_glerror();

        if(mFormatSwapBytes)
        {
            glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
            stop_glerror();
        }

        const U8* sub_datap = datap + (y_pos * data_width + x_pos) * getComponents();
        // Update the GL texture
        bool res = gGL.getTextureSlot(0)->bindManual(mBindTarget, tex_name);
        if (!res) LL_ERRS() << "LLImageGL::setSubImage(): bindTexture failed" << LL_ENDL;
        stop_glerror();

        const bool use_sub_image = should_stagger_image_set(isCompressed());
        if (!use_sub_image)
        {
            // *TODO: Why does this work here, in setSubImage, but not in
            // setManualImage? Maybe because it only gets called with the
            // dimensions of the full image?  Or because the image is never
            // compressed?
            glTexSubImage2D(mTarget, 0, x_pos, y_pos, width, height, mFormatPrimary, mFormatType, sub_datap);
        }
        else
        {
            sub_image_lines(mTarget, 0, x_pos, y_pos, width, height, mFormatPrimary, mFormatType, sub_datap, data_width);
        }
        if (!skip_unbind)
        {
            gGL.getTextureSlot(0)->unbind();
        }
        stop_glerror();

        if(mFormatSwapBytes)
        {
            glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
            stop_glerror();
        }

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        stop_glerror();
        mGLTextureCreated = true;
    }
    return true;
}

bool LLImageGL::setSubImage(const LLImageRaw* imageraw, S32 x_pos, S32 y_pos, S32 width, S32 height, bool force_fast_update /* = false */, LLGLuint use_name, bool skip_unbind /* = false */)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    return setSubImage(imageraw->getData(), imageraw->getWidth(), imageraw->getHeight(), x_pos, y_pos, width, height, force_fast_update, use_name, skip_unbind);
}

// Copy sub image from frame buffer
bool LLImageGL::setSubImageFromFrameBuffer(S32 fb_x, S32 fb_y, S32 x_pos, S32 y_pos, S32 width, S32 height)
{
    if (gGL.getTextureSlot(0)->bind(this, false, true))
    {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x_pos, y_pos, fb_x, fb_y, width, height);
        mGLTextureCreated = true;
        stop_glerror();
        return true;
    }
    else
    {
        return false;
    }
}

// static
void LLImageGL::generateTextures(S32 numTextures, U32 *textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    static constexpr U32 pool_size = 1024;
    static thread_local U32 name_pool[pool_size]; // pool of texture names
    static thread_local U32 name_count = 0; // number of available names in the pool

    if (name_count == 0)
    {
        LL_PROFILE_ZONE_NAMED("iglgt - reup pool");
        // pool is emtpy, refill it
        glGenTextures(pool_size, name_pool);
        name_count = pool_size;
    }

    if ((U32)numTextures <= name_count)
    {
        //copy teture names off the end of the pool
        memcpy(textures, name_pool + name_count - numTextures, sizeof(U32) * numTextures);
        name_count -= numTextures;
    }
    else
    {
        LL_PROFILE_ZONE_NAMED("iglgt - pool miss");
        glGenTextures(numTextures, textures);
    }
}

// static
void LLImageGL::updateClass()
{
    sFrameCount++;

    // wait a few frames before actually deleting the textures to avoid
    // synchronization issues with the GPU
    U32 idx = (sFrameCount+DELETE_DELAY) % (DELETE_DELAY+1);

    if (!sFreeList[idx].empty())
    {
        free_tex_images((GLsizei) sFreeList[idx].size(), sFreeList[idx].data());
        glDeleteTextures((GLsizei)sFreeList[idx].size(), sFreeList[idx].data());
        sFreeList[idx].resize(0);
    }
}

// static
void LLImageGL::deleteTextures(S32 numTextures, const U32 *textures)
{
    if (gGLManager.mInited)
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
        U32 idx = sFrameCount % (DELETE_DELAY+1);
        for (S32 i = 0; i < numTextures; ++i)
        {
            sFreeList[idx].push_back(textures[i]);
        }
    }
}

// static
void LLImageGL::resolveUploadFormat(S32& intformat, U32& pixformat, U32& pixtype,
                                    const void*& pixels, S32 width, S32 height)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    // The deprecated formats are re-expressed as R8 / RG8 and read back through the
    // texture's swizzle attribute. GL_TEXTURE_SWIZZLE_RGBA is in both core and compat
    // profiles since GL 3.3, so this works everywhere at the 4.1 floor.
    //
    // The upload paths do not write GL_TEXTURE_SWIZZLE_RGBA themselves — overwriting it is
    // destructive when the bound texture already has a custom swizzle (e.g. an LLImageGL
    // whose resolveDeprecatedFormat ran and applied a specific mask in createGLTexture,
    // or any caller that set up its own swizzle before this call). The format rewrite
    // here still happens so the allocation gets the right backing storage; applying the
    // swizzle is the caller's responsibility. LLImageGL's createGLTexture path handles it
    // via mSwizzleMask. Direct callers that pass GL_ALPHA / GL_LUMINANCE /
    // GL_LUMINANCE_ALPHA must apply the matching swizzle themselves before uploading.
    if (pixformat == GL_ALPHA)
    { //GL_ALPHA → R8; caller-set {0,0,0,R} swizzle is required for {0,0,0,A} sample semantics
        pixformat = GL_RED;
        intformat = GL_R8;
    }

    if (pixformat == GL_LUMINANCE)
    { //GL_LUMINANCE → R8; caller-set {R,R,R,1} swizzle is required for {L,L,L,1} sample semantics
        pixformat = GL_RED;
        intformat = GL_R8;
    }

    if (pixformat == GL_LUMINANCE_ALPHA)
    { //GL_LUMINANCE_ALPHA → RG8; caller-set {R,R,R,G} swizzle is required for {L,L,L,A} sample semantics
        pixformat = GL_RG;
        intformat = GL_RG8;
    }
}

// Upload one level into a texture that already has storage. Same format resolution as the
// allocating paths, but a sub-image write, which is what makes it legal on immutable
// storage. Does no VRAM accounting -- allocateTextureStorage recorded the whole object
// when it created the storage.
void LLImageGL::setManualSubImage(U32 target, S32 miplevel, S32 width, S32 height, U32 pixformat, U32 pixtype, const void* pixels)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (pixels == nullptr)
    {
        // storage already exists and there is nothing to write into it
        return;
    }

    S32 intformat = 0; // resolved but unused; storage was fixed at allocation
    resolveUploadFormat(intformat, pixformat, pixtype, pixels, width, height);

    stop_glerror();
    {
        LL_PROFILE_ZONE_NAMED("glTexSubImage2D");
        LL_PROFILE_ZONE_NUM(width);
        LL_PROFILE_ZONE_NUM(height);

        if (should_stagger_image_set(false))
        {
            sub_image_lines(target, miplevel, 0, 0, width, height, pixformat, pixtype, (const U8*)pixels, width);
        }
        else
        {
            glTexSubImage2D(target, miplevel, 0, 0, width, height, pixformat, pixtype, pixels);
        }
    }
    stop_glerror();
}

// Opt a freshly-allocated texture out of the sRGB transfer function.
//
// GL applies it on every read of an sRGB-format texture unless told otherwise, so without
// this the decode is decided by the internal format and nothing else -- see
// ALSampler::SRGBDecode for why that is the wrong way round for this renderer.
//
// The samplers already skip it, and a sampler object overrides the texture when one is
// bound, so this is what makes a bind with sampler 0 behave the same way rather than
// quietly linearising. Harmless on a non-sRGB format: the state exists but nothing reads it.
static void skipSRGBDecode(U32 target)
{
    if (gGLManager.mHasTextureSRGBDecode)
    {
        glTexParameteri(target, GL_TEXTURE_SRGB_DECODE_EXT, GL_SKIP_DECODE_EXT);
    }
}

// Allocate a 2D texture on the currently-bound name and upload level 0.
//
// This owns the whole texture: call it exactly once per texture object. The result cannot
// then be resized or reformatted -- build a new texture instead. That is the contract
// glTexStorage2D imposes, and the one D3D11 requires of every resource.
//
// levels is the mip level COUNT (see calcMipLevelCount). Storage is allocated for all of
// them, so the caller can fill levels 1+ with setManualSubImage.
//
// pixels may be null to allocate without uploading.
void LLImageGL::allocateTexture2D(U32 target, S32 intformat, S32 width, S32 height,
                                  U32 pixformat, U32 pixtype, const void* pixels, S32 levels)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    levels = llmax(1, levels);

    resolveUploadFormat(intformat, pixformat, pixtype, pixels, width, height);

    free_cur_tex_image();

    assertStorageAllocatable(target, intformat);

    {
        LL_PROFILE_ZONE_NAMED("glTexStorage2D");
        glTexStorage2D(target, levels, intformat, width, height);
        skipSRGBDecode(target);

        if (pixels != nullptr)
        {
            if (should_stagger_image_set(false))
            {
                sub_image_lines(target, 0, 0, 0, width, height, pixformat, pixtype, (const U8*)pixels, width);
            }
            else
            {
                glTexSubImage2D(target, 0, 0, 0, width, height, pixformat, pixtype, pixels);
            }
        }
    }

    alloc_tex_image(width, height, intformat, 1, levels > 1);
    stop_glerror();
}

//create an empty GL texture: just create a texture name
//the texture is assiciate with some image by calling glTexImage outside LLImageGL
bool LLImageGL::createGLTexture()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    checkActiveThread();

    if (gGLManager.mIsDisabled)
    {
        LL_WARNS() << "Trying to create a texture while GL is disabled!" << LL_ENDL;
        return false;
    }

    mGLTextureCreated = false ; //do not save this texture when gl is destroyed.

    llassert(gGLManager.mInited);
    stop_glerror();

    if(mTexName)
    {
        LLImageGL::deleteTextures(1, (reinterpret_cast<GLuint*>(&mTexName))) ;
        mTexName = 0;
    }


    LLImageGL::generateTextures(1, &mTexName);
    stop_glerror();
    if (!mTexName)
    {
        LL_WARNS() << "LLImageGL::createGLTexture failed to make an empty texture" << LL_ENDL;
        return false;
    }

    return true ;
}

bool LLImageGL::createGLTexture(S32 discard_level, const LLImageRaw* imageraw, S32 usename/*=0*/, bool to_create, S32 category, bool defer_copy, LLGLuint* tex_name)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    checkActiveThread();

    if (gGLManager.mIsDisabled)
    {
        LL_WARNS() << "Trying to create a texture while GL is disabled!" << LL_ENDL;
        return false;
    }

    llassert(gGLManager.mInited);
    stop_glerror();

    if (!imageraw || imageraw->isBufferInvalid())
    {
        LL_WARNS() << "Trying to create a texture from invalid image data" << LL_ENDL;
        mGLTextureCreated = false;
        return false;
    }

    if (discard_level < 0)
    {
        llassert(mCurrentDiscardLevel >= 0);
        discard_level = mCurrentDiscardLevel;
    }
    discard_level = llmin(discard_level, MAX_DISCARD_LEVEL);

    // Actual image width/height = raw image width/height * 2^discard_level
    S32 raw_w = imageraw->getWidth() ;
    S32 raw_h = imageraw->getHeight() ;

    S32 w = raw_w << discard_level;
    S32 h = raw_h << discard_level;

    // Everything from setSize onward describes the texture being built, not the one
    // mTexName still names. Off-thread that gap lasts until syncTexName publishes on the
    // main thread, so snapshot what consumers should keep seeing until then.
    if (!on_main_thread())
    {
        beginUpload();
    }

    // setSize may call destroyGLTexture if the size does not match
    if (!setSize(w, h, imageraw->getComponents(), discard_level))
    {
        LL_WARNS() << "Trying to create a texture with incorrect dimensions!" << LL_ENDL;
        mGLTextureCreated = false;
        endUpload(); // nothing will publish; don't leave the getters on a stale snapshot
        return false;
    }

    if (mHasExplicitFormat &&
        ((mFormatPrimary == GL_RGBA && mComponents < 4) ||
         (mFormatPrimary == GL_RGB  && mComponents < 3)))

    {
        LL_WARNS()  << "Incorrect format: " << std::hex << mFormatPrimary << " components: " << (U32)mComponents <<  LL_ENDL;
        mHasExplicitFormat = false;
    }

    if( !mHasExplicitFormat )
    {
        switch (mComponents)
        {
        case 1:
            // Single-channel — used by font glyph maps, but the path
            // is generic for any 1-component upload. setManualImage
            // swizzles LUMINANCE → R8 with a gray-replicate mask on
            // core profile.
            mFormatInternal = GL_LUMINANCE8;
            mFormatPrimary = GL_LUMINANCE;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        case 2:
            // Two-channel (luminance + alpha). Same swizzle remap
            // happens in setManualImage on core profile.
            mFormatInternal = GL_LUMINANCE8_ALPHA8;
            mFormatPrimary = GL_LUMINANCE_ALPHA;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        case 3:
            // sRGB, not linear. The bits are identical either way -- an 8-bit texture
            // uploaded from a JPEG2000/PNG/TGA asset already holds sRGB-encoded values --
            // so this changes nothing about what is stored, only whether GL is willing to
            // decode it. Sampling is unaffected unless a bind asks for the decode with
            // ALSampler::SRGBDecode, and nothing does by default.
            //
            // Worth stating why it cannot be narrowed to colour textures: the same
            // LLViewerFetchedTexture object serves whichever glTF slot references its UUID
            // (see fetch_texture), so one image can be base colour for one material and a
            // normal map for another. The format cannot know; only the bind can. Which is
            // exactly the split the sampler work established.
            mFormatInternal = GL_SRGB8;
            mFormatPrimary = GL_RGB;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        case 4:
            mFormatInternal = GL_SRGB8_ALPHA8;
            mFormatPrimary = GL_RGBA;
            mFormatType = GL_UNSIGNED_BYTE;
            break;
        default:
            LL_ERRS() << "Bad number of components for texture: " << (U32)getComponents() << LL_ENDL;
        }

        // Calc alpha layout first (keys on the deprecated names), then
        // rewrite the format to core-profile-valid forms.
        calcAlphaChannelOffsetAndStride() ;
        resolveDeprecatedFormat();
    }

    if(!to_create) //not create a gl texture
    {
        destroyGLTexture();
        mCurrentDiscardLevel = discard_level;
        mLastBindTime = sLastFrameTime;
        mGLTextureCreated = false;
        endUpload(); // no texture left to disagree with the members
        return true ;
    }

    setCategory(category);
    const U8* rawdata = imageraw->getData();
    return createGLTexture(discard_level, rawdata, false, usename, defer_copy, tex_name);
}

bool LLImageGL::createGLTexture(S32 discard_level, const U8* data_in, bool data_hasmips, S32 usename, bool defer_copy, LLGLuint* tex_name)
// Call with void data, vmem is allocated but unitialized
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LL_PROFILE_GPU_ZONE("createGLTexture");
    checkActiveThread();

    bool main_thread = on_main_thread();

    if (!main_thread)
    {
        // No-op when the imageraw overload above already captured.
        beginUpload();
    }

    if (defer_copy)
    {
        data_in = nullptr;
    }
    else
    {
        llassert(data_in);
    }

    stop_glerror();

    if (discard_level < 0)
    {
        llassert(mCurrentDiscardLevel >= 0);
        discard_level = mCurrentDiscardLevel;
    }
    discard_level = llclamp(discard_level, 0, (S32)mMaxDiscardLevel);
    discard_level = llmin(discard_level, MAX_DISCARD_LEVEL);

    if (main_thread // <--- always force creation of new_texname when not on main thread ...
        && !defer_copy // <--- ... or defer copy is set
        && mTexName != 0 && discard_level == mCurrentDiscardLevel)
    {
        LL_PROFILE_ZONE_NAMED("cglt - early setImage");
        // This will only be true if the size has not changed
        if (tex_name != nullptr)
        {
            *tex_name = mTexName;
        }
        return setImage(data_in, data_hasmips);
    }

    GLuint old_texname = mTexName;
    GLuint new_texname = 0;
    if (usename != 0)
    {
        llassert(main_thread);
        new_texname = usename;
    }
    else
    {
        LLImageGL::generateTextures(1, &new_texname);
        mStorageAllocated = false; // brand-new name, no storage allocated yet
        {
            gGL.getTextureSlot(0)->bind(this, false, false, new_texname);
            glTexParameteri(ALTextureSlot::getInternalType(mBindTarget), GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(ALTextureSlot::getInternalType(mBindTarget), GL_TEXTURE_MAX_LEVEL, mMaxDiscardLevel - discard_level);
            // Apply the swizzle mask once if resolveDeprecatedFormat saved
            // an original format. Per-texture state persists across
            // glTexImage2D / scaleDown reallocations, so we never re-set it.
            if (mDeprecatedSourceFormat != 0)
            {
                applySwizzleForDeprecatedFormat(mBindTarget,
                                                mDeprecatedSourceFormat);
            }
        }
    }

    if (tex_name != nullptr)
    {
        *tex_name = new_texname;
    }

    if (mUseMipMaps)
    {
        mAutoGenMips = true;
    }

    mCurrentDiscardLevel = discard_level;

    {
        LL_PROFILE_ZONE_NAMED("cglt - late setImage");
        if (!setImage(data_in, data_hasmips, new_texname))
        {
            endUpload(); // nothing will publish; don't leave the getters on a stale snapshot
            return false;
        }
    }

    // things will break if we don't unbind after creation
    gGL.getTextureSlot(0)->unbind();

    //if we're on the image loading thread, be sure to delete old_texname and update mTexName on the main thread
    if (!defer_copy)
    {
        if (!main_thread)
        {
            syncToMainThread(new_texname);
        }
        else
        {
            //not on background thread, immediately set mTexName
            if (old_texname != 0 && old_texname != new_texname)
            {
                LLImageGL::deleteTextures(1, &old_texname);
            }
            mTexName = new_texname;
            endUpload();
        }
    }


    mTextureMemory = (S64Bytes)getMipBytes(mCurrentDiscardLevel);

    // mark this as bound at this point, so we don't throw it out immediately
    mLastBindTime = sLastFrameTime;

    checkActiveThread();
    return true;
}

void LLImageGL::syncToMainThread(LLGLuint new_tex_name)
{
    LL_PROFILE_ZONE_SCOPED;
    llassert(!on_main_thread());

    GLsync sync;
    {
        LL_PROFILE_ZONE_NAMED("cglt - sync");
        // No flush before the fence: glFenceSync already orders after every command
        // issued on this context, so the flush below submits those too.
        sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        // REQUIRED, and NOT replaceable by GL_SYNC_FLUSH_COMMANDS_BIT on the waiter:
        // that bit flushes the command stream of whichever context calls
        // glClientWaitSync, which is the main thread's context, not this one. Only a
        // flush here submits the fence, so without it the main thread polls forever.
        glFlush();
    }

    // Block here until the upload has actually completed, then let the main thread swap
    // the name in. This costs this thread's throughput, but publishing mTexName must not
    // outrun the metadata that createGLTexture already wrote (mWidth/mHeight/mComponents
    // via setSize, mCurrentDiscardLevel, the format fields) -- consumers read those
    // through LLGLTexture::getWidth()/getDiscardLevel() and pair them with mTexName.
    // Deferring the name publish past this point widens that mismatch into something
    // sculpt reproducibly trips over (LLVOVolume::sculpt reads back from GL at the new
    // dimensions and gets the old texture).
    //
    // Do NOT be tempted to turn this into a glWaitSync on the main thread: that is a
    // GPU-timeline wait only and gives the main thread's context no CPU-side sync point,
    // so the driver is not obliged to have observed this context's changes to the shared
    // texture by the time the main thread binds it. That was the original implementation
    // and it had to be special-cased for NVIDIA (SL-17284). glClientWaitSync *is* a
    // CPU-side sync point in whichever context calls it, which is the guarantee we need,
    // and it is now taken uniformly on every vendor.
    {
        LL_PROFILE_ZONE_NAMED("cglt - wait sync");
        // One second per iteration so we actually block in the driver rather than
        // spinning. Note FENCE_WAIT_TIME_NANOSECONDS is 1000ns despite its "1 ms"
        // comment, which would busy-wait.
        constexpr U64 WAIT_SLICE_NS = 1000000000ull;
        GLenum res = glClientWaitSync(sync, 0, WAIT_SLICE_NS);
        while (res == GL_TIMEOUT_EXPIRED)
        {
            res = glClientWaitSync(sync, 0, WAIT_SLICE_NS);
        }
        if (res == GL_WAIT_FAILED)
        {
            // Not a valid sync object -- we have no completion guarantee to offer, so
            // say so rather than silently handing over a texture that may not be ready.
            LL_WARNS_ONCE() << "glClientWaitSync failed waiting on a texture upload fence." << LL_ENDL;
        }
        glDeleteSync(sync);
    }

    ref();
    if (!LL::WorkQueue::postMaybe(
            mMainQueue,
            [=, this]()
            {
                LL_PROFILE_ZONE_NAMED("cglt - delete callback");
                syncTexName(new_tex_name);
                unref();
            }))
    {
        // main queue is gone (shutdown); nothing will run the lambda, so don't strand
        // the reference we just took
        unref();
    }

    LL_PROFILER_GPU_COLLECT;
}


// Capture what mTexName currently holds, before createGLTexture starts overwriting the
// members with the geometry of the texture it is about to build. Idempotent, because the
// imageraw overload calls it and then delegates to the data overload which calls it too.
void LLImageGL::beginUpload()
{
    if (mUploadInFlight)
    {
        return;
    }

    mPublished.mWidth               = mWidth;
    mPublished.mHeight              = mHeight;
    mPublished.mComponents          = mComponents;
    mPublished.mCurrentDiscardLevel = mCurrentDiscardLevel;
    mPublished.mMaxDiscardLevel     = mMaxDiscardLevel;
    mUploadInFlight = true;
}

void LLImageGL::syncTexName(LLGLuint texname)
{
    if (texname != 0)
    {
        if (mTexName != 0 && mTexName != texname)
        {
            LLImageGL::deleteTextures(1, &mTexName);
        }
        mTexName = texname;
    }

    // Members and mTexName describe the same texture again.
    endUpload();
}

bool LLImageGL::readBackRaw(S32 discard_level, LLImageRaw* imageraw, bool compressed_ok) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (discard_level < 0)
    {
        discard_level = mCurrentDiscardLevel;
    }

    if (mTexName == 0 || discard_level < mCurrentDiscardLevel || discard_level > mMaxDiscardLevel )
    {
        return false;
    }

    S32 gl_discard = discard_level - mCurrentDiscardLevel;

    //explicitly unbind texture
    gGL.getTextureSlot(0)->unbind();
    // llverify is debug-only; in release a failed bind would have left
    // glGetTexImage reading from whatever the previous bind was, returning
    // bytes for the wrong texture. Check the result and bail.
    if (!gGL.getTextureSlot(0)->bindManual(mBindTarget, mTexName))
    {
        LL_WARNS() << "readBackRaw: bindManual failed for tex " << mTexName << LL_ENDL;
        return false;
    }

    //debug code, leave it there commented.
    //checkTexSize() ;

    LLGLint glwidth = 0;
    glGetTexLevelParameteriv(mTarget, gl_discard, GL_TEXTURE_WIDTH, (GLint*)&glwidth);
    if (glwidth == 0)
    {
        // No mip data smaller than current discard level
        return false;
    }

    S32 width = getWidth(discard_level);
    S32 height = getHeight(discard_level);
    S32 ncomponents = getComponents();
    if (ncomponents == 0)
    {
        return false;
    }
    if(width < glwidth)
    {
        LL_WARNS() << "texture size is smaller than it should be." << LL_ENDL ;
        LL_WARNS() << "width: " << width << " glwidth: " << glwidth << " mWidth: " << mWidth <<
            " mCurrentDiscardLevel: " << (S32)mCurrentDiscardLevel << " discard_level: " << (S32)discard_level << LL_ENDL ;
        return false ;
    }

    if (width <= 0 || width > 2048 || height <= 0 || height > 2048 || ncomponents < 1 || ncomponents > 4)
    {
        LL_ERRS() << llformat("LLImageGL::readBackRaw: bogus params: %d x %d x %d",width,height,ncomponents) << LL_ENDL;
    }

    LLGLint is_compressed = 0;
    if (compressed_ok)
    {
        glGetTexLevelParameteriv(mTarget, gl_discard, GL_TEXTURE_COMPRESSED, (GLint*)&is_compressed);
    }

    //-----------------------------------------------------------------------------------------------
    GLenum error ;
    while((error = glGetError()) != GL_NO_ERROR)
    {
        LL_WARNS() << "GL Error happens before reading back texture. Error code: " << error << LL_ENDL ;
    }
    //-----------------------------------------------------------------------------------------------

    LLImageDataLock lock(imageraw);

    if (is_compressed)
    {
        LLGLint glbytes;
        glGetTexLevelParameteriv(mTarget, gl_discard, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, (GLint*)&glbytes);
        if(!imageraw->allocateDataSize(width, height, ncomponents, glbytes))
        {
            constexpr S64 MAX_GL_BYTES = 2048 * 2048;
            if (glbytes > 0 && glbytes <= MAX_GL_BYTES)
            {
                LLError::LLUserWarningMsg::showOutOfMemory();
                LL_ERRS() << "Memory allocation failed for reading back texture. Data size: " << glbytes << LL_ENDL;
            }
            else
            {
                LL_WARNS() << "Memory allocation failed for reading back texture. Data size is: " << glbytes << LL_ENDL;
                LL_WARNS() << "width: " << width << "height: " << height << "components: " << ncomponents << LL_ENDL;
            }
            return false ;
        }

        glGetCompressedTexImage(mTarget, gl_discard, (GLvoid*)(imageraw->getData()));
        //stop_glerror();
    }
    else
    {
        if(!imageraw->allocateDataSize(width, height, ncomponents))
        {
            constexpr F32 MAX_IMAGE_SIZE = 2048 * 2048;
            F32 size = (F32)width * (F32)height * (F32)ncomponents;
            if (size > 0 && size <= MAX_IMAGE_SIZE)
            {
                LLError::LLUserWarningMsg::showOutOfMemory();
                LL_ERRS() << "Memory allocation failed for reading back texture. Data size: " << size << LL_ENDL;
            }
            else
            {
                LL_WARNS() << "Memory allocation failed for reading back texture." << LL_ENDL;
                LL_WARNS() << "width: " << width << "height: " << height << "components: " << ncomponents << LL_ENDL;
            }
            return false ;
        }

        glGetTexImage(GL_TEXTURE_2D, gl_discard, mFormatPrimary, mFormatType, (GLvoid*)(imageraw->getData()));
        //stop_glerror();
    }

    //-----------------------------------------------------------------------------------------------
    if((error = glGetError()) != GL_NO_ERROR)
    {
        LL_WARNS() << "GL Error happens after reading back texture. Error code: " << error << LL_ENDL ;
        imageraw->deleteData() ;

        while((error = glGetError()) != GL_NO_ERROR)
        {
            LL_WARNS() << "GL Error happens after reading back texture. Error code: " << error << LL_ENDL ;
        }

        return false ;
    }
    //-----------------------------------------------------------------------------------------------

    return true ;
}

void LLImageGL::destroyGLTexture()
{
    checkActiveThread();

    if (mTexName != 0)
    {
        if (mExternalTexture)
        {
            // Caller owns this texture (constructed via the wrap-an-existing-
            // GL-name ctor). We must not glDeleteTextures it. Just forget our
            // reference so callers using destroyGLTexture as a "detach" still
            // see mTexName == 0 afterwards.
            mTexName = 0;
            mCurrentDiscardLevel = -1;
            mGLTextureCreated = false;
            return;
        }

        if(mTextureMemory != S64Bytes(0))
        {
            mTextureMemory = (S64Bytes)0;
        }

        LLImageGL::deleteTextures(1, &mTexName);
        mCurrentDiscardLevel = -1 ; //invalidate mCurrentDiscardLevel.
        mTexName = 0;
        mGLTextureCreated = false ;
    }
}

//force to invalidate the gl texture, most likely a sculpty texture
void LLImageGL::forceToInvalidateGLTexture()
{
    checkActiveThread();
    if (mTexName != 0)
    {
        destroyGLTexture();
    }
    else
    {
        mCurrentDiscardLevel = -1 ; //invalidate mCurrentDiscardLevel.
    }
}

//----------------------------------------------------------------------------

bool LLImageGL::getIsResident(bool test_now)
{
    if (test_now)
    {
        if (mTexName != 0)
        {
            mIsResident = true;
        }
        else
        {
            mIsResident = false;
        }
    }

    return mIsResident;
}

S32 LLImageGL::getHeight(S32 discard_level) const
{
    const S32 base = mUploadInFlight ? mPublished.mHeight : mHeight;
    if (discard_level < 0)
    {
        // mCurrentDiscardLevel can still be -1 if no discard has been set;
        // treat that as "full resolution" rather than shifting by a negative.
        discard_level = llmax<S32>(mUploadInFlight ? mPublished.mCurrentDiscardLevel
                                                   : mCurrentDiscardLevel, 0);
    }
    S32 height = base >> discard_level;
    if (height < 1) height = 1;
    return height;
}

S32 LLImageGL::getWidth(S32 discard_level) const
{
    const S32 base = mUploadInFlight ? mPublished.mWidth : mWidth;
    if (discard_level < 0)
    {
        discard_level = llmax<S32>(mUploadInFlight ? mPublished.mCurrentDiscardLevel
                                                   : mCurrentDiscardLevel, 0);
    }
    S32 width = base >> discard_level;
    if (width < 1) width = 1;
    return width;
}

S64 LLImageGL::getBytes(S32 discard_level) const
{
    if (discard_level < 0)
    {
        discard_level = llmax<S32>(mCurrentDiscardLevel, 0);
    }
    S32 w = mWidth>>discard_level;
    S32 h = mHeight>>discard_level;
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    return dataFormatBytes(mFormatPrimary, w, h);
}

S64 LLImageGL::getMipBytes(S32 discard_level) const
{
    if (discard_level < 0)
    {
        // Match getWidth/getHeight/getBytes: mCurrentDiscardLevel can still
        // be -1 if no discard has been set; treat that as "full resolution"
        // rather than shifting by a negative (which is UB).
        discard_level = llmax<S32>(mCurrentDiscardLevel, 0);
    }
    S32 w = mWidth>>discard_level;
    S32 h = mHeight>>discard_level;
    S64 res = dataFormatBytes(mFormatPrimary, w, h);
    if (mUseMipMaps)
    {
        while (w > 1 && h > 1)
        {
            w >>= 1; if (w == 0) w = 1;
            h >>= 1; if (h == 0) h = 1;
            res += dataFormatBytes(mFormatPrimary, w, h);
        }
    }
    return res;
}

bool LLImageGL::isJustBound() const
{
    return sLastFrameTime - mLastBindTime < 0.5f;
}

bool LLImageGL::getBoundRecently() const
{
    return (bool)(sLastFrameTime - mLastBindTime < MIN_TEXTURE_LIFETIME);
}

bool LLImageGL::getIsAlphaMask() const
{
    llassert_always(!sSkipAnalyzeAlpha);
    return mIsMask;
}

void LLImageGL::setTarget(const LLGLenum target, const ALTextureSlot::eTextureType bind_target)
{
    mTarget = target;
    mBindTarget = bind_target;
}

const S8 INVALID_OFFSET = -99 ;
void LLImageGL::setNeedsAlphaAndPickMask(bool need_mask)
{
    if(mNeedsAlphaAndPickMask != need_mask)
    {
        mNeedsAlphaAndPickMask = need_mask;

        if(mNeedsAlphaAndPickMask)
        {
            mAlphaOffset = 0 ;
        }
        else //do not need alpha mask
        {
            mAlphaOffset = INVALID_OFFSET ;
            mIsMask = false;
        }
    }
}

void LLImageGL::calcAlphaChannelOffsetAndStride()
{
    if(mAlphaOffset == INVALID_OFFSET)//do not need alpha mask
    {
        return ;
    }

    mAlphaStride = -1 ;
    switch (mFormatPrimary)
    {
    case GL_LUMINANCE:
    case GL_ALPHA:
        mAlphaStride = 1;
        break;
    case GL_LUMINANCE_ALPHA:
        mAlphaStride = 2;
        break;
    case GL_RED:
    case GL_RGB:
    case GL_SRGB:
        mNeedsAlphaAndPickMask = false;
        mIsMask = false;
        return; //no alpha channel.
    case GL_RGBA:
    case GL_SRGB_ALPHA:
        mAlphaStride = 4;
        break;
    case GL_BGRA:
        mAlphaStride = 4;
        break;
    default:
        break;
    }

    mAlphaOffset = -1 ;
    if (mFormatType == GL_UNSIGNED_BYTE)
    {
        mAlphaOffset = mAlphaStride - 1 ;
    }
    else if(is_little_endian())
    {
        if (mFormatType == GL_UNSIGNED_INT_8_8_8_8)
        {
            mAlphaOffset = 0 ;
        }
        else if (mFormatType == GL_UNSIGNED_INT_8_8_8_8_REV)
        {
            mAlphaOffset = 3 ;
        }
    }
    else //big endian
    {
        if (mFormatType == GL_UNSIGNED_INT_8_8_8_8)
        {
            mAlphaOffset = 3 ;
        }
        else if (mFormatType == GL_UNSIGNED_INT_8_8_8_8_REV)
        {
            mAlphaOffset = 0 ;
        }
    }

    if( mAlphaStride < 1 || //unsupported format
        mAlphaOffset < 0 || //unsupported type
        (mFormatPrimary == GL_BGRA && mFormatType != GL_UNSIGNED_BYTE)) //unknown situation
    {
        LL_WARNS() << "Cannot analyze alpha for image with format type " << std::hex << mFormatType << std::dec << LL_ENDL;

        mNeedsAlphaAndPickMask = false ;
        mIsMask = false;
    }
}

// static
void LLImageGL::applySwizzleForDeprecatedFormat(ALTextureSlot::eTextureType type, U32 original_format)
{
    // Swizzle table is owned here; callers (LLImageGL::createGLTexture for
    // resolved instances, llvoavatar's morph-mask upload for raw GL textures)
    // identify the format they originally asked for and let this routine
    // pick the matching mask. Keeps GL_TEXTURE_SWIZZLE_RGBA and the
    // mask-component constants out of the call sites.
    LLGLint mask[4];
    switch (original_format)
    {
    case GL_ALPHA:
        // Original meaning: byte goes to alpha, (R,G,B) read as (0,0,0).
        mask[0] = GL_ZERO; mask[1] = GL_ZERO; mask[2] = GL_ZERO; mask[3] = GL_RED;
        break;
    case GL_LUMINANCE:
        // Original meaning: byte replicated to RGB, alpha = 1.
        mask[0] = GL_RED;  mask[1] = GL_RED;  mask[2] = GL_RED;  mask[3] = GL_ONE;
        break;
    case GL_LUMINANCE_ALPHA:
        // Original meaning: first byte replicated to RGB, second byte → alpha.
        mask[0] = GL_RED;  mask[1] = GL_RED;  mask[2] = GL_RED;  mask[3] = GL_GREEN;
        break;
    default:
        return;  // not a format we re-express via swizzle
    }
    glTexParameteriv(ALTextureSlot::getInternalType(type), GL_TEXTURE_SWIZZLE_RGBA, mask);
}

void LLImageGL::resolveDeprecatedFormat()
{
    // setManualImage rewrites the deprecated source/internal formats locally
    // (so glTexImage2D allocates the right backing storage), but its
    // rewrites don't propagate to LLImageGL's mFormatPrimary / mFormatInternal
    // members. Subsequent setSubImage / readBackRaw / scaleDown that read
    // those members would hand the deprecated enums to GL — invalid in core
    // profile, divergent from the actual GL texture state on compat. Rewrite
    // here at format-resolution time so members stay consistent with what
    // the texture will hold, and remember the original format so
    // createGLTexture can apply the matching swizzle once via
    // applySwizzleForDeprecatedFormat.
    //
    switch (mFormatPrimary)
    {
    case GL_ALPHA:
        mDeprecatedSourceFormat = mFormatPrimary;
        mFormatPrimary = GL_RED;
        mFormatInternal = GL_R8;
        break;
    case GL_LUMINANCE:
        mDeprecatedSourceFormat = mFormatPrimary;
        mFormatPrimary = GL_RED;
        mFormatInternal = GL_R8;
        break;
    case GL_LUMINANCE_ALPHA:
        mDeprecatedSourceFormat = mFormatPrimary;
        mFormatPrimary = GL_RG;
        mFormatInternal = GL_RG8;
        break;
    default:
        // Reset so a stale value from a prior format doesn't make
        // createGLTexture apply the wrong swizzle on a fresh non-deprecated
        // format (e.g. setExplicitFormat(LUMINANCE) followed by
        // setExplicitFormat(RGBA), or an LLImageGL whose component count
        // changed across createGLTexture calls).
        mDeprecatedSourceFormat = 0;
        break;
    }
}

void LLImageGL::analyzeAlpha(const void* data_in, U32 w, U32 h)
{
    if(!data_in || sSkipAnalyzeAlpha || !mNeedsAlphaAndPickMask)
    {
        return ;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    U32 length = w * h;
    U32 alphatotal = 0;

    U32 sample[16];
    memset(sample, 0, sizeof(U32)*16);

    // generate histogram of quantized alpha.
    // also add-in the histogram of a 2x2 box-sampled version.  The idea is
    // this will mid-skew the data (and thus increase the chances of not
    // being used as a mask) from high-frequency alpha maps which
    // suffer the worst from aliasing when used as alpha masks.
    if (w >= 2 && h >= 2)
    {
        // Walk only complete 2x2 quads. Odd dimensions previously hit
        // asserts in debug and read OOB on the trailing row/column in
        // release (`current[w * mAlphaStride]` indexes one row down,
        // which doesn't exist for the last odd row). Trailing odd row
        // and column are dropped from the histogram; the remaining
        // sample is still representative for the mask classifier.
        const U32 paired_w = w & ~1u;
        const U32 paired_h = h & ~1u;
        const GLubyte* rowstart = ((const GLubyte*) data_in) + mAlphaOffset;
        for (U32 y = 0; y < paired_h; y += 2)
        {
            const GLubyte* current = rowstart;
            for (U32 x = 0; x < paired_w; x += 2)
            {
                const U32 s1 = current[0];
                alphatotal += s1;
                const U32 s2 = current[w * mAlphaStride];
                alphatotal += s2;
                current += mAlphaStride;
                const U32 s3 = current[0];
                alphatotal += s3;
                const U32 s4 = current[w * mAlphaStride];
                alphatotal += s4;
                current += mAlphaStride;

                ++sample[s1/16];
                ++sample[s2/16];
                ++sample[s3/16];
                ++sample[s4/16];

                const U32 asum = (s1+s2+s3+s4);
                alphatotal += asum;
                sample[asum/(16*4)] += 4;
            }

            rowstart += 2 * w * mAlphaStride;
        }
        length *= 2; // we sampled everything twice, essentially
    }
    else
    {
        const GLubyte* current = ((const GLubyte*) data_in) + mAlphaOffset;
        for (U32 i = 0; i < length; i++)
        {
            const U32 s1 = *current;
            alphatotal += s1;
            ++sample[s1/16];
            current += mAlphaStride;
        }
    }

    // if more than 1/16th of alpha samples are mid-range, this
    // shouldn't be treated as a 1-bit mask

    // also, if all of the alpha samples are clumped on one half
    // of the range (but not at an absolute extreme), then consider
    // this to be an intentional effect and don't treat as a mask.

    U32 midrangetotal = 0;
    for (U32 i = 2; i < 13; i++)
    {
        midrangetotal += sample[i];
    }
    U32 lowerhalftotal = 0;
    for (U32 i = 0; i < 8; i++)
    {
        lowerhalftotal += sample[i];
    }
    U32 upperhalftotal = 0;
    for (U32 i = 8; i < 16; i++)
    {
        upperhalftotal += sample[i];
    }

    if (midrangetotal > length/48 || // lots of midrange, or
        (lowerhalftotal == length && alphatotal != 0) || // all close to transparent but not all totally transparent, or
        (upperhalftotal == length && alphatotal != 255*length)) // all close to opaque but not all totally opaque
    {
        mIsMask = false; // not suitable for masking
    }
    else
    {
        mIsMask = true;
    }
}

//----------------------------------------------------------------------------
U32 LLImageGL::createPickMask(S32 pWidth, S32 pHeight)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    freePickMask();
    // updatePickMask walks the source with `for (x = 0; x < width; x += 2)`,
    // so the actual cells-per-row stored linearly in the bitmap is
    // ceil(width/2). The reader must use the same stride, otherwise odd
    // widths read from the wrong row.
    U32 stride_w = (U32)((pWidth + 1) / 2);
    U32 stride_h = (U32)((pHeight + 1) / 2);

    U32 size = stride_w * stride_h;
    size = (size + 7) / 8; // pixelcount-to-bits
    mPickMask = new U8[size];
    mPickMaskWidth = stride_w;
    mPickMaskHeight = stride_h;

    memset(mPickMask, 0, sizeof(U8) * size);

    return size;
}

//----------------------------------------------------------------------------
void LLImageGL::freePickMask()
{
    if (mPickMask != NULL)
    {
        delete [] mPickMask;
    }
    mPickMask = NULL;
    mPickMaskWidth = mPickMaskHeight = 0;
}

bool LLImageGL::isCompressed() const
{
    llassert(mFormatPrimary != 0);
    // *NOTE: Not all compressed formats are included here.
    bool is_compressed = false;
    switch (mFormatPrimary)
    {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        is_compressed = true;
        break;
    default:
        break;
    }
    return is_compressed;
}

//----------------------------------------------------------------------------
void LLImageGL::updatePickMask(S32 width, S32 height, const U8* data_in)
{
    if(!mNeedsAlphaAndPickMask)
    {
        return ;
    }

    if (mFormatType != GL_UNSIGNED_BYTE ||
        ((mFormatPrimary != GL_RGBA)
      && (mFormatPrimary != GL_SRGB_ALPHA)))
    {
        //cannot generate a pick mask for this texture
        freePickMask();
        return;
    }


#ifdef SHOW_ASSERT
    const U32 pickSize = createPickMask(width, height);
#else // SHOW_ASSERT
    createPickMask(width, height);
#endif // SHOW_ASSERT

    U32 pick_bit = 0;

    for (S32 y = 0; y < height; y += 2)
    {
        for (S32 x = 0; x < width; x += 2)
        {
            U8 alpha = data_in[(y*width+x)*4+3];

            if (alpha > 32)
            {
                U32 pick_idx = pick_bit/8;
                U32 pick_offset = pick_bit%8;
                llassert(pick_idx < pickSize);

                mPickMask[pick_idx] |= 1 << pick_offset;
            }

            ++pick_bit;
        }
    }
}

//bool LLImageGL::getMask(const LLVector2 &tc)
// [RLVa:KB] - Checked: RLVa-2.2 (@setoverlay)
bool LLImageGL::getMask(const LLVector2 &tc) const
// [/RLVa:KB]
{
    bool res = true;

    if (mPickMask)
    {
        F32 u,v;
        if (LL_LIKELY(tc.isFinite()))
        {
            u = tc.mV[0] - floorf(tc.mV[0]);
            v = tc.mV[1] - floorf(tc.mV[1]);
        }
        else
        {
            LL_WARNS_ONCE("render") << "Ugh, non-finite u/v in mask pick" << LL_ENDL;
            u = v = 0.f;
            // removing assert per EXT-4388
            // llassert(false);
        }

        if (LL_UNLIKELY(u < 0.f || u > 1.f ||
                v < 0.f || v > 1.f))
        {
            LL_WARNS_ONCE("render") << "Ugh, u/v out of range in image mask pick" << LL_ENDL;
            u = v = 0.f;
            // removing assert per EXT-4388
            // llassert(false);
        }

        S32 x = llfloor(u * mPickMaskWidth);
        S32 y = llfloor(v * mPickMaskHeight);

        // Defensive clamp to the last valid index. The previous version
        // tested `> mPickMaskWidth` and clamped to `mPickMaskWidth` itself,
        // which is one past the valid range and addresses the start of the
        // next row's bits. Use `>=` and clamp to width-1 / height-1.
        if (LL_UNLIKELY(x >= mPickMaskWidth))
        {
            LL_WARNS_ONCE("render") << "Ooh, width overrun on pick mask read, that coulda been bad." << LL_ENDL;
            x = mPickMaskWidth > 0 ? mPickMaskWidth - 1 : 0;
        }
        if (LL_UNLIKELY(y >= mPickMaskHeight))
        {
            LL_WARNS_ONCE("render") << "Ooh, height overrun on pick mask read, that woulda been bad." << LL_ENDL;
            y = mPickMaskHeight > 0 ? mPickMaskHeight - 1 : 0;
        }

        S32 idx = y*mPickMaskWidth+x;
        S32 offset = idx%8;

        res = (mPickMask[idx/8] & (1 << offset)) != 0;
    }

    return res;
}

void LLImageGL::setCurTexSizebar(S32 index, bool set_pick_size)
{
    sCurTexSizeBar = index ;

    if(set_pick_size)
    {
        sCurTexPickSize = (1 << index) ;
    }
    else
    {
        sCurTexPickSize = -1 ;
    }
}
void LLImageGL::resetCurTexSizebar()
{
    sCurTexSizeBar = -1 ;
    sCurTexPickSize = -1 ;
}

// Allocate backing storage for the currently-bound texture at the given level-0 size,
// and record the VRAM accounting for it. Callers replacing an existing texture are
// responsible for releasing the old accounting (free_tex_image) themselves.
//
// Single place so the upcoming switch to immutable storage (glTexStorage2D, which also
// needs mMipLevels and a sized internal format) lands in one spot rather than at every
// allocation site.
// The internal format glTexStorage* should be handed. Block-compressed textures carry
// their (sized) compressed format in mFormatPrimary rather than mFormatInternal, which is
// the convention the glCompressedTexImage2D calls already follow.
S32 LLImageGL::getStorageInternalFormat() const
{
    return isCompressed() ? mFormatPrimary : mFormatInternal;
}

void LLImageGL::allocateTextureStorage(S32 width, S32 height, bool has_mips)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    mMipLevels = has_mips ? calcMipLevelCount(width, height) : 1;

    assertStorageAllocatable(mTarget, getStorageInternalFormat());

    {
        LL_PROFILE_ZONE_NAMED("glTexStorage2D");
        // Dimensions, format and level count are fixed for the lifetime of this texture
        // object. Every later write must be a sub-image, and any change of size or format
        // must create a new texture -- see scaleDown and createGLTexture.
        glTexStorage2D(mTarget, mMipLevels, getStorageInternalFormat(), width, height);
        skipSRGBDecode(mTarget);
        mStorageAllocated = true;
    }

    alloc_tex_image(width, height, mFormatInternal, 1, has_mips);
}

// static
void LLImageGL::generateMipmaps(U32 target)
{
    // See the header comment: clearing the sampler makes the texture object's own
    // SKIP_DECODE (written at allocation) govern the mip filter's colour space, instead of
    // whatever sampler the last draw left on the slot. bindSampler dedups, so this is free
    // when the slot is already sampler-less.
    gGL.getTextureSlot(0)->bindSampler(0);
    glGenerateMipmap(target);
}

bool LLImageGL::scaleDown(S32 desired_discard)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (mTarget != GL_TEXTURE_2D
        || mFormatInternal == -1 // not initialized
        || isCompressed()        // neither path can work on block-compressed data:
                                 // the FBO path cannot render into it and the PBO path
                                 // cannot re-upload it as loose pixels
        )
    {
        return false;
    }

    desired_discard = llmin(desired_discard, mMaxDiscardLevel);

    if (desired_discard <= mCurrentDiscardLevel)
    {
        return false;
    }

    S32 mip = desired_discard - mCurrentDiscardLevel;

    S32 desired_width = getWidth(desired_discard);
    S32 desired_height = getHeight(desired_discard);

    // Downscale into a NEW texture object rather than reallocating this one in place.
    // glTexImage2D on the live name is illegal once the texture has immutable storage
    // (GL_INVALID_OPERATION, silently leaving it at the old size), and it has no D3D11
    // equivalent either -- there a resource's dimensions are fixed at creation. Both
    // paths below therefore build the smaller texture separately and swap it in.
    const LLGLuint old_texname = mTexName;
    LLGLuint new_texname = 0;
    generateTextures(1, &new_texname);
    mStorageAllocated = false; // brand-new name; allocateTextureStorage sets this
    if (new_texname == 0)
    {
        LL_WARNS_ONCE("LLImageGL") << "Failed to allocate a texture name for downscaling." << LL_ENDL;
        return false;
    }

    if (gGLManager.mDownScaleMethod == 0)
    { // use an FBO to downscale the texture
        glViewport(0, 0, desired_width, desired_height);

        // draw a full screen triangle, sampling the old (larger) texture
        if (gGL.getTextureSlot(0)->bind(this, true, true))
        {
            // bind() leaves the slot's sampler alone, so the downscale draw would sample
            // through whatever the last draw bound. That used to only wobble the filter;
            // with sRGB storage a stray decode sampler would render LINEAR values into the
            // copy, which then reads back as sRGB. Pin it: trilinear to pull from the
            // source's own mip chain, clamp, and no decode.
            gGL.getTextureSlot(0)->bindSampler(gGL.getSampler(ALSamplers::TrilinearClamp));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            // Bind the new name and give it storage, then copy the rendered result out
            // of the framebuffer into it. glCopyTexSubImage2D is a sub-image write, so
            // it is legal on immutable storage. Sampler 0, NOT mHasMipMaps -- the third
            // parameter selects a sampler object now, and glGenerateMipmap below must run
            // under the texture's own SKIP_DECODE state, not a leftover sampler's.
            gGL.getTextureSlot(0)->bindManual(mBindTarget, new_texname);
            allocateTextureStorage(desired_width, desired_height, mHasMipMaps);
            glCopyTexSubImage2D(mTarget, 0, 0, 0, 0, 0, desired_width, desired_height);

            if (mHasMipMaps)
            { // generate mipmaps if needed
                LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("scaleDown - glGenerateMipmap");
                generateMipmaps(mTarget);
            }

            gGL.getTextureSlot(0)->unbind();
        }
        else
        {
            LL_WARNS_ONCE("LLImageGL") << "Failed to bind texture for downscaling." << LL_ENDL;
            deleteTextures(1, &new_texname);
            return false;
        }
    }
    else
    { // use a PBO to downscale the texture
        U64 size = getBytes(desired_discard);
        llassert(size <= 2048 * 2048 * 4); // we shouldn't be using this method to downscale huge textures, but it'll work
        gGL.getTextureSlot(0)->bind(this, false, true);

        if (sScratchPBO == 0)
        {
            glGenBuffers(1, &sScratchPBO);
            sScratchPBOSize = 0;
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, sScratchPBO);

        if (size > sScratchPBOSize)
        {
            glBufferData(GL_PIXEL_PACK_BUFFER, size, NULL, GL_STREAM_COPY);
            sScratchPBOSize = (U32)size;
        }

        // read the desired mip out of the old texture into the PBO
        glGetTexImage(mTarget, mip, mFormatPrimary, mFormatType, nullptr);

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        // ...and back out of the PBO into the new one. Sampler 0, NOT mHasMipMaps -- the
        // third parameter selects a sampler object now, and glGenerateMipmap below must
        // run under the texture's own SKIP_DECODE state, not a leftover sampler's.
        gGL.getTextureSlot(0)->bindManual(mBindTarget, new_texname);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, sScratchPBO);
        allocateTextureStorage(desired_width, desired_height, mHasMipMaps);
        glTexSubImage2D(mTarget, 0, 0, 0, desired_width, desired_height, mFormatPrimary, mFormatType, nullptr);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        if (mHasMipMaps)
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("scaleDown - glGenerateMipmap");
            generateMipmaps(mTarget);
        }

        gGL.getTextureSlot(0)->unbind();
    }

    // Retire the old texture and publish the new one. Accounting for the new allocation
    // is done by allocateTextureStorage.
    free_tex_image(old_texname);
    deleteTextures(1, &old_texname);
    mTexName = new_texname;

    mCurrentDiscardLevel = desired_discard;

    return true;
}


//----------------------------------------------------------------------------
#if LL_IMAGEGL_THREAD_CHECK
void LLImageGL::checkActiveThread()
{
    llassert(mActiveThread == LLThread::currentID());
}
#endif

//----------------------------------------------------------------------------


LLImageGLThread::LLImageGLThread(LLWindow* window)
    // We want exactly one thread.
    : LL::ThreadPool("LLImageGL", 1)
    , mWindow(window)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    mFinished = false;

    mContext = mWindow->createSharedContext();
    LL::ThreadPool::start();
}

void LLImageGLThread::run()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    // We must perform setup on this thread before actually servicing our
    // WorkQueue, likewise cleanup afterwards.
    mWindow->makeContextCurrent(mContext);
    gGL.init(false);
    LL::ThreadPool::run();
    gGL.shutdown();
    mWindow->destroySharedContext(mContext);
}

