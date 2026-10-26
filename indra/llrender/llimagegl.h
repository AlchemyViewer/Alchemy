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
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#define LL_IMAGEGL_THREAD_CHECK 0 //set to 1 to enable thread debugging for ImageGL

class LLWindow;

#define BYTES_TO_MEGA_BYTES(x) ((x) >> 20)
#define MEGA_BYTES_TO_BYTES(x) ((x) << 20)

namespace LLImageGLMemory
{
    // What a texture's storage is for. The accounting keeps a total of each, and every
    // texture's storage is in one of them.
    enum EClass : U8
    {
        STREAMED_TEXTURE, // fetched by asset ID or from the bake service, but for one the UI alone holds (a
                          // UI boost and no face or volume in the world), which is OTHER_TEXTURE. Not all of it
                          // is the bias's to bring down: boosted ones (sculpt maps, HUDs, bakes) and those that
                          // are not LOD textures keep their size
        OTHER_TEXTURE,    // every other image: UI, media, local images, lookup tables
        RENDER_TARGET,    // render target attachments
        REFLECTION_PROBE, // the reflection and hero probes' cube map arrays
        NUM_CLASSES
    };

    // Record the storage of the texture bound on unit 0, as `cls`: `levels` levels from the
    // level-0 width x height down, as dataFormatVRAMBytes counts them, for each of `count`
    // layers -- array layers, cube faces or 3D slices, each of which holds the levels. The
    // texture must have no storage recorded. Returns the bytes recorded. Storage GL refused
    // for want of video memory stays counted: nothing asks GL about it, which would wait for
    // a threaded driver's thread, and that GL ran out is noted (take_gl_out_of_memory).
    U64 alloc_tex_image(U32 width, U32 height, U32 intformat, U32 count, U32 levels, EClass cls);
    void free_tex_image(U32 texName);
    void free_tex_images(U32 count, const U32* texNames);
    void free_cur_tex_image();
    // Count the storage recorded for texName, if any, as `cls` from now on.
    void set_tex_class(U32 texName, EClass cls);
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

    // Main thread: publish every off-thread upload whose fence has signalled (syncTexName,
    // then its afterPublish callbacks). Returns how many are still waiting on the GPU. With
    // `poll` it polls the fences no frame's end has seen signalled; without, it asks GL
    // nothing and publishes what pollUploads saw. updateClass publishes without polling; a
    // loop waiting on texture creation polls.
    static U32 publishUploads(bool poll = true);

    // Main thread, at the frame's end (LLRender::endFrame): poll the fences of the uploads
    // handed over, oldest first, for updateClass to publish the signalled ones, each only once
    // the LLImageGL thread has seen it signalled (LLImageGLThread::watchUpload). A poll of a
    // fence not yet signalled waits for a threaded driver's own thread to catch up; a poll of
    // one signalled answers at once.
    static void pollUploads();

    // Main thread, as the viewer shuts down: wait for the GPU on every upload handed over, a
    // second at most in all, and publish them, since nothing will poll again. What has not
    // signalled by then is given up, its texture deleted unpublished and what waited on it
    // run (afterPublish), so that the references it holds are let go while the world they
    // name is still whole.
    static void finishUploads();

    // The bytes of every texture's storage, as alloc_tex_image recorded it: the levels each
    // was allocated with, at dataFormatVRAMBytes' size. The second form counts one class.
    // ALVRAMUsage puts them beside what buffers hold.
    static U64 getTextureBytesAllocated();
    static U64 getTextureBytesAllocated(LLImageGLMemory::EClass cls);

    // These 2 functions replace glGenTextures() and glDeleteTextures()
    static void generateTextures(S32 numTextures, U32 *textures);
    static void deleteTextures(S32 numTextures, const U32 *textures);

