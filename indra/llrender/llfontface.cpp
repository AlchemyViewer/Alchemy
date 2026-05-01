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

#include "llfontfreetype.h"  // for LLFontManager (gFontManagerp), EFontHinting, ll::fonts::LoadedFont

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <hb.h>
#include <hb-ft.h>

extern FT_Library gFTLibrary;

LLFontFace::LLFontFace()
:   mHinting(static_cast<EFontHinting>(0))
{
}

LLFontFace::~LLFontFace()
{
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
        return false;

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

    error = FT_Set_Char_Size(mFTFace,
                             0,                            // char_width in 1/64 pt
                             (S32)(point_size * 64),       // char_height in 1/64 pt
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

    return true;
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
    coords[axis_index] = (FT_Fixed)(value * 65536.0f);
    int error = FT_Set_Var_Design_Coordinates(mFTFace, num_coords, coords);

    delete[] coords;
    FT_Done_MM_Var(gFTLibrary, master);

    return error == 0;
}
