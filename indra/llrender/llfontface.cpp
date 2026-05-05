/**
 * @file llfontface.cpp
 * @brief Refcounted FT_Face wrapper.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llfontface.h"

#include "llfontfreetype.h"   // for LLFontGlyphInfo, LLFontManager, ll::fonts::LoadedFont
#include "llfontregistry.h"   // for EFontHinting full definition
#include "llimage.h"          // LLImageRaw, LLImageDataLock
#include "llmath.h"           // ll_round, llclamp

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <hb.h>
#include <hb-ft.h>

#include <algorithm>

extern FT_Library gFTLibrary;

LLFontFace::LLFontFace()
:   mHinting(static_cast<EFontHinting>(0)),
    mFontBitmapCachep(new LLFontBitmapCache)
{
}

LLFontFace::~LLFontFace()
{
    // Free LLFontGlyphInfo entries — owned by this face's caches.
    for (auto& entry : mCharGlyphInfoMap)
        delete entry.second;
    mCharGlyphInfoMap.clear();
    for (auto& entry : mShapedGlyphInfoMap)
        delete entry.second;
    mShapedGlyphInfoMap.clear();

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

bool LLFontFace::load(const std::string& filename, S32 face_index,
                      F32 point_size, F32 vert_dpi, F32 horz_dpi,
                      S32 weight, EFontHinting hinting, S32 flags)
{
    llassert(!mFTFace); // load() is called once per LLFontFace instance.

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
        // FT_Open_Face leaves *aface undefined on failure; clear so ~LLFontFace
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

    if (weight >= 0)
    {
        mWghtAxisSet = setVariationAxis("wght", static_cast<F32>(weight));
        // For Inter (and any other variable face exposing opsz), set the
        // optical-size axis from the point size so glyph design adapts to
        // its rendered size.
        setVariationAxis("opsz", point_size);
    }

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

    if (!mFTFace->charmap)
    {
        FT_Set_Charmap(mFTFace, mFTFace->charmaps[0]);
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

LLFontGlyphInfo* LLFontFace::findGlyphInfo(llwchar wch, EFontGlyphType type) const
{
    auto range = mCharGlyphInfoMap.equal_range(wch);
    auto iter = (type != EFontGlyphType::Unspecified)
        ? std::find_if(range.first, range.second,
            [type](const char_glyph_info_map_t::value_type& e) { return e.second->mGlyphType == type; })
        : range.first;
    return (iter != range.second) ? iter->second : nullptr;
}

void LLFontFace::insertGlyphInfo(llwchar wch, LLFontGlyphInfo* gi) const
{
    llassert(gi->mGlyphType < EFontGlyphType::Count);
    auto range = mCharGlyphInfoMap.equal_range(wch);
    auto iter = std::find_if(range.first, range.second,
        [gi](const char_glyph_info_map_t::value_type& e) { return e.second->mGlyphType == gi->mGlyphType; });
    if (iter != range.second)
    {
        delete iter->second;
        iter->second = gi;
    }
    else
    {
        mCharGlyphInfoMap.insert(std::make_pair(wch, gi));
    }
}

LLFontGlyphInfo* LLFontFace::findShapedGlyphInfo(U32 glyph_index, EFontGlyphType type) const
{
    auto range = mShapedGlyphInfoMap.equal_range(glyph_index);
    auto iter = (type != EFontGlyphType::Unspecified)
        ? std::find_if(range.first, range.second,
            [type](const shaped_glyph_info_map_t::value_type& e) { return e.second->mGlyphType == type; })
        : range.first;
    return (iter != range.second) ? iter->second : nullptr;
}

void LLFontFace::insertShapedGlyphInfo(U32 glyph_index, LLFontGlyphInfo* gi) const
{
    llassert(gi->mGlyphType < EFontGlyphType::Count);
    auto range = mShapedGlyphInfoMap.equal_range(glyph_index);
    auto iter = std::find_if(range.first, range.second,
        [gi](const shaped_glyph_info_map_t::value_type& e) { return e.second->mGlyphType == gi->mGlyphType; });
    if (iter != range.second)
    {
        delete iter->second;
        iter->second = gi;
    }
    else
    {
        mShapedGlyphInfoMap.insert(std::make_pair(glyph_index, gi));
    }
}

void LLFontFace::resetBitmapCache()
{
    for (auto& entry : mCharGlyphInfoMap)
        delete entry.second;
    mCharGlyphInfoMap.clear();
    for (auto& entry : mShapedGlyphInfoMap)
        delete entry.second;
    mShapedGlyphInfoMap.clear();
    if (mFontBitmapCachep)
        mFontBitmapCachep->reset();
}

void LLFontFace::destroyGL()
{
    // Tear down GL textures up front for prompt GPU memory release, then
    // fully reset the face so any later loadFace() that resolves back to
    // this same LLFontFace via LLFontManager::mFaceCache (e.g. UI scale
    // change whose new vert/horz DPI floor-round to the same integers as
    // before) gets back an empty face equivalent to a freshly-constructed
    // one. Without the reset, stale mCharGlyphInfoMap / mShapedGlyphInfoMap
    // entries continue to point at atlas slots in zombie LLImageGLs (CPU
    // object alive, GL name 0), every bind() falls through to
    // sDefaultGLTexture, and text renders as solid colored rectangles.
    // Heads must clear their non-owning resolution caches BEFORE this
    // runs (LLFontFreetype::destroyGL handles that) — the deletes below
    // would otherwise leave dangling pointers in those maps.
    if (mFontBitmapCachep)
        mFontBitmapCachep->destroyGL();
    resetBitmapCache();
}

bool LLFontFace::setSubImageBGRA(U32 x, U32 y, U32 bitmap_num,
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
    U32* image_data = (U32*)image_raw->getData();
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
            image_data[nDstOffset + idxCol] = data[nTemp + 3] << 24 | data[nTemp] << 16 | data[nTemp + 1] << 8 | data[nTemp + 2];
        }
    }

    return true;
}

void LLFontFace::setSubImageLuminanceAlpha(U32 x, U32 y, U32 bitmap_num,
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

U32 LLFontFace::getCharGlyphIndex(llwchar wch) const
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

hb_font_t* LLFontFace::getHbFont() const
{
    if (!mHbFont && mFTFace)
    {
        // hb_ft_font_create_referenced retains mFTFace for the lifetime of
        // the hb_font; the FT_Face won't be freed before the hb_font is.
        // ~LLFontFace destroys the hb_font first then calls FT_Done_Face.
        mHbFont = hb_ft_font_create_referenced(mFTFace);
        if (mHbFont)
        {
            // Hinting choice for measurement-time outline loads inside hb-ft.
            // Drawing-time loads in renderGlyph build their own load_flags;
            // those override these. EFontHinting's bit pattern is laid out to
            // be a valid FT_LOAD_* flag composite (see EFontHinting comments).
            hb_ft_font_set_load_flags(mHbFont, static_cast<int>(mHinting));
        }
    }
    return mHbFont;
}

bool LLFontFace::setVariationAxis(const std::string& axis_tag, F32 value)
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
