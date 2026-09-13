/**
 * @file lluiimage.cpp
 * @brief UI implementation
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

// Utilities functions the user interface needs

//#include "llviewerprecompiledheaders.h"
#include "linden_common.h"

// Project includes
#include "lluiimage.h"
#include "llframetimer.h"
#include <algorithm>

// Static member initialization
std::vector<LLPointer<LLUIImage> > LLUIImage::sImageList;
size_t LLUIImage::sCleanupIndex = 0;
bool LLUIImage::sEnableDisplayListsCollection = true;

LLUIImage::LLUIImage(const std::string& name, LLPointer<LLTexture> image)
:   mName(name),
    mImage(image),
    mScaleRegion(0.f, 1.f, 1.f, 0.f),
    mClipRegion(0.f, 1.f, 1.f, 0.f),
    mImageLoaded(NULL),
    mScaleStyle(SCALE_INNER),
    mCachedW(-1),
    mCachedH(-1)
{
    getTextureWidth();
    getTextureHeight();
}

LLUIImage::LLUIImage(std::string&& name, LLPointer<LLTexture> image) :
    mName(std::move(name)),
    mImage(image),
    mScaleRegion(0.f, 1.f, 1.f, 0.f),
    mClipRegion(0.f, 1.f, 1.f, 0.f),
    mImageLoaded(NULL),
    mScaleStyle(SCALE_INNER),
    mCachedW(-1),
    mCachedH(-1)
{
    getTextureWidth();
    getTextureHeight();
}

LLUIImage::~LLUIImage()
{
    delete mImageLoaded;

    // Unregister from global cleanup list (sanity check)
    // But it's supposed to be cleared already, else we wouldn't
    // be destructing this object.
    unregisterFromGlobalCleanup();
    mDisplayLists.clear();
}

S32 LLUIImage::getWidth() const
{
    // return clipped dimensions of actual image area
    return ll_round((F32)mImage->getWidth(0) * mClipRegion.getWidth());
}

S32 LLUIImage::getHeight() const
{
    // return clipped dimensions of actual image area
    return ll_round((F32)mImage->getHeight(0) * mClipRegion.getHeight());
}

void LLUIImage::drawCached(S32 x, S32 y, S32 width, S32 height, const LLColor4& color, bool solid_color) const
{
    LLImageGL* gl_image = mImage->getGLTexture();
    if (!gl_image)
    {
        // Nothing to record against until the texture exists, and nothing to draw.
        return;
    }

    // The GL name stands for the texture's current state: a discard change or a
    // recreation renames it, and recordings made against the old name lapse.
    const LLVector3 ui_scale = gGL.getUIScale();
    const PackedKey key = PackedKey::create(width, height, color, solid_color, ui_scale, gl_image->getTexName());

    // Where the image sits on screen, in the units the recording's vertices are
    // in. It goes through the modelview rather than into the vertices, which is
    // what lets one recording serve every position.
    LLVector3 offset = gGL.getUITranslation();
    offset.mV[VX] += (F32)x;
    offset.mV[VY] += (F32)y;
    offset.scaleVec(ui_scale);

    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();
    gGL.translatef(offset.mV[VX], offset.mV[VY], 0.f);

    auto it = mDisplayLists.find(key);
    if (it != mDisplayLists.end())
    {
        it->second.last_used = LLFrameTimer::getTotalSeconds();
        replayDisplayList(it->second.list, color, solid_color);
    }
    else
    {
        recordDisplayList(key, width, height, color, solid_color);
    }

    gGL.popMatrix();
}

// static
void LLUIImage::replayDisplayList(buffer_data_list_t& list, const LLColor4& color, bool solid_color)
{
    LL_PROFILE_ZONE_SCOPED;

    if (solid_color)
    {
        gSolidColorProgram.bind();
    }

    gGL.color4fv(color.mV); // for the shader

    for (LLVertexBufferData& buffer : list)
    {
        buffer.draw();
    }

    if (solid_color)
    {
        gUIProgram.bind();
    }
}

void LLUIImage::recordDisplayList(const PackedKey& key, S32 width, S32 height, const LLColor4& color, bool solid_color) const
{
    LL_PROFILE_ZONE_SCOPED;

    CachedDisplayList cached;
    cached.last_used = LLFrameTimer::getTotalSeconds();

    // Recorded at the origin under the UI scale alone. The offset the view tree
    // accumulated is already on the modelview; baked into the vertices as well it
    // would both double up and tie the recording to this one position.
    const LLVector3 ui_scale = gGL.getUIScale();
    gGL.pushUIMatrix();
    gGL.loadUIIdentity();
    gGL.scaleUI(ui_scale.mV[VX], ui_scale.mV[VY], ui_scale.mV[VZ]);

    gGL.beginList(&cached.list);

    gl_draw_scaled_image_with_border(
        0, 0,
        width, height,
        mImage,
        color,
        solid_color,
        mClipRegion,
        mScaleRegion,
        mScaleStyle == SCALE_INNER);

    gGL.endList();

    gGL.popUIMatrix();

    // emplace: a key is only recorded once it has failed to be found.
    mDisplayLists.emplace(key, std::move(cached));

    // Register for cleanup on first recording
    if (mDisplayLists.size() == 1)
    {
        sImageList.push_back(const_cast<LLUIImage*>(this));
    }
}

void LLUIImage::invalidateDisplayLists()
{
    mDisplayLists.clear();

    unregisterFromGlobalCleanup();
}

void LLUIImage::cleanupDisplayLists()
{
    if (mDisplayLists.empty())
    {
        llassert(false); //it shouldn't be in this list
        unregisterFromGlobalCleanup();
        // marks current position for a recheck. Increments after cleanupDisplayLists.
        if (sCleanupIndex > 0)
        {
            sCleanupIndex--;
        }
        else if (sImageList.empty())
        {
            sCleanupIndex = 0;
        }
        else
        {
            sCleanupIndex = sImageList.size() - 1;
        }
        return;
    }

    // Time threshold for cleaning up unused display lists (global cleanup)
    constexpr F64 DISPLAY_LIST_TIMEOUT = 2.0;
    const F64 now = LLFrameTimer::getTotalSeconds();

    // Remove display lists that haven't been used recently
    for (auto it = mDisplayLists.begin(); it != mDisplayLists.end(); )
    {
        if (now - it->second.last_used > DISPLAY_LIST_TIMEOUT)
        {
            it = mDisplayLists.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Unregister from cleanup list if all display lists were removed
    if (mDisplayLists.empty())
    {
        unregisterFromGlobalCleanup();
        // marks current position for a recheck. Increments after cleanupDisplayLists.
        if (sCleanupIndex > 0)
        {
            sCleanupIndex--;
        }
        else if (sImageList.empty())
        {
            sCleanupIndex = 0;
        }
        else
        {
            sCleanupIndex = sImageList.size() - 1;
        }
    }
}

void LLUIImage::unregisterFromGlobalCleanup()
{
    auto list_it = std::find(sImageList.begin(), sImageList.end(), this);
    if (list_it != sImageList.end())
    {
        // Swap with last element and pop (O(1) removal)
        *list_it = sImageList.back();
        sImageList.pop_back();
    }
}

// static
void LLUIImage::updateClass()
{
    if (sImageList.empty())
    {
        return;
    }

    // Clean up a batch of images each frame to amortize the cost
    // ensuring all images are checked regularly
    // Note: buffers often get obsolete in batches, perhaps
    // increase rate of cleanup after a buffer was removed?
    // and decrease rate if no buffer were removed and creates
    // for a while?
    constexpr size_t BATCH_SIZE = 8;
    size_t images_to_process = std::min(BATCH_SIZE, sImageList.size());

    for (size_t i = 0; i < images_to_process; ++i)
    {
        if (sCleanupIndex >= sImageList.size())
        {
            sCleanupIndex = 0;
        }

        sImageList[sCleanupIndex]->cleanupDisplayLists();
        ++sCleanupIndex;
    }
}

void LLUIImage::cleanupClass()
{
    std::vector<LLPointer<LLUIImage> > list_copy(sImageList);
    sImageList.clear();
    for (LLUIImage* image : list_copy)
    {
        // invalidateDisplayLists will attempt to clear sImageList
        image->invalidateDisplayLists();
    }
    sCleanupIndex = 0;
}

void LLUIImage::draw3D(const LLVector3& origin_agent, const LLVector3& x_axis, const LLVector3& y_axis,
                        const LLRect& rect, const LLColor4& color)
{
    F32 border_scale = 1.f;
    F32 border_height = (1.f - mScaleRegion.getHeight()) * getHeight();
    F32 border_width = (1.f - mScaleRegion.getWidth()) * getWidth();
    if (rect.getHeight() < border_height || rect.getWidth() < border_width)
    {
         if(border_height - rect.getHeight() > border_width - rect.getWidth())
         {
             border_scale = (F32)rect.getHeight() / border_height;
         }
         else
         {
             border_scale = (F32)rect.getWidth() / border_width;
         }
    }

    LLRender2D::pushMatrix();
    {
        LLVector3 rect_origin = origin_agent + ((F32)rect.mLeft * x_axis) + ((F32)rect.mBottom * y_axis);
        LLRender2D::translate(rect_origin.mV[VX],
                                            rect_origin.mV[VY],
                                            rect_origin.mV[VZ]);
        gGL.getTextureSlot(0)->bindSampled(getImage(), ALSamplers::BilinearClamp);
        gGL.color4fv(color.mV);

        LLRectf center_uv_rect(mClipRegion.mLeft + mScaleRegion.mLeft * mClipRegion.getWidth(),
                            mClipRegion.mBottom + mScaleRegion.mTop * mClipRegion.getHeight(),
                            mClipRegion.mLeft + mScaleRegion.mRight * mClipRegion.getWidth(),
                            mClipRegion.mBottom + mScaleRegion.mBottom * mClipRegion.getHeight());
        gl_segmented_rect_3d_tex(mClipRegion,
                                center_uv_rect,
                                LLRectf(border_width * border_scale * 0.5f / (F32)rect.getWidth(),
                                        (rect.getHeight() - (border_height * border_scale * 0.5f)) / (F32)rect.getHeight(),
                                        (rect.getWidth() - (border_width * border_scale * 0.5f)) / (F32)rect.getWidth(),
                                        (border_height * border_scale * 0.5f) / (F32)rect.getHeight()),
                                (F32)rect.getWidth() * x_axis,
                                (F32)rect.getHeight() * y_axis);

    } LLRender2D::popMatrix();
}

//#include "lluiimage.inl"

boost::signals2::connection LLUIImage::addLoadedCallback( const image_loaded_signal_t::slot_type& cb )
{
    if (!mImageLoaded)
    {
        mImageLoaded = new image_loaded_signal_t();
    }
    return mImageLoaded->connect(cb);
}


void LLUIImage::onImageLoaded()
{
    if (mImageLoaded)
    {
        (*mImageLoaded)();
    }

    invalidateDisplayLists();
}

namespace LLInitParam
{
    void ParamValue<LLUIImage*>::updateValueFromBlock()
    {
        // The keyword "none" is specifically requesting a null image
        // do not default to current value. Used to overwrite template images.
        if (name() == "none")
        {
            updateValue(NULL);
            return;
        }

        LLUIImage* imagep =  LLRender2D::getInstance()->getUIImage(name());
        if (imagep)
        {
            updateValue(imagep);
        }
    }

    void ParamValue<LLUIImage*>::updateBlockFromValue(bool make_block_authoritative)
    {
        static const std::string sNone("none");

        const LLUIImage* imagep = getValue();
        setComponent(name, imagep ? imagep->getName() : sNone, make_block_authoritative);
    }


    bool ParamCompare<LLUIImage*, false>::equals(
        LLUIImage* const &a,
        LLUIImage* const &b)
    {
        // force all LLUIImages for XML UI export to be "non-default"
        if (!a && !b)
            return false;
        else
            return (a == b);
    }
}

