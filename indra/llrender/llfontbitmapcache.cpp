/**
 * @file llfontbitmapcache.cpp
 * @brief Storage for previously rendered glyphs.
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

#include "llgl.h"
#include "llfontbitmapcache.h"
#include "llframetimer.h"

S32 LLFontBitmapCache::sNextGeneration = 0;

LLFontBitmapCache::LLFontBitmapCache()
    : mGeneration(++sNextGeneration)
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

    // Sheet size: ~40 typical-width glyphs across, rounded up to a power of two.
    // The previous (* 20, cap 512) sizing produced 256-512 px sheets for most
    // fonts, which is too small for color emoji fonts (whose actual SBIX/SVG
    // strike bitmaps run 32-128 px tall — only ~7×7 = 49 emoji per 512² sheet,
    // and even worse with row-packing waste from variable glyph heights). At
    // 1024² we fit ~15×15 = 225 64-px emoji per sheet and still keep memory
    // reasonable (1 MB grayscale, 4 MB color, per sheet).
    S32 image_width = mMaxCharWidth * 40;
    S32 pow_iw = 2;
    while (pow_iw < image_width)
    {
        pow_iw <<= 1;
    }
    image_width = pow_iw;
    image_width = llmin(1024, image_width); // Don't make bigger than 1024x1024, ever.

    mBitmapWidth = image_width;
    mBitmapHeight = image_width;
}

LLImageRaw *LLFontBitmapCache::getImageRaw(EFontGlyphType bitmap_type, U32 bitmap_num) const
{
    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_type >= EFontGlyphType::Count || bitmap_num >= mImageRawVec[bitmap_idx].size())
        return nullptr;

    // Released slots must NOT be touched: releaseSheet zeroes the slot's
    // last-used time and the eviction sweep / recycling logic rely on it
    // staying zero. A diagnostic or defensive probe of a released sheet
    // returning null shouldn't make the slot look recently used.
    if (mImageRawVec[bitmap_idx][bitmap_num].isNull())
        return nullptr;

    touchSheet(bitmap_type, bitmap_num);
    return mImageRawVec[bitmap_idx][bitmap_num];
}

LLImageGL *LLFontBitmapCache::getImageGL(EFontGlyphType bitmap_type, U32 bitmap_num) const
{
    const U32 bitmap_idx = static_cast<U32>(bitmap_type);
    if (bitmap_type >= EFontGlyphType::Count || bitmap_num >= mImageGLVec[bitmap_idx].size())
        return nullptr;

    // Same released-slot guard as getImageRaw — null reads don't touch.
    if (mImageGLVec[bitmap_idx][bitmap_num].isNull())
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

    // mCurrentSheet is the active allocation target. If the eviction sweep
    // released it, force a fresh sheet — the in-flight pen offsets are
    // pointing into a freed image.
    const S32 active_sheet = mCurrentSheet[bitmap_idx];
    const bool active_sheet_released = (active_sheet >= 0)
                                    && mImageRawVec[bitmap_idx][active_sheet].isNull();
    bool need_new_sheet = (active_sheet < 0) || active_sheet_released;

    if (!need_new_sheet && (mCurrentOffsetX[bitmap_idx] + width + 4) > mBitmapWidth)
    {
        // Y advance must clear the tallest glyph already placed in this row.
        // Don't floor by mMaxCharHeight — for color emoji fonts the SFNT
        // FontBBox often covers the union of SBIX strike dimensions even when
        // the requested point size renders much smaller bitmaps (Twemoji's
        // bbox at 11 pt produces mMaxCharHeight ~350 while typical glyphs
        // rasterize ~16 px). Using that as a floor wastes ~330 px of vertical
        // atlas space per row. The actual placed-glyph maximum is the right
        // figure here; the new glyph's own height is what gates the row-fits
        // check below. Fallback to the new glyph's height when the row is
        // somehow "full" with nothing placed (degenerate case where the very
        // first glyph in a fresh row is wider than the atlas).
        const S32 placed_row_height = (mCurrentRowMaxHeight[bitmap_idx] > 0)
                                    ? mCurrentRowMaxHeight[bitmap_idx]
                                    : height;
        if ((mCurrentOffsetY[bitmap_idx] + placed_row_height + height + 8) > mBitmapHeight)
        {
            need_new_sheet = true;
        }
        else
        {
            // Move to next row in current image.
            mCurrentOffsetX[bitmap_idx] = 4;
            mCurrentOffsetY[bitmap_idx] += placed_row_height + 4;
            mCurrentRowMaxHeight[bitmap_idx] = 0;
        }
    }

    // Even when X didn't overflow, the placement still needs to fit the
    // sheet vertically — the row was opened earlier by a smaller glyph
    // that passed the wrap-time fit check, but a later (taller) glyph in
    // the same row can poke past mBitmapHeight if we don't re-check here.
    // Force a new sheet in that case rather than writing past the atlas.
    if (!need_new_sheet
        && (mCurrentOffsetY[bitmap_idx] + height + 4) > mBitmapHeight)
    {
        need_new_sheet = true;
    }

    if (need_new_sheet)
    {
        // We're out of space in the current image, or no image
        // has been allocated yet.  Make a new one.

        S32 image_width = mMaxCharWidth * 40;
        S32 pow_iw = 2;
        while (pow_iw < image_width)
        {
            pow_iw *= 2;
        }
        image_width = pow_iw;
        image_width = llmin(1024, image_width); // Don't make bigger than 1024x1024, ever.
        S32 image_height = image_width;

        mBitmapWidth = image_width;
        mBitmapHeight = image_height;

        // Prefer recycling a released slot over growing the sheet vectors.
        // The nullptr placeholder only existed for index stability, and the
        // purge-before-release contract (every glyph entry referencing a
        // sheet is erased before releaseSheet, which also bumps the
        // generation so captured vertex buffers rebuild) guarantees nothing
        // references the released index anymore. Without recycling, a long
        // session that cycles glyph working sets through eviction grows the
        // slot vectors monotonically.
        S32 slot = -1;
        for (size_t i = 0, n = mImageRawVec[bitmap_idx].size(); i < n; ++i)
        {
            if (mImageRawVec[bitmap_idx][i].isNull())
            {
                slot = static_cast<S32>(i);
                break;
            }
        }
        if (slot < 0)
        {
            mImageRawVec[bitmap_idx].emplace_back();
            mImageGLVec[bitmap_idx].emplace_back();
            mLastUsedTime[bitmap_idx].push_back(0.0);
            slot = static_cast<S32>(mImageRawVec[bitmap_idx].size()) - 1;
        }

        S32 num_components = getNumComponents(bitmap_type);
        mImageRawVec[bitmap_idx][slot] = new LLImageRaw(mBitmapWidth, mBitmapHeight, num_components);

        LLImageRaw* image_raw = mImageRawVec[bitmap_idx][slot];
        if (EFontGlyphType::Grayscale == bitmap_type)
        {
            image_raw->clear(255, 0);
        }
        else
        {
            // LLImageRaw doesn't zero its allocation. The BGRA sheet's
            // borders and inter-glyph gutters are sampled by the shadow
            // shader's dilated taps (and by any future linear filtering),
            // so they must be transparent black, not heap garbage.
            image_raw->clear(0, 0, 0, 0);
        }

        // Make corresponding GL image.
        mImageGLVec[bitmap_idx][slot] = new LLImageGL(image_raw, false);
        LLImageGL* image_gl = mImageGLVec[bitmap_idx][slot];

        // Fresh sheet hasn't been read or written yet.
        mLastUsedTime[bitmap_idx][slot] = 0.0;
        mCurrentSheet[bitmap_idx] = slot;

        // Start at beginning of the new image. 4px border guarantees that
        // the shadow shader's worst-case sample reach (2px screen dilation +
        // 2 atlas-texel sample offset = 4 atlas pixels from the glyph) lands
        // in zero-cleared territory rather than adjacent glyph data.
        mCurrentOffsetX[bitmap_idx] = 4;
        mCurrentOffsetY[bitmap_idx] = 4;
        mCurrentRowMaxHeight[bitmap_idx] = 0;

        // Glyph pages carry no sampling mode; LLFontGL names PointWrap at every bind.
    }

    pos_x = mCurrentOffsetX[bitmap_idx];
    pos_y = mCurrentOffsetY[bitmap_idx];
    bitmap_num = static_cast<U32>(mCurrentSheet[bitmap_idx]);

    mCurrentOffsetX[bitmap_idx] += width + 4;
    // Track tallest glyph placed in the current row so the next Y advance
    // clears it without overlapping previously placed glyphs.
    if (height > mCurrentRowMaxHeight[bitmap_idx])
        mCurrentRowMaxHeight[bitmap_idx] = height;
    touchSheet(bitmap_type, bitmap_num);
    mGeneration = ++sNextGeneration;

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

    mGeneration = ++sNextGeneration;
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
        mCurrentSheet[idx] = -1;
    }

    mBitmapWidth = 0;
    mBitmapHeight = 0;
    mGeneration = ++sNextGeneration;
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
