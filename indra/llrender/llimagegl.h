/**
 * @file llimagegl.h
 * @brief Object for managing images and their textures
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


#ifndef LL_LLIMAGEGL_H
#define LL_LLIMAGEGL_H

#include "llimage.h"

#include "llgltypes.h"
#include "llpointer.h"
#include "llrefcount.h"
#include "v2math.h"
#include "llunits.h"
#include "llthreadsafequeue.h"
#include "llrender.h"
#include "threadpool.h"
#include "workqueue.h"
#include <boost/unordered_set.hpp>

#define LL_IMAGEGL_THREAD_CHECK 0 //set to 1 to enable thread debugging for ImageGL

class LLWindow;

#define BYTES_TO_MEGA_BYTES(x) ((x) >> 20)
#define MEGA_BYTES_TO_BYTES(x) ((x) << 20)

namespace LLImageGLMemory
{
    // width/height are the level-0 dimensions. If has_mips is true,
    // the recorded size includes the full mip pyramid down to 1x1
    // (driver-padded via dataFormatVRAMBytes per level). count covers
    // array slices / cube faces, each of which gets its own pyramid.
    void alloc_tex_image(U32 width, U32 height, U32 intformat, U32 count, bool has_mips = false);
    void free_tex_image(U32 texName);
    void free_tex_images(U32 count, const U32* texNames);
    void free_cur_tex_image();
}

//============================================================================
// NOTE: thread-safe refcount rather than plain LLRefCount because syncToMainThread()
// takes a reference on the LLImageGL thread which is released on the main thread.
// Refcount traffic here is per-texture-lifetime (LLImageGL lives in member slots like
// LLGLTexture::mGLTexturep, it isn't copied through hot loops), so the atomic is not on
// any measured path.
class LLImageGL : public LLThreadSafeRefCount
{
    friend class ALTextureSlot;
public:

    // call once per frame
    static void updateClass();

    // Get an estimate of how many bytes have been allocated in vram for
    // textures. Allocations recorded via alloc_tex_image with has_mips=true
    // include the full mip pyramid (driver-padded via dataFormatVRAMBytes);
    // single-level allocations cover level 0 only.
    //
    // NOTE: viewer consumers (llviewertexture.cpp, lltextureview.cpp)
    // historically scaled this by 2x via a /512 divisor instead of /1024
    // when converting to MB. That fudge predates per-mip accounting and
    // also compensates for driver-side overhead (descriptor structs, page
    // alignment). Leave it alone until the budget code is reconciled
    // holistically.
    static U64 getTextureBytesAllocated();

    // These 2 functions replace glGenTextures() and glDeleteTextures()
    static void generateTextures(S32 numTextures, U32 *textures);
    static void deleteTextures(S32 numTextures, const U32 *textures);

    // Size calculation
    //
    // dataFormatBits/Bytes report the *tight host-side* layout — i.e. the
    // packed CPU buffer size for glTexImage2D/glTexSubImage2D uploads
    // (matching the pixel format). Use these for source-pointer
    // arithmetic and host-side accounting (getBytes/getMipBytes).
    //
    // dataFormatVRAMBits/Bytes report the *driver-side* allocation for
    // VRAM accounting. They differ from the host values for formats that
    // drivers pad (RGB8 -> RGBX, RGB16F -> RGBA16F, RGB32F -> RGBA32F,
    // DEPTH_COMPONENT24 -> 32-bit). For other formats they delegate to
    // dataFormatBits/Bytes.
    static S32 dataFormatBits(S32 dataformat);
    static S64 dataFormatBytes(S32 dataformat, S32 width, S32 height);
    static S32 dataFormatVRAMBits(S32 dataformat);
    static S64 dataFormatVRAMBytes(S32 dataformat, S32 width, S32 height);
    static S32 dataFormatComponents(S32 dataformat);

    bool updateBindStats() const ;
    F32 getTimePassedSinceLastBound();
    void forceUpdateBindStats(void) const;

    // needs to be called every frame
    static void updateStats(F32 current_time);

    // cleanup GL state
    static void destroyGL();

    static bool checkSize(S32 width, S32 height);

    // Number of mip levels in a full pyramid for these level-0 dimensions, counting
    // level 0 itself: 256x256 -> 9. This is a COUNT, which is what glTexStorage* takes.
    // Each dimension clamps at 1 independently, so 1024x16 is 11 levels, not 5.
    static S32 calcMipLevelCount(S32 width, S32 height);

    //for server side use only.
    // Not currently necessary for LLImageGL, but required in some derived classes,
    // so include for compatability
    static bool create(LLPointer<LLImageGL>& dest, bool usemipmaps = true);
    static bool create(LLPointer<LLImageGL>& dest, U32 width, U32 height, U8 components, bool usemipmaps = true);
    static bool create(LLPointer<LLImageGL>& dest, const LLImageRaw* imageraw, bool usemipmaps = true);

public:
    LLImageGL(bool usemipmaps = true);
    LLImageGL(U32 width, U32 height, U8 components, bool usemipmaps = true);
    LLImageGL(const LLImageRaw* imageraw, bool usemipmaps = true);

    // For wrapping textures created via GL elsewhere with our API only. Use with caution.
    // The trailing address mode is accepted and ignored: sampling is named at the bind now.
    // Kept in the signature so the several call sites that pass one still compile; drop it
    // when they are cleaned up.
    // Wrap a texture object this LLImageGL does not own. No address-mode parameter: it took
    // one, the body never read it, and callers were still passing sampling state to a class
    // that stopped carrying any.
    LLImageGL(LLGLuint mTexName, U32 components, LLGLenum target, LLGLint  formatInternal, LLGLenum formatPrimary, LLGLenum formatType);

protected:
    virtual ~LLImageGL();

    void analyzeAlpha(const void* data_in, U32 w, U32 h);
    void calcAlphaChannelOffsetAndStride();

    // Rewrite mFormatPrimary/mFormatInternal from deprecated forms
    // (GL_LUMINANCE / GL_ALPHA / GL_LUMINANCE_ALPHA) to their core-profile-
    // valid equivalents (GL_RED + R8 with a {R,R,R,1} swizzle, etc.). Records
    // the swizzle mask so the next createGLTexture can apply it once. No-op
    // for non-deprecated formats.
    //
    // Must run AFTER calcAlphaChannelOffsetAndStride: the alpha layout
    // calc keys on the deprecated names (GL_LUMINANCE_ALPHA stride=2 etc).
    // After rewrite, mAlphaStride/mAlphaOffset are still correct for the
    // physical data layout (1 byte per channel either way), and subsequent
    // setSubImage / readBackRaw / scaleDown read the live format names.
    void resolveDeprecatedFormat();

public:
    virtual void dump();    // debugging info to LL_INFOS()

    bool setSize(S32 width, S32 height, S32 ncomponents, S32 discard_level = -1);
    void setComponents(S32 ncomponents) { mComponents = (S8)ncomponents ;}

    // Allocate a 2D texture on the currently-bound name and upload level 0. This owns the
    // whole texture: call it exactly once per texture object, and build a new texture
    // rather than resizing or reformatting this one -- that is what glTexStorage2D
    // requires, and what D3D11/12 and Vulkan require of every resource. pixels may be null
    // to allocate only. levels is the mip level COUNT; storage is allocated for all of
    // them, so levels 1+ can be filled with setManualSubImage.
    static void allocateTexture2D(U32 target, S32 intformat, S32 width, S32 height,
                                  U32 pixformat, U32 pixtype, const void *pixels,
                                  S32 levels = 1);

    // Upload one level into a texture that already has storage. Allocates nothing and does
    // no VRAM accounting, which is what makes it legal on immutable storage. The caller
    // must have allocated with a compatible format and size.
    static void setManualSubImage(U32 target, S32 miplevel, S32 width, S32 height, U32 pixformat, U32 pixtype, const void *pixels);

    // Generate the mip chain of the texture bound on SLOT 0, under the texture's own
    // sRGB-decode state. Always use this instead of a bare glGenerateMipmap: mip generation
    // follows the slot's TEXTURE_SRGB_DECODE -- sampler first if one is bound, else the
    // texture's (EXT_texture_sRGB_decode issue 10) -- so a bare call mips sRGB-stored data
    // raw or decoded depending on whatever sampler the last draw left behind. This clears
    // the slot's sampler first, making the texture's SKIP_DECODE (written at allocation)
    // govern deterministically.
    static void generateMipmaps(U32 target);

    // Apply the GL_TEXTURE_SWIZZLE_RGBA mask that re-expresses a deprecated
    // source format on the currently-bound texture as samplable RGBA.
    // `original_format` is one of GL_ALPHA, GL_LUMINANCE, GL_LUMINANCE_ALPHA;
    // anything else is a no-op. Centralizes the swizzle table so callers
    // outside llrender don't need to know the GL_RED/GL_GREEN/GL_ZERO/GL_ONE
    // mask layout (or even that GL_TEXTURE_SWIZZLE_RGBA exists).
    //
    // For LLImageGL instances, createGLTexture calls this internally based
    // on the format resolved by setExplicitFormat / the auto-format switch;
    // callers uploading into a raw GL texture name (e.g. llvoavatar's
    // morph-mask upload) call this themselves before/after binding.
    static void applySwizzleForDeprecatedFormat(ALTextureSlot::eTextureType type, U32 original_format);

    bool createGLTexture() ;
    bool createGLTexture(S32 discard_level, const LLImageRaw* imageraw, S32 usename = 0, bool to_create = true,
        S32 category = sMaxCategories-1, bool defer_copy = false, LLGLuint* tex_name = nullptr);
    bool createGLTexture(S32 discard_level, const U8* data, bool data_hasmips = false, S32 usename = 0, bool defer_copy = false, LLGLuint* tex_name = nullptr);
    void setImage(const LLImageRaw* imageraw);
    bool setImage(const U8* data_in, bool data_hasmips = false, S32 usename = 0);
    // *TODO: This function may not work on block-compressed textures (i.e. those
    // uploaded already compressed, where isCompressed() is true). Partial image
    // updates do not work on compressed textures.
    bool setSubImage(const LLImageRaw* imageraw, S32 x_pos, S32 y_pos, S32 width, S32 height, bool force_fast_update = false, LLGLuint use_name = 0, bool skip_unbind = false);
    bool setSubImage(const U8* datap, S32 data_width, S32 data_height, S32 x_pos, S32 y_pos, S32 width, S32 height, bool force_fast_update = false, LLGLuint use_name = 0, bool skip_unbind = false);
    bool setSubImageFromFrameBuffer(S32 fb_x, S32 fb_y, S32 x_pos, S32 y_pos, S32 width, S32 height);

    // wait for gl commands to finish on current thread and push
    // a lambda to main thread to swap mNewTexName and mTexName
    void syncToMainThread(LLGLuint new_tex_name);

    // Read back a raw image for this discard level, if it exists
    bool readBackRaw(S32 discard_level, LLImageRaw* imageraw, bool compressed_ok) const;
    void destroyGLTexture();
    void forceToInvalidateGLTexture();

    void setExplicitFormat(LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format = 0, bool swap_bytes = false);
    void setComponents(S8 ncomponents) { mComponents = ncomponents; }

    // While an off-thread upload is in flight the members below describe the texture
    // being built, not the one mTexName still names. Consumers pair these getters with
    // getTexName() -- LLVOVolume::sculpt reads back from GL at getWidth() -- so they must
    // report what mTexName actually holds until both flip together in syncTexName.
    S32  getDiscardLevel() const        { return mUploadInFlight ? mPublished.mCurrentDiscardLevel : mCurrentDiscardLevel; }
    S32  getMaxDiscardLevel() const     { return mUploadInFlight ? mPublished.mMaxDiscardLevel : mMaxDiscardLevel; }

    // override the current discard level
    // should only be used for local textures where you know exactly what you're doing
    void setDiscardLevel(S32 level) { mCurrentDiscardLevel = level; }

    S32  getCurrentWidth() const { return mWidth ;}
    S32  getCurrentHeight() const { return mHeight ;}
    S32  getWidth(S32 discard_level = -1) const;
    S32  getHeight(S32 discard_level = -1) const;
    U8   getComponents() const { return (U8)(mUploadInFlight ? mPublished.mComponents : mComponents); }
    S64  getBytes(S32 discard_level = -1) const;
    S64  getMipBytes(S32 discard_level = -1) const;
    bool getBoundRecently() const;
    bool isJustBound() const;
    bool getHasExplicitFormat() const { return mHasExplicitFormat; }
    LLGLenum getPrimaryFormat() const { return mFormatPrimary; }
    LLGLenum getFormatType() const { return mFormatType; }

    // Internal format to hand glTexStorage*. Block-compressed textures keep their sized
    // compressed format in mFormatPrimary rather than mFormatInternal.
    S32 getStorageInternalFormat() const;

    bool getHasGLTexture() const { return mTexName != 0; }
    LLGLuint getTexName() const { return mTexName; }

    bool getIsAlphaMask() const;

    bool getIsResident(bool test_now = false); // not const

    void setTarget(const LLGLenum target, const ALTextureSlot::eTextureType bind_target);

    ALTextureSlot::eTextureType getTarget(void) const { return mBindTarget; }
    bool isGLTextureCreated(void) const { return mGLTextureCreated ; }
    void setGLTextureCreated (bool initialized) { mGLTextureCreated = initialized; }

    bool getUseMipMaps() const { return mUseMipMaps; }
    void setUseMipMaps(bool usemips) { mUseMipMaps = usemips; }
    void setHasMipMaps(bool hasmips) { mHasMipMaps = hasmips; }
    void updatePickMask(S32 width, S32 height, const U8* data_in);
// [RLVa:KB] - Checked: RLVa-2.2 (@setoverlay)
    bool getMask(const LLVector2 &tc) const;
// [/RLVa:KB]
//  bool getMask(const LLVector2 &tc);

    void checkTexSize(bool forced = false) const ;

    // Sets the addressing mode used to sample the texture
    // NO sampling state here, and none coming back.
    //
    // A texture carries data plus facts about itself (dimensions, format, swizzle, whether it
    // has a mip chain). How it is READ belongs to the pass reading it: two materials can
    // reference one image and want different wrap modes -- glTF specifies sampler state per
    // texture reference for exactly that reason -- so an image that decided its own filtering
    // had to pick a winner and be wrong for everyone else. Name an ALSampler at the bind.
    LLGLenum getTexTarget()const { return mTarget; }

    void init(bool usemipmaps);
    virtual void cleanup(); // Clean up the LLImageGL so it can be reinitialized.  Be careful when using this in derived class destructors

    void setNeedsAlphaAndPickMask(bool need_mask);

#if LL_IMAGEGL_THREAD_CHECK
    // thread debugging
    std::thread::id mActiveThread;
    void checkActiveThread();
#endif

    // scale down to the desired discard level using GPU
    // returns true if texture was scaled down
    // desired discard will be clamped to max discard
    // if desired discard is less than or equal to current discard, no scaling will occur
    // only works for GL_TEXTURE_2D target
    bool scaleDown(S32 desired_discard);

public:
    // Various GL/Rendering options
    S64Bytes mTextureMemory;
    mutable F32  mLastBindTime; // last time this was bound, by discard level

private:
    // Resolve the GL formats and pixel data an upload will actually use: deprecated
    // format rewriting (GL_ALPHA/GL_LUMINANCE -> R8/RG8, read back through the
    // texture's swizzle). In/out parameters are rewritten in place.
    //
    // Split out because allocation and upload need the resolved format
    // independently: glTexStorage2D has to be given the internal format before
    // any pixels are written. Resolution is deterministic, so resolving once for the
    // allocation and again per upload yields consistent formats.
    static void resolveUploadFormat(S32& intformat, U32& pixformat, U32& pixtype,
                                    const void*& pixels, S32 width, S32 height);

    // Allocate backing storage for the currently-bound texture at the given level-0
    // size, and record its VRAM accounting. Sets mMipLevels. Callers replacing an
    // existing texture must release the old accounting themselves.
    void allocateTextureStorage(S32 width, S32 height, bool has_mips);

    U32 createPickMask(S32 pWidth, S32 pHeight);
    void freePickMask();
    bool isCompressed() const;

    LLPointer<LLImageRaw> mSaveData; // used for destroyGL/restoreGL
    LL::WorkQueue::weak_t mMainQueue;
    U8* mPickMask;  //downsampled bitmap approximation of alpha channel.  NULL if no alpha channel
    U16 mPickMaskWidth;
    U16 mPickMaskHeight;
    S8 mUseMipMaps;
    bool mHasExplicitFormat; // If false (default), GL format is f(mComponents)
    bool mAutoGenMips = false;

    bool mIsMask;
    bool mNeedsAlphaAndPickMask;
    S8   mAlphaStride ;
    S8   mAlphaOffset ;

    // Geometry of the texture mTexName currently names, captured when an off-thread
    // upload begins overwriting the members with the *next* texture's geometry.
    //
    // createGLTexture runs on the LLImageGL thread and writes mWidth/mHeight/mComponents
    // (via setSize) and mCurrentDiscardLevel long before syncTexName publishes the new
    // mTexName on the main thread. Without this the image advertises the new texture's
    // dimensions while still naming the old one for the whole upload, and anything
    // pairing the two -- sculpt reading back from GL at the advertised size -- gets
    // garbage. Only consulted while mUploadInFlight; the members are the truth otherwise.
    struct PublishedGeom
    {
        S32 mWidth = 0;
        S32 mHeight = 0;
        S8  mComponents = 0;
        S8  mCurrentDiscardLevel = -1;
        S8  mMaxDiscardLevel = 0;
    };
    PublishedGeom mPublished;
    bool     mUploadInFlight = false;

    // Live geometry for the UPLOAD path. setImage runs while mUploadInFlight is set (the
    // off-thread createGLTexture), and it describes the texture being BUILT -- the getters
    // above keep answering for the texture mTexName still names, so sizing storage or the
    // per-level writes through them would build the new texture at the old texture's size.
    S32 liveWidth(S32 discard_level) const
    {
        return llmax(mWidth >> llmax(discard_level, 0), 1);
    }
    S32 liveHeight(S32 discard_level) const
    {
        return llmax(mHeight >> llmax(discard_level, 0), 1);
    }

    bool     mGLTextureCreated ;
    LLGLuint mTexName;
    // Whether mTexName has had its storage allocated yet. Storage is always immutable
    // (glTexStorage2D), so it can be allocated exactly once: every upload afterwards is a
    // sub-image write, and any size or format change has to build a new texture object.
    // Set either by allocateTextureStorage or by markStorageAllocated when the owner of a
    // shared texture object allocated on this instance's behalf -- see LLCubeMap, where one
    // glTexStorage2D call covers all six faces. Reset whenever a fresh name is bound.
    bool     mStorageAllocated = false;
    U16      mWidth;
    U16      mHeight;
    S8       mCurrentDiscardLevel;


protected:
    LLGLenum mTarget;       // Normally GL_TEXTURE2D, sometimes something else (ex. cube maps)
    ALTextureSlot::eTextureType mBindTarget;    // Normally TT_TEXTURE, sometimes something else (ex. cube maps)
    bool mHasMipMaps;
    // Number of mip levels the texture holds -- a COUNT, not a highest-index. 1 means
    // level 0 only; 0 means no texture yet. Matches LLRenderTarget::mMipLevels, and is
    // what glTexStorage2D takes. Computed once up front by calcMipLevelCount rather than
    // accumulated during upload, because allocation has to know it before any level is
    // written.
    S32 mMipLevels;

    LLGLboolean mIsResident;

    S8 mComponents;
    S8 mMaxDiscardLevel;

    LLGLint  mFormatInternal; // = GL internalformat
    LLGLenum mFormatPrimary;  // = GL format (pixel data format)
    LLGLenum mFormatType;
    bool     mFormatSwapBytes;// if true, use glPixelStorei(GL_UNPACK_SWAP_BYTES, 1)

    // The original (deprecated) source format the caller asked for, when
    // resolveDeprecatedFormat() rewrote it. Used at texture-creation time
    // to apply the corresponding GL_TEXTURE_SWIZZLE_RGBA mask via
    // applySwizzleForDeprecatedFormat. 0 = no rewrite happened.
    LLGLenum mDeprecatedSourceFormat;

    bool mExternalTexture;

    // STATICS
public:
    static boost::unordered_set<LLImageGL*> sImageList;
    static S32 sCount;
    static U32 sFrameCount;
    static F32 sLastFrameTime;

    // Global memory statistics
    static U32 sBindCount;                  // Tracks number of texture binds for current frame
    static U32 sUniqueCount;                // Tracks number of unique texture binds for current frame
    static LLImageGL* sDefaultGLTexture ;
    static bool sAutomatedTest;
#if DEBUG_MISS
    bool mMissed; // Missed on last bind?
    bool getMissed() const { return mMissed; };
#else
    bool getMissed() const { return false; };
#endif

public:
    static void initClass(LLWindow* window, S32 num_catagories, bool skip_analyze_alpha = false, bool thread_texture_loads = false, bool thread_media_updates = false);
    static void cleanupClass() ;

private:
    static S32 sMaxCategories;
    static bool sSkipAnalyzeAlpha;
    static U32 sScratchPBO;
    static U32 sScratchPBOSize;

    //the flag to allow to call readBackRaw(...).
    //can be removed if we do not use that function at all.
    static bool sAllowReadBackRaw ;
//
//****************************************************************************************************
//The below for texture auditing use only
//****************************************************************************************************
private:
    S32 mCategory ;
public:
    void setCategory(S32 category) {mCategory = category;}
    S32  getCategory()const {return mCategory;}

    void setTexName(GLuint texName) { mTexName = texName; }

    // Declare that this image's texture object already has immutable storage, allocated
    // by whoever owns it. Needed where one GL texture is shared by several LLImageGL
    // objects and no individual object is in a position to allocate: glTexStorage2D on
    // GL_TEXTURE_CUBE_MAP allocates all six faces in one call, so LLCubeMap makes it and
    // the six per-face objects only ever write sub-images.
    void markStorageAllocated() { mStorageAllocated = true; }

    //similar to setTexName, but will call deleteTextures on mTexName if mTexName is not 0 or texname
    void syncTexName(LLGLuint texname);

    // Snapshot the currently-published geometry before an off-thread upload starts
    // overwriting it. Idempotent: both createGLTexture overloads call it, and the outer
    // one delegates to the inner. endUpload() drops the snapshot once the members and
    // mTexName agree again, or after a failed upload.
    void beginUpload();
    void endUpload() { mUploadInFlight = false; }

    //for debug use: show texture size distribution
    //----------------------------------------
    static S32 sCurTexSizeBar ;
    static S32 sCurTexPickSize ;

    static void setCurTexSizebar(S32 index, bool set_pick_size = true) ;
    static void resetCurTexSizebar();

//****************************************************************************************************
//End of definitions for texture auditing use only
//****************************************************************************************************

};

class LLImageGLThread : public LLSimpleton<LLImageGLThread>, LL::ThreadPool
{
public:
    // follows gSavedSettings "RenderGLMultiThreadedTextures"
    static bool sEnabledTextures;
    // follows gSavedSettings "RenderGLMultiThreadedMedia"
    static bool sEnabledMedia;

    LLImageGLThread(LLWindow* window);

    // post a function to be executed on the LLImageGL background thread
    template <typename CALLABLE>
    bool post(CALLABLE&& func)
    {
        return getQueue().post(std::forward<CALLABLE>(func));
    }

    void run() override;

private:
    LLWindow* mWindow;
    void* mContext = nullptr;
    LLAtomicBool mFinished;
};

#endif // LL_LLIMAGEGL_H
