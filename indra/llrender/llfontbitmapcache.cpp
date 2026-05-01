/**
 * @file llfontbitmapcache.cpp
 * @brief Storage for previously rendered glyphs.
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

#include "linden_common.h"

#include "llgl.h"
#include "llfontbitmapcache.h"
#include "llframetimer.h"

LLFontBitmapCache::LLFontBitmapCache()

{
}

LLFontBitmapCache::~LLFontBitmapCache()
{
}

void LLFontBitmapCache::init(S32 max_char_width,
                             S32 max_char_height)
{
    reset();

    mMaxCharWidth = max_char_width;
    mMaxCharHeight = max_char_height;

    S32 image_width = mMaxCharWidth * 20;
    S32 pow_iw = 2;
    while (pow_iw < image_width)
    {
        pow_iw <<= 1;
    }
    image_width = pow_iw;
    image_width = llmin(512, image_width); // Don't make bigger than 512x512, ever.

    mBitmapWidth = image_width;
    mBitmapHeight = image_width;
}

LLImageRaw *LLFontBitmapCache::getImageRaw(EFontGlyphType bitmap_type, U32 bitmap_num) const
{
    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_type >= EFontGlyphType::Count || bitmap_num >= mImageRawVec[bitmap_idx].size())
        return nullptr;

    touchSheet(bitmap_type, bitmap_num);
    return mImageRawVec[bitmap_idx][bitmap_num];
}

LLImageGL *LLFontBitmapCache::getImageGL(EFontGlyphType bitmap_type, U32 bitmap_num) const
{
    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_type >= EFontGlyphType::Count || bitmap_num >= mImageGLVec[bitmap_idx].size())
        return nullptr;

    touchSheet(bitmap_type, bitmap_num);
    return mImageGLVec[bitmap_idx][bitmap_num];
}


bool LLFontBitmapCache::nextOpenPos(S32 width, S32 height, S32& pos_x, S32& pos_y, EFontGlyphType bitmap_type, U32& bitmap_num)
{
    if (bitmap_type >= EFontGlyphType::Count)
    {
        return false;
    }

    const U32 bitmap_idx = static_cast<U32>(bitmap_type);

    // The last sheet is the active allocation target. If it was released by
    // the eviction sweep, force a fresh sheet — its in-flight pen offsets are
    // pointing into a freed image.
    const bool last_sheet_released = !mImageRawVec[bitmap_idx].empty()
                                  && mImageRawVec[bitmap_idx].back().isNull();
    bool need_new_sheet = mImageRawVec[bitmap_idx].empty() || last_sheet_released;

    if (!need_new_sheet && (mCurrentOffsetX[bitmap_idx] + width + 4) > mBitmapWidth)
    {
        // Y advance must clear the tallest glyph already placed in this row,
        // not just mMaxCharHeight (which is derived from the font's outline
        // bbox and underestimates color-emoji strike heights). Likewise the
        // "fits another row?" check uses the new glyph's own height to be
        // sure it won't run off the bottom.
        const S32 row_step = llmax(mMaxCharHeight, mCurrentRowMaxHeight[bitmap_idx]);
        const S32 next_row_height = llmax(mMaxCharHeight, height);
        if ((mCurrentOffsetY[bitmap_idx] + row_step + next_row_height + 8) > mBitmapHeight)
        {
            need_new_sheet = true;
        }
        else
        {
            // Move to next row in current image.
            mCurrentOffsetX[bitmap_idx] = 4;
            mCurrentOffsetY[bitmap_idx] += row_step + 4;
            mCurrentRowMaxHeight[bitmap_idx] = 0;
        }
    }

    if (need_new_sheet)
    {
        // We're out of space in the current image, or no image
        // has been allocated yet.  Make a new one.

        S32 image_width = mMaxCharWidth * 20;
        S32 pow_iw = 2;
        while (pow_iw < image_width)
        {
            pow_iw *= 2;
        }
        image_width = pow_iw;
        image_width = llmin(512, image_width); // Don't make bigger than 512x512, ever.
        S32 image_height = image_width;

        mBitmapWidth = image_width;
        mBitmapHeight = image_height;

        S32 num_components = getNumComponents(bitmap_type);
        mImageRawVec[bitmap_idx].emplace_back(new LLImageRaw(mBitmapWidth, mBitmapHeight, num_components));
        bitmap_num = static_cast<U32>(mImageRawVec[bitmap_idx].size()) - 1;

        LLImageRaw* image_raw = mImageRawVec[bitmap_idx][bitmap_num];
        if (EFontGlyphType::Grayscale == bitmap_type)
        {
            image_raw->clear(255, 0);
        }

        // Make corresponding GL image.
        mImageGLVec[bitmap_idx].emplace_back(new LLImageGL(image_raw, false, false));
        LLImageGL* image_gl = mImageGLVec[bitmap_idx][bitmap_num];

        // Track per-sheet last-used time alongside the image vectors.
        mLastUsedTime[bitmap_idx].push_back(0.0);

        // Start at beginning of the new image. 4px border guarantees that
        // the shadow shader's worst-case sample reach (2px screen dilation +
        // 2 atlas-texel sample offset = 4 atlas pixels from the glyph) lands
        // in zero-cleared territory rather than adjacent glyph data.
        mCurrentOffsetX[bitmap_idx] = 4;
        mCurrentOffsetY[bitmap_idx] = 4;
        mCurrentRowMaxHeight[bitmap_idx] = 0;

        // Attach corresponding GL texture. (*TODO: is this needed?)
        gGL.getTexUnit(0)->bind(image_gl);
        image_gl->setFilteringOption(LLTexUnit::TFO_POINT); // was setMipFilterNearest(true, true);
    }

    pos_x = mCurrentOffsetX[bitmap_idx];
    pos_y = mCurrentOffsetY[bitmap_idx];
    bitmap_num = getNumBitmaps(bitmap_type) - 1;

    mCurrentOffsetX[bitmap_idx] += width + 4;
    // Track tallest glyph placed in the current row so the next Y advance
    // clears it, even when the glyph is taller than mMaxCharHeight.
    if (height > mCurrentRowMaxHeight[bitmap_idx])
        mCurrentRowMaxHeight[bitmap_idx] = height;
    touchSheet(bitmap_type, bitmap_num);
    mGeneration++;

    return true;
}

void LLFontBitmapCache::touchSheet(EFontGlyphType bitmap_type, U32 bitmap_num) const
{
    if (bitmap_type >= EFontGlyphType::Count)
        return;

    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_num >= mLastUsedTime[bitmap_idx].size())
        return;
    mLastUsedTime[bitmap_idx][bitmap_num] = LLFrameTimer::getTotalSeconds();
}

F64 LLFontBitmapCache::getSheetLastUsedTime(EFontGlyphType bitmap_type, U32 bitmap_num) const
{
    if (bitmap_type >= EFontGlyphType::Count)
        return 0.0;
    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_num >= mLastUsedTime[bitmap_idx].size())
        return 0.0;
    return mLastUsedTime[bitmap_idx][bitmap_num];
}

bool LLFontBitmapCache::isSheetReleased(EFontGlyphType bitmap_type, U32 bitmap_num) const
{
    if (bitmap_type >= EFontGlyphType::Count)
        return false;
    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_num >= mImageRawVec[bitmap_idx].size())
        return false;
    return mImageRawVec[bitmap_idx][bitmap_num].isNull();
}

void LLFontBitmapCache::releaseSheet(EFontGlyphType bitmap_type, U32 bitmap_num)
{
    if (bitmap_type >= EFontGlyphType::Count)
        return;
    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_num >= mImageRawVec[bitmap_idx].size())
        return;
    if (mImageRawVec[bitmap_idx][bitmap_num].isNull()
        && (bitmap_num >= mImageGLVec[bitmap_idx].size()
            || mImageGLVec[bitmap_idx][bitmap_num].isNull()))
    {
        return; // Already released.
    }

    if (bitmap_num < mImageGLVec[bitmap_idx].size() && mImageGLVec[bitmap_idx][bitmap_num].notNull())
    {
        // Drop the GL texture explicitly so the GPU memory is freed promptly,
        // not on the next driver-level GC. The LLPointer reset below releases
        // the LLImageGL CPU object.
        mImageGLVec[bitmap_idx][bitmap_num]->destroyGLTexture();
        mImageGLVec[bitmap_idx][bitmap_num] = nullptr;
    }
    mImageRawVec[bitmap_idx][bitmap_num] = nullptr;

    if (bitmap_num < mLastUsedTime[bitmap_idx].size())
        mLastUsedTime[bitmap_idx][bitmap_num] = 0.0;

    mGeneration++;
}

void LLFontBitmapCache::destroyGL()
{
    for (U32 idx = 0, cnt = static_cast<U32>(EFontGlyphType::Count); idx < cnt; idx++)
    {
        for (LLImageGL* image_gl : mImageGLVec[idx])
        {
            if (image_gl)
                image_gl->destroyGLTexture();
        }
    }
}

void LLFontBitmapCache::reset()
{
    for (U32 idx = 0, cnt = static_cast<U32>(EFontGlyphType::Count); idx < cnt; idx++)
    {
        mImageRawVec[idx].clear();
        mImageGLVec[idx].clear();
        mLastUsedTime[idx].clear();
        mCurrentOffsetX[idx] = 4;
        mCurrentOffsetY[idx] = 4;
        mCurrentRowMaxHeight[idx] = 0;
    }

    mBitmapWidth = 0;
    mBitmapHeight = 0;
    mGeneration++;
}

//static
U32 LLFontBitmapCache::getNumComponents(EFontGlyphType bitmap_type)
{
    switch (bitmap_type)
    {
        case EFontGlyphType::Grayscale:
            return 2;
        case EFontGlyphType::Color:
            return 4;
        default:
            llassert(false);
            return 2;
    }
}