    // Size calculation
    //
    // dataFormatBits/Bytes report the *tight host-side* layout — i.e. the
    // packed CPU buffer size for glTexImage2D/glTexSubImage2D uploads
    // (matching the pixel format, at a byte a component for an unsized
    // one). Use these for source-pointer arithmetic.
    //
    // dataFormatVRAMBits/Bytes report the *driver-side* allocation for
    // VRAM accounting. They differ from the host values for formats that
    // drivers pad (RGB8 -> RGBX, RGB16F -> RGBA16F, RGB32F -> RGBA32F,
    // DEPTH_COMPONENT24 -> 32-bit). For other formats they delegate to
    // dataFormatBits.
    //
    // dataFormatVRAMBytes counts `levels` levels from the level-0 width x height down, each
    // dimension halved and held at 1, which is the chain glTexStorage* allocates: the texels
    // at their padded size, and for a block-compressed format the whole 4x4 blocks that cover
    // each level.
    static S32 dataFormatBits(S32 dataformat);
    static S64 dataFormatBytes(S32 dataformat, S32 width, S32 height);
    static S32 dataFormatVRAMBits(S32 dataformat);
    static S64 dataFormatVRAMBytes(S32 dataformat, S32 width, S32 height, S32 levels = 1);
    static S32 dataFormatComponents(S32 dataformat);

    // The sized integer colour formats: R32UI, RG16I, RGB10_A2UI and the rest. A shader reads their texels as
    // integers, through an isampler or a usampler, and GL neither filters them nor builds their mips: a filtering
    // sampler leaves such a texture incomplete, reading 0, and glGenerateMipmap refuses it.
    static bool isIntegerFormat(S32 internal_format);
    // Of those, the ones whose texels are signed, read through an isampler.
    static bool isSignedIntegerFormat(S32 internal_format);
    // The pixel format an integer internal format's host data is laid out in: GL_RED_INTEGER for R32UI,
    // GL_RGBA_INTEGER for RGBA16I. 0 for any other format.
    static U32 integerPixelFormat(S32 internal_format);

    bool updateBindStats() const ;
    F32 getTimePassedSinceLastBound();
    void forceUpdateBindStats(void) const;

    // needs to be called every frame
    static void updateStats(F64 current_time);

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

    // Wrap a texture object this LLImageGL does not own, created via GL elsewhere. Use
    // with caution: nothing here deletes it.
    LLImageGL(LLGLuint mTexName, U32 components, LLGLenum target, LLGLint  formatInternal, LLGLenum formatPrimary, LLGLenum formatType);

    // What analyzeAlphaData finds in a level's alpha.
    struct AlphaAnalysis
    {
        // Whether it is near enough to 0 or 1 everywhere to be tested as a mask rather than blended.
        bool mIsMask = false;
        // The highest alpha of any texel.
        U8 mMaxAlpha = 255;
        // The lowest alpha of any texel. With mMaxAlpha's default, an analysis that read nothing bounds every alpha.
        U8 mMinAlpha = 0;
    };

protected:
    virtual ~LLImageGL();

    static AlphaAnalysis analyzeAlphaData(const void* data_in, U32 w, U32 h, S8 alpha_offset, S8 alpha_stride);
    // What a texture with no alpha channel analyses as: every texel opaque.
    static AlphaAnalysis opaqueAlphaAnalysis() { AlphaAnalysis analysis; analysis.mMinAlpha = 255; return analysis; }
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
    // them, so levels 1+ can be filled with setManualSubImage. The storage is counted as
    // `cls`.
    static void allocateTexture2D(U32 target, S32 intformat, S32 width, S32 height,
                                  U32 pixformat, U32 pixtype, const void *pixels,
                                  S32 levels = 1,
                                  LLImageGLMemory::EClass cls = LLImageGLMemory::OTHER_TEXTURE);

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

    // Off the main thread: fence this thread's upload of new_tex_name and hand it to the main
    // thread, which publishes it in place of mTexName once the fence has signalled (see
    // publishUploads), and to the LLImageGL thread to see it signalled (watchUpload). Does
    // not wait for the GPU.
    void syncToMainThread(LLGLuint new_tex_name);

