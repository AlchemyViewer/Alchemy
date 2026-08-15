/**
 * @file alfontface.cpp
 * @brief Refcounted FT_Face wrapper.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alfontface.h"

#include "llfontfreetype.h"   // for LLFontGlyphInfo, LLFontManager, ll::fonts::LoadedFont
#include "llfontgl.h"         // for sUseDarkEmojiPalette
#include "llfontregistry.h"   // for EFontHinting full definition
#include "llframetimer.h"     // collectGarbage throttle clock
#include "llimage.h"          // LLImageRaw, LLImageDataLock
#include "llmath.h"           // ll_round, llclamp

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
#include FT_COLOR_H

#include <hb.h>
#include <hb-ft.h>

#include <algorithm>
#include <vector>

extern FT_Library gFTLibrary;

ALFontFace::ALFontFace()
:   mHinting(static_cast<EFontHinting>(0)),
    mFontBitmapCachep(new LLFontBitmapCache)
{
}

ALFontFace::~ALFontFace()
{
    // Free LLFontGlyphInfo entries — owned by this face's cache.
    for (auto& entry : mGlyphInfoMap)
        delete entry.second;
    mGlyphInfoMap.clear();

    if (mHbFont)
    {
        // hb_ft_font_create_referenced retained mFTFace; destroying the
        // hb_font releases that reference.
        hb_font_destroy(mHbFont);
        mHbFont = nullptr;
    }
    if (mFTFace)
    {
        FT_Done_Face(mFTFace);
        mFTFace = nullptr;
    }
    delete mFontBitmapCachep;
    mFontBitmapCachep = nullptr;
}

bool ALFontFace::load(const std::string& filename, S32 face_index,
                      F32 point_size, F32 vert_dpi, F32 horz_dpi,
                      EFontHinting hinting, S32 flags,
                      const ALFontVarAxes& var_axes)
{
    llassert(!mFTFace); // load() is called once per ALFontFace instance.

    mHinting = hinting;

    FT_Open_Args openArgs;
    memset(&openArgs, 0, sizeof(openArgs));
    openArgs.memory_base = gFontManagerp->loadFont(filename, openArgs.memory_size);
    if (!openArgs.memory_base)
        return false;

    openArgs.flags = FT_OPEN_MEMORY;
    int error = FT_Open_Face(gFTLibrary, &openArgs, face_index, &mFTFace);
    if (error)
    {
        // FT_Open_Face leaves *aface undefined on failure; clear so ~ALFontFace
        // doesn't call FT_Done_Face on a garbage pointer.
        mFTFace = nullptr;
        return false;
    }

    // Native-hinted glyphs (HINTING_DEFAULT) are designed by the foundry to
    // sit on the integer pixel grid; subpixel pen position would wash out
    // the hinting. Autohinted (FORCE_AUTOHINT), light-autohinted (LIGHT),
    // and unhinted (NO_HINTING) glyphs tolerate and benefit from subpixel
    // placement. Color and SVG fonts are designed to be rendered at specific
    // sizes; subpixel positioning can cause unwanted blurring.
    mHasColor     = FT_HAS_COLOR(mFTFace);
    mHasSvg       = FT_HAS_SVG(mFTFace);
    mIsFixedWidth = (mFTFace->face_flags & FT_FACE_FLAG_FIXED_WIDTH) != 0;
    mUseSubpixelPen = !mHasColor && !mHasSvg && (hinting != EFontHinting::DEFAULT);

    // COLRv1 probe: read the first 2 bytes of the COLR table and check the
    // version field. FreeType's FT_LOAD_COLOR + FT_Render_Glyph rasterize
    // COLRv0 directly but explicitly do NOT rasterize COLRv1 (per ftcolor.h
    // notes); a separate paint walker handles those. Distinguishing here
    // lets renderGlyph branch without re-probing the table on every glyph.
    {
        FT_ULong length = 2;
        FT_Byte  buf[2] = { 0, 0 };
        if (FT_Load_Sfnt_Table(mFTFace, TTAG_COLR, 0, buf, &length) == 0 && length >= 2)
        {
            const U16 version = (static_cast<U16>(buf[0]) << 8) | buf[1];
            mHasColrV1 = (version >= 1);
        }
    }

    // CPAL palette pick. Default to palette 0 (the font's primary palette).
    // When EmojiUseDarkPalette is on and the face actually carries a palette
    // flagged for dark backgrounds, prefer the first such palette; otherwise
    // fall back to 0 even when the setting is on (a font may not ship a
    // dark variant). Computed once at load and cached so the per-glyph
    // rasterize path doesn't re-walk palette flags.
    if (mHasColor && LLFontGL::sUseDarkEmojiPalette)
    {
        FT_Palette_Data palette_data = {};
        if (FT_Palette_Data_Get(mFTFace, &palette_data) == 0
            && palette_data.palette_flags != nullptr)
        {
            for (FT_UShort i = 0; i < palette_data.num_palettes; ++i)
            {
                if (palette_data.palette_flags[i] & FT_PALETTE_FOR_DARK_BACKGROUND)
                {
                    mPaletteIndex = i;
                    break;
                }
            }
        }
    }

    // OpenType variation axes. Each axis is independently gated by its
    // *_set flag in var_axes; an unset axis is silently skipped (and
    // setVariationAxis itself silently no-ops on faces that lack the
    // requested tag, so non-variable fonts pass through cleanly).
    //
    // opsz is the one exception to the "explicit only" rule: when no
    // value is supplied, fall back to the rendered point_size so faces
    // like Inter automatically pick up the design's optical adjustment
    // at small / large sizes. Registry callers that want to pin opsz
    // can supply font_optical_size in fonts.xml.
    if (var_axes.wght_set)
        mWghtAxisSet = setVariationAxis("wght", var_axes.wght);
    if (var_axes.opsz_set)
        mOpszAxisSet = setVariationAxis("opsz", var_axes.opsz);
    else
        mOpszAxisSet = setVariationAxis("opsz", point_size);
    if (var_axes.ital_set)
        mItalAxisSet = setVariationAxis("ital", var_axes.ital);
    if (var_axes.wdth_set)
        mWdthAxisSet = setVariationAxis("wdth", var_axes.wdth);
    if (var_axes.slnt_set)
        mSlntAxisSet = setVariationAxis("slnt", var_axes.slnt);

    // Round-to-nearest into 26.6: a plain (S32) cast truncates toward zero,
    // which loses up to ~1/64 pt of precision for non-integer point sizes
    // (e.g. LSmall=8.1 -> 8.1 * 64 = 518.4 -> would truncate to 518 instead
    // of rounding to 518).
    error = FT_Set_Char_Size(mFTFace,
                             0,                                  // char_width in 1/64 pt
                             ll_round(point_size * 64.f),        // char_height in 1/64 pt
                             (U32)horz_dpi,
                             (U32)vert_dpi);
    if (error)
    {
        FT_Done_Face(mFTFace);
        mFTFace = nullptr;
        return false;
    }

    // FT_Set_Char_Size runs once per ALFontFace lifetime. The hb_font_t
    // (lazy in getHbFont) snapshots size->metrics at creation and uses
    // its ppem/scale for subsequent shape calls; resizing the face after
    // this point would require hb_ft_font_changed(mHbFont) to resync HB.
    // New sized state goes through a fresh ALFontFace — the registry's
    // face cache enforces this, since point_size/DPI are part of the
    // ALFontFaceKey. Snapshot ppem so getHbFont can assert the invariant.
    mLoadedXPpem = mFTFace->size->metrics.x_ppem;
    mLoadedYPpem = mFTFace->size->metrics.y_ppem;

    // Prefer Unicode cmap explicitly. FT's auto-pick prefers Unicode when
    // present, but a font whose first cmap is non-Unicode (Apple Roman,
    // Symbol, Mac legacy) would otherwise drive every FT_Get_Char_Index to
    // a wrong-or-zero glyph for non-ASCII codepoints. Both FT and HB share
    // the cmap, so the failure is consistent — and consistently wrong.
    // Fall back to the existing first-charmap pick (with a warning) when
    // the font has no Unicode cmap at all.
    if (FT_Select_Charmap(mFTFace, FT_ENCODING_UNICODE) != 0)
    {
        LL_WARNS("Font") << "No Unicode cmap in " << filename
            << "; non-ASCII glyph lookups may be wrong" << LL_ENDL;
        if (!mFTFace->charmap && mFTFace->num_charmaps > 0)
        {
            FT_Set_Charmap(mFTFace, mFTFace->charmaps[0]);
        }
    }

    // Size the bitmap atlas from the just-set face metrics. Same calculation
    // as the legacy LLFontFreetype::loadFace did, just on the wrapper now.
    F32 pixels_per_em   = (point_size / 72.f) * vert_dpi;
    F32 ems_per_unit    = 1.f / mFTFace->units_per_EM;
    F32 pixels_per_unit = pixels_per_em * ems_per_unit;

    F32 y_max = mFTFace->bbox.yMax * pixels_per_unit;
    F32 y_min = mFTFace->bbox.yMin * pixels_per_unit;
    F32 x_max = mFTFace->bbox.xMax * pixels_per_unit;
    F32 x_min = mFTFace->bbox.xMin * pixels_per_unit;
    S32 max_char_width  = ll_round(0.5f + (x_max - x_min));
    S32 max_char_height = ll_round(0.5f + (y_max - y_min));
    mFontBitmapCachep->init(max_char_width, max_char_height);

    return true;
}

LLFontGlyphInfo* ALFontFace::findGlyphInfo(U32 glyph_index, EFontGlyphType type) const
{
    auto range = mGlyphInfoMap.equal_range(glyph_index);
    auto iter = (type != EFontGlyphType::Unspecified)
        ? std::find_if(range.first, range.second,
            [type](const glyph_info_map_t::value_type& e) { return e.second->mGlyphType == type; })
        : range.first;
    return (iter != range.second) ? iter->second : nullptr;
}

LLFontGlyphInfo* ALFontFace::insertGlyphInfo(U32 glyph_index, LLFontGlyphInfo* gi) const
{
    llassert(gi->mGlyphType < EFontGlyphType::Count);
    auto range = mGlyphInfoMap.equal_range(glyph_index);
    auto iter = std::find_if(range.first, range.second,
        [gi](const glyph_info_map_t::value_type& e) { return e.second->mGlyphType == gi->mGlyphType; });
    if (iter != range.second)
    {
        // Keep the already-published entry — pointers to it may be live up
        // the stack, and swapping it out would free memory in active use.
        // Reaching here means an upstream dedup probe was skipped; the
        // duplicate's atlas slots stay orphaned (we have no slot-level
        // reclaim), which is a leak of atlas space but not of memory safety.
        llassert(false);
        delete gi;
        return iter->second;
    }
    mGlyphInfoMap.insert(std::make_pair(glyph_index, gi));
    return gi;
}

void ALFontFace::resetBitmapCache()
{
    for (auto& entry : mGlyphInfoMap)
        delete entry.second;
    mGlyphInfoMap.clear();
    if (mFontBitmapCachep)
        mFontBitmapCachep->reset();
}

void ALFontFace::collectGarbage() const
{
    if (!mFTFace || !mFontBitmapCachep)
        return;

    // Sweep cadence: cheap enough to run at the top of every frame, with
    // GC_INTERVAL_SEC bounding actual work. Idle threshold sized for "real
    // user idle" — roughly the time after which a chat scrollback or panel of
    // unique-codepoint text has stopped being displayed. Long enough not to
    // churn during normal interaction; short enough that an hour-long session
    // doesn't accumulate every transient code page ever shown.
    constexpr F64 GC_INTERVAL_SEC      = 5.0;
    constexpr F64 IDLE_THRESHOLD_SEC   = 60.0 * 15.0;

    const F64 now = LLFrameTimer::getTotalSeconds();
    if (now < mNextGcTime)
        return;
    mNextGcTime = now + GC_INTERVAL_SEC;

    auto glyph_uses_sheet = [](const LLFontGlyphInfo* gi, EFontGlyphType type, U32 num) -> bool
    {
        for (U8 p = 0; p < gi->mPhaseCount; ++p)
        {
            const auto& entry = gi->mPhaseSlots[p].mBitmapEntry;
            if (entry.first == type && entry.second >= 0 && static_cast<U32>(entry.second) == num)
                return true;
        }
        return false;
    };

    // Shaped runs in ALFontShaping's cache hold only metric/glyph_id data — no
    // atlas references — so they survive eviction; getGlyphInfoByIndex on the
    // next frame re-rasterizes whichever glyphs were dropped here. Cache
    // generation bumps inside releaseSheet so LLFontVertexBuffer rebuilds.
    for (U32 t = 0; t < static_cast<U32>(EFontGlyphType::Count); ++t)
    {
        const EFontGlyphType type = static_cast<EFontGlyphType>(t);
        const U32 sheet_count = mFontBitmapCachep->getNumBitmaps(type);
        for (U32 num = 0; num < sheet_count; ++num)
        {
            if (mFontBitmapCachep->isSheetReleased(type, num))
                continue;
            const F64 last_used = mFontBitmapCachep->getSheetLastUsedTime(type, num);
            // last_used == 0 means the sheet was allocated but not yet drawn
            // from — skip it for one cycle so a brand-new sheet gets at least
            // a frame to be touched before it's a candidate.
            if (last_used <= 0.0)
                continue;
            if ((now - last_used) <= IDLE_THRESHOLD_SEC)
                continue;

            // Delete the glyph entries that reference this sheet, then
            // release the sheet itself. There is no head-side cache to
            // invalidate: getGlyphInfoByIndex routes every lookup through
            // findGlyphInfo here, so every freetype sharing this face
            // observes the deletion on its next render and re-rasterizes.
            auto matches = [&](const LLFontGlyphInfo* gi) { return glyph_uses_sheet(gi, type, num); };
            erase_glyph_entries(matches);

            mFontBitmapCachep->releaseSheet(type, num);
        }
    }
}

void ALFontFace::destroyGlyphInfo(LLFontGlyphInfo* gi)
{
    delete gi;
}

void ALFontFace::destroyGL()
{
    // Tear down GL textures up front for prompt GPU memory release, then
    // fully reset the face so any later loadFace() that resolves back to
    // this same ALFontFace via LLFontManager::mFaceCache (e.g. UI scale
    // change whose new vert/horz DPI floor-round to the same integers as
    // before) gets back an empty face equivalent to a freshly-constructed
    // one. Without the reset, stale mGlyphInfoMap entries continue to
    // point at atlas slots in zombie LLImageGLs (CPU object alive, GL
    // name 0), every bind() falls through to sDefaultGLTexture, and text
    // renders as solid colored rectangles.
    if (mFontBitmapCachep)
        mFontBitmapCachep->destroyGL();
    resetBitmapCache();
}

bool ALFontFace::setSubImageBGRA(U32 x, U32 y, U32 bitmap_num,
                                 U16 width, U16 height,
                                 const U8* data, S32 stride) const
{
    LLImageRaw* image_raw = mFontBitmapCachep ? mFontBitmapCachep->getImageRaw(EFontGlyphType::Color, bitmap_num) : nullptr;
    if (!image_raw)
    {
        llassert(false);
        return false;
    }
    llassert(image_raw->getComponents() == 4);

    // Inspired by LLImageRaw::setSubImage(); copy + ARGB swizzle.
    // image_raw->getData() is a U8* buffer; storing pixels through a U32*
    // alias is strict-aliasing UB. Go through the byte cursor + memcpy
    // (folded to a single 32-bit store by both GCC and Clang).
    U8* const image_data = image_raw->getData();
    if (!image_data)
        return false;

    // FreeType convention: `data` points to the first row in DRAW order
    // (top of the glyph). `stride` is the signed byte offset to the next
    // row down; FT may hand us a negative pitch when the buffer is laid
    // out bottom-up, in which case `data` sits at a higher address than
    // the rest of the buffer. Use signed arithmetic so both pitch signs
    // land on the right source bytes.
    for (U32 idxRow = 0; idxRow < height; idxRow++)
    {
        // Atlas is GL bottom-up (memory row 0 = texture bottom). Map atlas
        // memory row `idxRow` to draw-order source row (height-1-idxRow)
        // so source-top lands at atlas-top visually.
        const ptrdiff_t src_row    = (ptrdiff_t)(height - 1 - idxRow);
        const ptrdiff_t nSrcOffset = src_row * (ptrdiff_t)stride;
        const U32 nDstOffset = (y + idxRow) * image_raw->getWidth() + x;

        for (U32 idxCol = 0; idxCol < width; idxCol++)
        {
            const ptrdiff_t nTemp = nSrcOffset + (ptrdiff_t)idxCol * 4;
            const U32 pixel = data[nTemp + 3] << 24 | data[nTemp] << 16 | data[nTemp + 1] << 8 | data[nTemp + 2];
            std::memcpy(image_data + (nDstOffset + idxCol) * sizeof(U32), &pixel, sizeof(U32));
        }
    }

    return true;
}

void ALFontFace::setSubImageLuminanceAlpha(U32 x, U32 y, U32 bitmap_num,
                                           U32 width, U32 height,
                                           U8* data, S32 stride) const
{
    LLImageRaw* image_raw = mFontBitmapCachep ? mFontBitmapCachep->getImageRaw(EFontGlyphType::Grayscale, bitmap_num) : nullptr;
    if (!image_raw)
    {
        llassert(false);
        return;
    }

    LLImageDataLock lock(image_raw);

    llassert(image_raw->getComponents() == 2);

    U8* target = image_raw->getData();
    llassert(target);
    if (!data || !target)
        return;

    if (0 == stride)
        stride = width;

    // Hard bounds check: nextOpenPos is supposed to guarantee that the
    // chosen (x, y, width, height) box fits inside the just-allocated
    // sheet, but a regression in pen/sheet accounting (e.g. mBitmapWidth
    // / mBitmapHeight unset because mMaxCharWidth was 0 at allocation
    // time, or a glyph taller than the sheet) silently produces an
    // out-of-range write here that corrupts adjacent heap allocations
    // and crashes a few frames later. Bail with a once-per-process WARN
    // and the offending dimensions so the underlying bug is diagnosable
    // from logs instead of presenting as a stack-walked OOB write.
    const U32 target_width  = image_raw->getWidth();
    const U32 target_height = image_raw->getHeight();
    if (x + width > target_width || y + height > target_height)
    {
        LL_WARNS_ONCE("Font") << "setSubImageGrayscale OOB: "
                              << "atlas=" << target_width << "x" << target_height
                              << ", components=" << (S32)image_raw->getComponents()
                              << ", glyph=" << width << "x" << height
                              << " at (" << x << "," << y
                              << "), bitmap_num=" << bitmap_num
                              << ", mBitmapWidth=" << mFontBitmapCachep->getBitmapWidth()
                              << ", mBitmapHeight=" << mFontBitmapCachep->getBitmapHeight()
                              << ", mMaxCharWidth=" << mFontBitmapCachep->getMaxCharWidth()
                              << LL_ENDL;
        llassert(false);
        return;
    }

    // gating the alpha. FT's bitmap.buffer points to the first row in draw
    // order (top); stride is the signed byte offset to the next row down,
    // so a negative stride means buffer sits at a higher address than the
    // rest of the data. Use signed arithmetic for from_offset so both pitch
    // gating the alpha. Source data is bottom-up (FreeType convention), so
    // walk source rows in reverse.
    for (U32 i = 0; i < height; i++)
    {
        U32 to_offset = (y + i) * target_width + x;
        ptrdiff_t from_offset = (ptrdiff_t)(height - 1 - i) * (ptrdiff_t)stride;
        for (U32 j = 0; j < width; j++)
        {
            *(target + to_offset * 2 + 1) = *(data + from_offset);
            to_offset++;
            from_offset++;
        }
    }
}

U32 ALFontFace::getCharGlyphIndex(llwchar wch) const
{
    if (!mFTFace)
        return 0;

    auto [it, inserted] = mCharIndexCache.try_emplace(wch, 0);
    if (inserted)
    {
        it->second = static_cast<U32>(FT_Get_Char_Index(mFTFace, wch));
    }
    return it->second;
}

hb_font_t* ALFontFace::getHbFont() const
{
    if (!mHbFont && mFTFace)
    {
        // FT face must still be at the size load() set. If anything resized
        // it after load (without also dropping mHbFont), hb_ft_font_create
        // would snapshot the wrong ppem/scale and HB advances would silently
        // drift from FT advances. Cheap two-integer check; debug-only.
        llassert(mFTFace->size->metrics.x_ppem == mLoadedXPpem
              && mFTFace->size->metrics.y_ppem == mLoadedYPpem);

        // hb_ft_font_create_referenced retains mFTFace for the lifetime of
        // the hb_font; the FT_Face won't be freed before the hb_font is.
        // ~ALFontFace destroys the hb_font first then calls FT_Done_Face.
        mHbFont = hb_ft_font_create_referenced(mFTFace);
        if (mHbFont)
        {
            // Mirror FT's variation axis state into HB. hb_ft_font_create_*
            // does NOT propagate var coords; without this, HB's GSUB/GPOS
            // (ItemVariationStore lookups) run at the font's default axis
            // values even when FT renders the varied outlines correctly.
            // Visible on variation-aware kerning in fonts like Inter at
            // wght=600. Outlines stay correct because HB queries them via
            // FT callbacks; only HB-internal OT lookups need this.
            FT_MM_Var* mm = nullptr;
            if (FT_Get_MM_Var(mFTFace, &mm) == 0 && mm)
            {
                FT_UInt num_axis = mm->num_axis;
                if (num_axis > 0)
                {
                    std::vector<FT_Fixed> ft_coords(num_axis);
                    if (FT_Get_Var_Design_Coordinates(mFTFace, num_axis, ft_coords.data()) == 0)
                    {
                        std::vector<float> hb_coords(num_axis);
                        for (FT_UInt i = 0; i < num_axis; ++i)
                        {
                            // 16.16 fixed -> design-space float. HB's design
                            // coords are in the same units FT exposes.
                            hb_coords[i] = ft_coords[i] / 65536.0f;
                        }
                        hb_font_set_var_coords_design(mHbFont, hb_coords.data(), num_axis);
                    }
                }
                FT_Done_MM_Var(gFTLibrary, mm);
            }

            // Hinting choice for measurement-time outline loads inside hb-ft.
            // Drawing-time loads in renderGlyph build their own load_flags;
            // those override these. EFontHinting's bit pattern is laid out to
            // be a valid FT_LOAD_* flag composite (see EFontHinting comments).
            hb_ft_font_set_load_flags(mHbFont, static_cast<int>(mHinting));
        }
    }
    return mHbFont;
}

bool ALFontFace::setVariationAxis(const std::string& axis_tag, F32 value)
{
    if (!mFTFace || axis_tag.size() < 4)
        return false;

    FT_MM_Var* master = nullptr;
    if (FT_Get_MM_Var(mFTFace, &master) != 0)
    {
        // Not a variable font — silently skip.
        return false;
    }

    FT_UInt axis_index = 0;
    bool found = false;
    for (FT_UInt i = 0; i < master->num_axis; i++)
    {
        if (master->axis[i].tag == FT_MAKE_TAG(axis_tag[0], axis_tag[1], axis_tag[2], axis_tag[3]))
        {
            axis_index = i;
            found = true;

            F32 min_val = master->axis[i].minimum / 65536.0f;
            F32 max_val = master->axis[i].maximum / 65536.0f;
            value = llclamp(value, min_val, max_val);
            break;
        }
    }

    if (!found)
    {
        FT_Done_MM_Var(gFTLibrary, master);
        return false;
    }

    FT_UInt num_coords = master->num_axis;
    FT_Fixed* coords = new FT_Fixed[num_coords];
    FT_Get_Var_Design_Coordinates(mFTFace, num_coords, coords);
    // Round-to-nearest into 16.16. Plain (FT_Fixed) cast truncates, which
    // loses sub-unit precision for non-integer axis values (e.g. an opsz
    // axis driven by a fractional point size).
    coords[axis_index] = (FT_Fixed)ll_round(value * 65536.0f);
    int error = FT_Set_Var_Design_Coordinates(mFTFace, num_coords, coords);

    delete[] coords;
    FT_Done_MM_Var(gFTLibrary, master);

    return error == 0;
}