    // Main thread: run fn once the upload handed over by syncToMainThread has published, or
    // now if none is waiting. A caller finishing an off-thread upload goes through this, so
    // what it does next -- ending the snapshot, letting another upload start -- follows the
    // texture onto the screen rather than racing it.
    void afterPublish(std::function<void()> fn);

    // Read back a raw image for this discard level, if it exists. Waits for the GPU to finish everything queued ahead
    // of the read; requestReadBack does not.
    bool readBackRaw(S32 discard_level, LLImageRaw* imageraw, bool compressed_ok) const;

    // Main thread: the pixels of level `discard_level` of what mTexName holds (-1: the level on screen), read back
    // without waiting on the GPU (ALTextureReadback). requestReadBack issues the read, giving up one it issued before:
    // false where readBackRaw would not read the level, for a compressed texture, or where the pool has no room this
    // frame. collectReadBack fills `imageraw` with the read, at the size it was read, once the GPU has written it, a
    // few frames' ends later, and gives the level it is of.
    enum EReadBack
    {
        READBACK_NONE,    // nothing asked, or the read was lost (cancelled, expired, the context gone)
        READBACK_PENDING, // asked, and the GPU has yet to be shown finished with it
        READBACK_READY    // `imageraw` holds it
    };
    bool      requestReadBack(S32 discard_level = -1);
    EReadBack collectReadBack(LLImageRaw* imageraw, S32& discard_level);
    void      cancelReadBack();
    bool      isReadBackRequested() const { return mReadBack != 0; }
    void destroyGLTexture();
    void forceToInvalidateGLTexture();

    void setExplicitFormat(LLGLint internal_format, LLGLenum primary_format, LLGLenum type_format = 0, bool swap_bytes = false);
    void setComponents(S8 ncomponents) { mComponents = ncomponents; }

    // While an off-thread upload is in flight the members below describe the texture
    // being built, not the one mTexName still names. Consumers pair these getters with
    // getTexName() -- readBackRaw reads mTexName back at getWidth() -- so they must
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
    // The bytes of the storage allocateTextureStorage gave the texture: what alloc_tex_image
    // records for it. 0 where its owner allocated it (markStorageAllocated), or it has none.
    S64Bytes getTextureMemory() const { return mUploadInFlight ? mPublished.mTextureMemory : mTextureMemory; }
    // What the texture's storage is counted as, OTHER_TEXTURE unless set. Setting it moves
    // the storage mTexName has to the new class; storage an upload in flight has made moves
    // as it publishes. Main thread.
    LLImageGLMemory::EClass getVRAMClass() const { return mVRAMClass.load(std::memory_order_relaxed); }
    void setVRAMClass(LLImageGLMemory::EClass cls);

    // The bytes of the buffer scaleDown reads a level back through.
    static U64 getScratchBufferBytes() { return sScratchPBOSize; }
    S64  getBytes(S32 discard_level = -1) const;
    bool getBoundRecently() const;
    bool isJustBound() const;
    bool getHasExplicitFormat() const { return mHasExplicitFormat; }
    LLGLenum getPrimaryFormat() const { return mUploadInFlight ? mPublished.mFormatPrimary : mFormatPrimary; }
    // The source was alpha only (GL_ALPHA). It is held as R8 under a {0,0,0,R}
    // swizzle, so getPrimaryFormat() cannot tell it from a luminance source;
    // the format it was asked for can.
    bool isAlphaOnly() const { return (mUploadInFlight ? mPublished.mDeprecatedSourceFormat : mDeprecatedSourceFormat) == GL_ALPHA; }
    LLGLenum getFormatType() const { return mFormatType; }

    // Internal format to hand glTexStorage*. Block-compressed textures keep their sized
    // compressed format in mFormatPrimary rather than mFormatInternal.
    S32 getStorageInternalFormat() const;

    bool getHasGLTexture() const { return mTexName != 0; }
    LLGLuint getTexName() const { return mTexName; }
    // What the texture holds, as a value no texture has held before: taken again by every upload into it, in place or
    // not. A pass that keeps what it drew tells by it whether the pixels it sampled are as they were.
    U64 getContentStamp() const { return mContentStamp.load(std::memory_order_relaxed); }

    bool getIsAlphaMask() const;
    // The highest alpha of the level its alpha was last analysed at (analyzeAlpha), or 255 where it was not: no
    // alpha channel, an alpha it cannot read, or an analysis not yet back.
    U8 getMaxAlpha() const { return mMaxAlpha; }
    // The lowest alpha of that level, 255 where there is no alpha channel, or 0 where it was not analysed: an alpha
    // it cannot read, or an analysis not yet back.
    U8 getMinAlpha() const { return mMinAlpha; }

    bool getIsResident(bool test_now = false); // not const

    void setTarget(const LLGLenum target, const ALTextureSlot::eTextureType bind_target);

    ALTextureSlot::eTextureType getTarget(void) const { return mBindTarget; }
    bool isGLTextureCreated(void) const { return mGLTextureCreated.load(std::memory_order_relaxed); }
    void setGLTextureCreated (bool initialized) { mGLTextureCreated.store(initialized, std::memory_order_relaxed); }

    bool getUseMipMaps() const { return mUseMipMaps; }
    void setUseMipMaps(bool usemips) { mUseMipMaps = usemips; }
    void setHasMipMaps(bool hasmips) { mHasMipMaps = hasmips; }
    // Build the pick mask from level-0 RGBA8 data. During an upload in flight, or off the
    // main thread, the result waits for the texture it describes to publish.
    void updatePickMask(S32 width, S32 height, const U8* data_in);
// [RLVa:KB] - Checked: RLVa-2.2 (@setoverlay)
    bool getMask(const LLVector2 &tc) const;
// [/RLVa:KB]
//  bool getMask(const LLVector2 &tc);

    void checkTexSize(bool forced = false) const ;

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
    mutable F64  mLastBindTime; // last time this was bound, by discard level

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

    // Whether an upload of this level-0 size, in the current format, can be written into
    // the storage the current texture object already has rather than needing a new one.
    bool storageFits(S32 width, S32 height) const;

    void freePickMask();
    bool isCompressed() const;

    // Bytes of one level at this size, as getBytes counts it: an integer level by its pixel format AND
    // type -- an R32UI texel is four bytes, which GL_RED_INTEGER alone cannot say -- and any other by dataFormatBytes
    // of its pixel format, a byte a component. Rounded up to four, as dataFormatBytes rounds.
    S64 levelBytes(S32 width, S32 height) const;

    // The alpha facts an upload derives, routed through mPendingAlpha when stagesAlpha().
    // setPickMask takes ownership of mask, which may be null for "no pick mask".
    bool stagesAlpha() const;
    void setPickMask(U8* mask, U16 width, U16 height);
    void setAlphaAnalysis(const AlphaAnalysis& analysis);

    // Sets the published analysis to what a texture without a readable alpha holds: no mask, and opaque.
    void clearAlphaAnalysis();
    void publishPendingAlpha();
    void discardPendingAlpha();

    // Warn when an upload runs off the main thread without the snapshot beginUpload takes.
    void checkUploadBegun() const;

    LLPointer<LLImageRaw> mSaveData; // used for destroyGL/restoreGL
    LL::WorkQueue::weak_t mMainQueue;
    U8* mPickMask;  //downsampled bitmap approximation of alpha channel.  NULL if no alpha channel
    U16 mPickMaskWidth;
    U16 mPickMaskHeight;
    S8 mUseMipMaps;
    bool mHasExplicitFormat; // If false (default), GL format is f(mComponents)
    S8   mFormatComponents = -1; // the component count the format was worked out for, or -1
    void resolveComponentFormat(S8 components);
    bool mAutoGenMips = false;

    bool mIsMask;
    U8   mMaxAlpha;
    U8   mMinAlpha;
    bool mNeedsAlphaAndPickMask;
    LLAtomicU32 mAlphaAnalysisSerial; // for request tracking.
    S8   mAlphaStride ;
    S8   mAlphaOffset ;

    // The pick mask and alpha analysis of a texture that has not published yet.
    //
    // getMask and getIsAlphaMask answer for the texture mTexName names, and the main thread
    // asks them every frame (hover picking, the alpha pool). An off-thread upload building
    // the mask in place would free the buffer getMask was reading, so it builds here
    // instead and syncTexName swaps it in with the texture.
    struct PendingAlpha
    {
        U8*  mPickMask       = nullptr;
        U16  mPickMaskWidth  = 0;
        U16  mPickMaskHeight = 0;
        bool mHasPickMask    = false; // a result is waiting, possibly "no pick mask"
        AlphaAnalysis mAnalysis;
        bool mHasAnalysis    = false;
    };
    PendingAlpha mPendingAlpha;

    // Off-thread uploads of this image handed to the main thread and not yet published, and
    // what is to run once none are. Main thread only; see syncToMainThread / afterPublish.
    void queuePublish(LLGLuint name, GLsync fence, std::shared_ptr<std::atomic<bool>> seen);
    void finishPublish(LLGLuint name);
    U32 mPublishesPending = 0;
    std::vector<std::function<void()>> mOnPublished;

    // Geometry of the texture mTexName currently names, captured when an off-thread
    // upload begins overwriting the members with the *next* texture's geometry.
    //
    // createGLTexture runs on the LLImageGL thread and writes mWidth/mHeight/mComponents
    // (via setSize) and mCurrentDiscardLevel long before syncTexName publishes the new
    // mTexName on the main thread. Without this the image advertises the new texture's
    // dimensions while still naming the old one for the whole upload, and anything
    // pairing the two -- sculpt reading back from GL at the advertised size -- gets
    // garbage. Only consulted while mUploadInFlight; the members are the truth otherwise.
    // Both are written on the main thread only; see beginUpload.
    struct PublishedGeom
    {
        S32 mWidth = 0;
        S32 mHeight = 0;
        S8  mComponents = 0;
        S8  mCurrentDiscardLevel = -1;
        S8  mMaxDiscardLevel = 0;
        S32 mMipLevels = 0;
        // The format, which createGLTexture works out again from mComponents for every
        // upload of an image without an explicit one.
        LLGLenum mFormatPrimary = 0;
        LLGLenum mDeprecatedSourceFormat = 0;
        S64Bytes mTextureMemory = S64Bytes(0);
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

    // Written by the upload on LLImageGLThread and by the main thread that asks for the texture.
    std::atomic<bool> mGLTextureCreated;
    // getContentStamp, taken again by touchContents.
    std::atomic<U64> mContentStamp{ 0 };
    void touchContents();
    LLGLuint mTexName;
    // Whether mTexName has had its storage allocated yet. Storage is always immutable
    // (glTexStorage2D), so it can be allocated exactly once: every upload afterwards is a
    // sub-image write, and any size or format change has to build a new texture object.
    // Set either by allocateTextureStorage or by markStorageAllocated when the owner of a
    // shared texture object allocated on this instance's behalf -- see LLCubeMap, where one
    // glTexStorage2D call covers all six faces. Reset whenever a fresh name is bound.
    bool     mStorageAllocated = false;
    // What allocateTextureStorage gave it, so a later upload can tell whether it still
    // fits. All zero when the owner of a shared object allocated it.
    struct StorageDesc
    {
        S32 mWidth  = 0;
        S32 mHeight = 0;
        S32 mLevels = 0;
        S32 mFormat = 0;
    };
    StorageDesc mStorage;
    // Written on the main thread, and read by the upload that allocates storage, which may be
    // the LLImageGL thread's.
    std::atomic<LLImageGLMemory::EClass> mVRAMClass{ LLImageGLMemory::OTHER_TEXTURE };
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

    // The read requestReadBack issued, and what it read.
    U64 mReadBack           = 0;
    S32 mReadBackDiscard    = -1;
    S32 mReadBackWidth      = 0;
    S32 mReadBackHeight     = 0;
    S32 mReadBackComponents = 0;

    // The level a readback of `discard_level` (-1: the level on screen) reads -- `gl_discard` of mTexName -- its size and
    // its components, through the published view; false where mTexName holds no such level. Binds mTexName on unit 0,
    // for the read.
    bool readBackLevel(S32& discard_level, S32& gl_discard, S32& width, S32& height, S32& ncomponents) const;

    // STATICS
public:
    static boost::unordered_set<LLImageGL*> sImageList;
    static S32 sCount;
    static U32 sFrameCount;
    static F64 sLastFrameTime;

    // Global memory statistics
    static U32 sBindCount;                  // Tracks number of texture binds for current frame
    static U32 sUniqueCount;                // Tracks number of unique texture binds for current frame
    static U32 sReadbacks;                  // Textures read back from GL for current frame (readBackRaw)
    static F64 sReadbackSeconds;            // What those reads waited for the GPU, for current frame
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
    void markStorageAllocated()
    {
        mStorageAllocated = true;
        mStorage = StorageDesc();
    }

    // Publish texname in place of mTexName, deleting the old one, along with what the
    // upload that built it staged (the pick mask), then end the upload. Main thread.
    void syncTexName(LLGLuint texname);

    // Bracket an upload handed to the LLImageGL thread, on the MAIN thread: begin before
    // posting the work, and end in its completion callback whatever the worker did
    // (syncTexName has already ended it if the texture published). In between, the
    // geometry getters answer for the texture mTexName still names.
    void beginUpload();
    // The same, first working out here the format an image without an explicit one takes
    // from the components it is about to upload: see resolveComponentFormat.
    void beginUpload(S8 components);
    void endUpload();

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
    // follows gSavedSettings "RenderGLMultiThreadedTextures", and turned off by the thread where its context cannot be
    // made current
    static std::atomic<bool> sEnabledTextures;
    // follows gSavedSettings "RenderGLMultiThreadedMedia", and turned off with it
    static std::atomic<bool> sEnabledMedia;

    LLImageGLThread(LLWindow* window);
    // Joins the thread first: run() uses the window and context after the
    // queue closes.
    ~LLImageGLThread() override;

    // Finish the uploads posted and stop the thread, as the destructor does.
    using LL::ThreadPool::close;

    // post a function to be executed on the LLImageGL background thread
    template <typename CALLABLE>
    bool post(CALLABLE&& func)
    {
        return getQueue().post(std::forward<CALLABLE>(func));
    }

    void run() override;

    // On the thread: an upload handed to the main thread (LLImageGL::syncToMainThread), whose fence the thread sees
    // signalled behind the work queued after it, and then raises `seen`, which the main thread waits for before it
    // polls the fence (LLImageGL::pollUploads).
    void watchUpload(GLsync fence, std::shared_ptr<std::atomic<bool>> seen);

private:
    // On the thread: raises the seen flag of every upload in flight whose fence has signalled, oldest first, waiting for
    // them only while nothing else is queued, and posting itself again behind what is.
    void seeUploads();

    struct UploadInFlight
    {
        GLsync                             mFence;
        std::shared_ptr<std::atomic<bool>> mSeen;
    };

    LLWindow* mWindow;
    void* mContext = nullptr;
    std::deque<UploadInFlight> mUploadsInFlight; // the thread's
    bool mSeeingPosted = false;                  // the thread's: seeUploads() is queued
};

#endif // LL_LLIMAGEGL_H
