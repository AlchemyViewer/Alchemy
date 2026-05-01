/**
 * @file llfontfreetype.cpp
 * @brief Freetype font library wrapper
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "llfontfreetype.h"
#include "llfontgl.h"

// Freetype stuff
#include <plutosvg-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_MULTIPLE_MASTERS_H

// Harfbuzz
#include <hb.h>
#include <hb-ft.h>

#include "llfontshaping.h"

#include "lldir.h"
#include "llerror.h"
#include "llframetimer.h"
#include "llimage.h"
#include "llimagepng.h"
//#include "llimagej2c.h"
#include "llmath.h" // Linden math
#include "llstring.h"
//#include "imdebug.h"
#include "llfontbitmapcache.h"
#include "llgl.h"

#define ENABLE_OT_SVG_SUPPORT

FT_Render_Mode gFontRenderMode = FT_RENDER_MODE_NORMAL;

LLFontManager *gFontManagerp = nullptr;

FT_Library gFTLibrary = nullptr;

//static
void LLFontManager::initClass()
{
    if (!gFontManagerp)
    {
        gFontManagerp = new LLFontManager;
    }
}

//static
void LLFontManager::cleanupClass()
{
    delete gFontManagerp;
    gFontManagerp = nullptr;
}

LLFontManager::LLFontManager()
{
    LL_INFOS() << "Harfbuzz version: " << hb_version_string() << LL_ENDL;

    int error;
    error = FT_Init_FreeType(&gFTLibrary);
    if (error)
    {
        // Clean up freetype libs.
        LL_ERRS() << "Freetype initialization failure!" << LL_ENDL;
        FT_Done_FreeType(gFTLibrary);
    }

    FT_Int major, minor, patch;
    FT_Library_Version(gFTLibrary, &major, &minor, &patch);
    LL_INFOS() << "Freetype version: " << major << "." << minor << "." << patch << LL_ENDL;

#ifdef ENABLE_OT_SVG_SUPPORT
    FT_Property_Set(gFTLibrary, "ot-svg", "svg-hooks", &plutosvg_ft_hooks);
#endif
}

LLFontManager::~LLFontManager()
{
    // Order matters: cached LLFontFace instances hold FT_Face pointers
    // owned by gFTLibrary. Tearing those down (FT_Done_Face) requires the
    // library to still be alive, so drop the face cache (and bytes) first
    // and only then destroy the library.
    unloadAllFonts();
    FT_Done_FreeType(gFTLibrary);
    gFTLibrary = nullptr;
}


LLFontGlyphInfo::LLFontGlyphInfo(U32 index, EFontGlyphType glyph_type)
:   mGlyphIndex(index),
    mGlyphType(glyph_type),
    mWidth(0),          // In pixels
    mHeight(0),         // In pixels
    mXAdvance(0.f),     // In pixels
    mYAdvance(0.f),     // In pixels
    mXBearing(0),       // Distance from baseline to left in pixels
    mYBearing(0),       // Distance from baseline to top in pixels
    mLsbDelta(0),
    mRsbDelta(0),
    mPhaseCount(1)
{
    // mPhaseSlots default-construct (zeroed PhaseSlot per element).
}

LLFontGlyphInfo::LLFontGlyphInfo(const LLFontGlyphInfo& fgi)
    : mGlyphIndex(fgi.mGlyphIndex)
    , mGlyphType(fgi.mGlyphType)
    , mWidth(fgi.mWidth)
    , mHeight(fgi.mHeight)
    , mXAdvance(fgi.mXAdvance)
    , mYAdvance(fgi.mYAdvance)
    , mXBearing(fgi.mXBearing)
    , mYBearing(fgi.mYBearing)
    , mLsbDelta(fgi.mLsbDelta)
    , mRsbDelta(fgi.mRsbDelta)
    , mPhaseSlots(fgi.mPhaseSlots)
    , mPhaseCount(fgi.mPhaseCount)
{
}

LLFontFreetype::LLFontFreetype()
:   mAscender(0.f),
    mDescender(0.f),
    mLineHeight(0.f),
    mIsFallback(false),
    mHinting(EFontHinting::FORCE_AUTOHINT),
    mRenderGlyphCount(0),
    mStyle(0),
    mPointSize(0)
{
}


LLFontFreetype::~LLFontFreetype()
{
    // The shape cache holds LLShapedGlyph entries keyed on
    // (LLFontFreetype*, glyph_index); ours are about to dangle.
    LLFontShaping::clearCache();

    // Per-head resolution caches hold non-owning pointers into face-owned
    // glyph info entries. Just clear; entries are deleted by ~LLFontFace.
    mCharGlyphInfoMap.clear();
    mShapedGlyphInfoMap.clear();

    // mFace's FT_Face, hb_font_t, and atlas lifetime is owned by the
    // LLFontFace wrapper — releasing the LLPointer here lets the wrapper
    // drop its refcount; FT_Done_Face and atlas teardown fire when the
    // last LLFontFreetype that referenced it is destroyed.
    mFace = nullptr;

    // mFallbackFonts cleaned up by LLPointer destructor.
}

hb_font_t* LLFontFreetype::getHbFont() const
{
    return mFace ? mFace->getHbFont() : nullptr;
    // We deliberately do NOT override HarfBuzz's glyph_h_advance_func to
    // apply the autohinter rsb/lsb correction that getXKerning uses for
    // the legacy codepoint path. GPOS positioning emitted by HB supersedes
    // that correction's purpose, and threading it into HB's stateless
    // per-glyph callback would require a per-shaper "previous slot" cache
    // that doesn't fit the HB API model.
}

bool LLFontFreetype::isFixedWidth() const
{
    return mFace && mFace->isFixedWidth();
}

const LLFontFreetype* LLFontFreetype::selectShapingFace(llwchar base, U32& out_glyph_index) const
{
    out_glyph_index = 0;
    if (!getFTFace())
        return this;

    llassert(!mIsFallback);

    if (auto it = mShapingFaceResolution.find(base); it != mShapingFaceResolution.end())
    {
        out_glyph_index = it->second.second;
        return it->second.first;
    }

    // Shaping runs are emoji presentation (the detector only flags
    // sequences with ZWJ/VS16/skin-tone/keycap/tag/flag-pair), so try the
    // emoji-functor fallbacks first, ignoring the functor gate. This is
    // what gives:
    //
    //   * BMP pictographs like ❤ (U+2764) access to the emoji font's
    //     composed ZWJ form (❤️‍🔥), which the root face's mono glyph
    //     would otherwise steal.
    //
    //   * Keycap sequences (digit/#/* + FE0F + 20E3) the emoji font's
    //     GSUB-composed keycap glyph. Routing keycaps through the root
    //     face works for '8' but leaves U+20E3 as a notdef box because
    //     text fonts generally don't carry it, and HarfBuzz can't shop
    //     individual glyphs out to different faces mid-sequence.
    auto resolve = [&]() -> std::pair<const LLFontFreetype*, U32>
    {
        const size_t count = mFallbackFonts.size();
        for (size_t i = 0; i < count; ++i)
        {
            const fallback_font_t& pair = mFallbackFonts[i];
            if (!pair.second)
                continue;
            U32 gi = pair.first->getCharGlyphIndex(base);
            if (gi)
                return { pair.first.get(), gi };
        }
        // No emoji fallback has the base — fall back to the root face.
        if (U32 gi = getCharGlyphIndex(base); gi != 0)
            return { this, gi };
        // Monochrome fallbacks.
        for (size_t i = 0; i < count; ++i)
        {
            const fallback_font_t& pair = mFallbackFonts[i];
            if (pair.second)
                continue;
            U32 gi = pair.first->getCharGlyphIndex(base);
            if (gi)
                return { pair.first.get(), gi };
        }
        // No face has a glyph; caller will end up rendering a tofu via `this`.
        return { this, 0u };
    };

    const auto result = resolve();
    mShapingFaceResolution.emplace(base, result);
    out_glyph_index = result.second;
    return result.first;
}

bool LLFontFreetype::faceHasGlyph(llwchar wch) const
{
    if (!getFTFace())
        return false;
    return getCharGlyphIndex(wch) != 0;
}

U32 LLFontFreetype::getCharGlyphIndex(llwchar wch) const
{
    return mFace ? mFace->getCharGlyphIndex(wch) : 0;
}

bool LLFontFreetype::loadFace(const std::string& filename, F32 point_size, F32 vert_dpi, F32 horz_dpi, S32 weight, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags)
{
    if (mFace)
    {
        // Reload: cached shape entries depend on the previous face state,
        // and our resolution cache holds entries from the old fallback chain.
        LLFontShaping::clearCache();
    }
    mShapingFaceResolution.clear();
    // Per-head resolution caches hold non-owning pointers into face-owned
    // entries. resetBitmapCache (called from reset()) clears them; in the
    // initial-load path they're already empty.
    mCharGlyphInfoMap.clear();
    mShapedGlyphInfoMap.clear();

    // Resolve the (file, sized + variable axis) state via the manager's
    // shared face cache. Heads and fallbacks alike consult the cache; same
    // params yield the same wrapper.
    LLFontFaceKey key{filename, face_n, point_size, vert_dpi, horz_dpi, weight, hinting, flags};
    mFace = gFontManagerp->getOrCreateFace(key);
    if (!mFace || !mFace->isValid())
    {
        mFace = nullptr;
        return false;
    }

    LLFT_Face ft = mFace->face();

    mIsFallback = is_fallback;
    mHinting    = hinting;
    mFontFlags  = flags;
    mWeight     = weight;
    mUseSubpixelPen = mFace->useSubpixelPen();

    // Per-instance derived metrics. The face wrapper has FT_Set_Char_Size
    // baked in for these (size, dpi) values; the metrics fall out of the
    // sized face's units_per_EM and bbox.
    F32 pixels_per_em = (point_size / 72.f) * vert_dpi;
    F32 ems_per_unit = 1.f / ft->units_per_EM;
    F32 pixels_per_unit = pixels_per_em * ems_per_unit;

    mAscender   =  ft->ascender  * pixels_per_unit;
    mDescender  = -ft->descender * pixels_per_unit;
    mLineHeight =  ft->height    * pixels_per_unit;

    // The atlas (LLFontBitmapCache) is owned by mFace and was initialized
    // inside LLFontFace::load with the same metrics computation; nothing
    // to do here for atlas setup.

    if (!mIsFallback)
    {
        // Pre-warm the default glyph (notdef) so misses on this head route
        // through it without an extra rasterize on first miss. Goes into
        // mFace's atlas; subsequent faces wrapping the same LLFontFace as
        // a fallback won't re-pre-warm — atlas already has notdef.
        if (!mFace->findGlyphInfo(0, EFontGlyphType::Grayscale))
        {
            addGlyphFromFont(this, 0, 0, EFontGlyphType::Grayscale);
        }
    }

    mName = filename;
    mPointSize = point_size;

    mStyle = LLFontGL::NORMAL;
    if (ft->style_flags & FT_STYLE_FLAG_BOLD)
    {
        mStyle |= LLFontGL::BOLD;
    }
    else if (flags & LLFontGL::BOLD)
    {
        // Programmatic bolding for fonts in a 'bold' descriptor that don't
        // have the bold style flag set (e.g. Inter SemiBold).
        mStyle |= LLFontGL::BOLD;
    }
    else if (weight >= 600 && mFace->wghtAxisSet())
    {
        // Variable face whose wght axis we set to 600+ already produced a
        // heavy face; skip the programmatic bolding pass.
        mStyle |= LLFontGL::BOLD;
    }

    if (ft->style_flags & FT_STYLE_FLAG_ITALIC)
    {
        mStyle |= LLFontGL::ITALIC;
    }

    return true;
}

S32 LLFontFreetype::getNumFaces(const std::string& filename)
{
    // Probe-only: open a temporary face to read num_faces and immediately
    // close it. Doesn't touch any per-instance state — different from a
    // full loadFace, which goes through the manager's cache.
    FT_Face probe = nullptr;
    FT_Open_Args openArgs;
    memset(&openArgs, 0, sizeof(openArgs));
    openArgs.memory_base = gFontManagerp->loadFont(filename, openArgs.memory_size);
    if (!openArgs.memory_base)
        return 0;
    openArgs.flags = FT_OPEN_MEMORY;
    if (FT_Open_Face(gFTLibrary, &openArgs, 0, &probe) != 0)
        return 0;
    S32 num_faces = probe->num_faces;
    FT_Done_Face(probe);
    return num_faces;
}

void LLFontFreetype::addFallbackFont(const LLPointer<LLFontFreetype>& fallback_font,
                                     const char_functor_t& functor)
{
    mFallbackFonts.emplace_back(fallback_font, functor);
    // Resolution cache encodes the previous fallback list; new fallback may
    // win for codepoints that previously resolved to a later face or to
    // notdef on this face.
    mShapingFaceResolution.clear();
}

F32 LLFontFreetype::getLineHeight() const
{
    return mLineHeight;
}

F32 LLFontFreetype::getAscenderHeight() const
{
    return mAscender;
}

F32 LLFontFreetype::getDescenderHeight() const
{
    return mDescender;
}

F32 LLFontFreetype::getXAdvance(llwchar wch) const
{
    if (getFTFace() == nullptr)
        return 0.0;

    // Return existing info only if it is current
    LLFontGlyphInfo* gi = getGlyphInfo(wch, EFontGlyphType::Unspecified);
    if (gi)
    {
        return gi->mXAdvance;
    }
    else
    {
        char_glyph_info_map_t::iterator found_it = mCharGlyphInfoMap.find((llwchar)0);
        if (found_it != mCharGlyphInfoMap.end())
        {
            return found_it->second->mXAdvance;
        }
    }

    // Last ditch fallback - no glyphs defined at all.
    return (F32)getBitmapCache()->getMaxCharWidth();
}

F32 LLFontFreetype::getXAdvance(const LLFontGlyphInfo* glyph) const
{
    if (getFTFace() == nullptr)
        return 0.0;

    return glyph->mXAdvance;
}

F32 LLFontFreetype::getXKerning(llwchar char_left, llwchar char_right) const
{
    if (getFTFace() == nullptr)
        return 0.0;

    //llassert(!mIsFallback);
    LLFontGlyphInfo* left_glyph_info = getGlyphInfo(char_left, EFontGlyphType::Unspecified);;
    // Kern this puppy.
    LLFontGlyphInfo* right_glyph_info = getGlyphInfo(char_right, EFontGlyphType::Unspecified);

    return getXKerning(left_glyph_info, right_glyph_info);
}

F32 LLFontFreetype::getXKerning(const LLFontGlyphInfo* left_glyph_info, const LLFontGlyphInfo* right_glyph_info) const
{
    if (getFTFace() == nullptr)
        return 0.0;

    U32 left_glyph = left_glyph_info ? left_glyph_info->mGlyphIndex : 0;
    U32 right_glyph = right_glyph_info ? right_glyph_info->mGlyphIndex : 0;

    FT_Vector  delta;

    // UNFITTED gives subpixel-precise kerning when callers maintain a
    // fractional pen accumulator (mUseSubpixelPen — autohinted/unhinted
    // faces). DEFAULT grid-fits to integer pixels, which is what callers
    // want when they round per glyph (native-hinted faces).
    const FT_UInt kern_mode = mUseSubpixelPen ? FT_KERNING_UNFITTED : FT_KERNING_DEFAULT;
    llverify(!FT_Get_Kerning(getFTFace(), left_glyph, right_glyph, kern_mode, &delta));

    // Apply the FreeType auto-hinter's subpixel side-bearing correction between
    // adjacent glyphs. The lsb/rsb deltas are populated only when the autohinter
    // ran; for native-hinted (DEFAULT) and unhinted (NO_HINTING) loads they're
    // always zero and the correction is meaningless.
    F32 delta_correction = 0.0f;
    if (mHinting == EFontHinting::FORCE_AUTOHINT && left_glyph_info && right_glyph_info)
    {
        // delta_diff is in 26.6 fixed point: the autohinter's net shift in
        // inter-glyph spacing (positive = hinter pushed glyphs apart).
        S32 delta_diff = left_glyph_info->mRsbDelta - right_glyph_info->mLsbDelta;
        if (mUseSubpixelPen)
        {
            // Fractional pen accumulator can absorb the exact sub-pixel
            // shift. FreeType reference: "you can apply the values directly
            // as a fractional adjustment" when sub-pixel positioning is in
            // use. Sign matches the integer pattern below — delta_diff > 0
            // moves the next glyph leftward to compensate for the hinter's
            // outward shift.
            delta_correction = -(F32)delta_diff / 64.0f;
        }
        else
        {
            // Integer pen: ±1 pixel jump at FreeType's documented thresholds
            // (ftautoh / glyph-to-bitmap example). The discrete clamp is the
            // best approximation of the fractional shift when the pen can't
            // hold sub-pixel state.
            if (delta_diff >= 32)
                delta_correction = -1.0f;
            else if (delta_diff < -32)
                delta_correction = 1.0f;
        }
    }

    // FT_Get_Kerning returns delta.x in 26.6 fixed-point regardless of mode;
    // the mode only controls whether values are grid-fitted to integer pixels.
    return (F32)(delta.x * (1.f / 64.f)) + delta_correction;
}

bool LLFontFreetype::hasGlyph(llwchar wch) const
{
    llassert(!mIsFallback);
    return(mCharGlyphInfoMap.find(wch) != mCharGlyphInfoMap.end());
}

LLFontGlyphInfo* LLFontFreetype::addGlyph(llwchar wch, EFontGlyphType glyph_type) const
{
    if (!getFTFace())
    {
        return nullptr;
    }

    llassert(!mIsFallback);
    llassert(glyph_type < EFontGlyphType::Count);
    //LL_DEBUGS() << "Adding new glyph for " << wch << " to font" << LL_ENDL;

    // Initialize char to glyph map
    U32 glyph_index = getCharGlyphIndex(wch);
    if (glyph_index == 0)
    {
        // No corresponding glyph in this font: look for a glyph in fallback
        // fonts.
        size_t count = mFallbackFonts.size();
        if (LLStringOps::isEmoji(wch))
        {
            // This is a "genuine" emoji (in the range 0x1f000-0x20000): print
            // it using the emoji font(s) if possible. HB
            for (size_t i = 0; i < count; ++i)
            {
                const fallback_font_t& pair = mFallbackFonts[i];
                if (!pair.second || !pair.second(wch))
                {
                    // If this font does not have a functor, or the character
                    // does not pass the functor, reject it. Note: we keep the
                    // functor test (despite the fact we already tested for
                    // LLStringOps::isEmoji(wch) above), in case we would use
                    // different, more restrictive or partionned functors in
                    // the future with several different emoji fonts. HB
                    continue;
                }
                glyph_index = pair.first->getCharGlyphIndex(wch);
                if (glyph_index)
                {
                    return addGlyphFromFont(pair.first, wch, glyph_index,
                                            glyph_type);
                }
            }
        }
        // Then try and find a monochrome fallback font that could print this
        // glyph: such fonts do *not* have a functor. We give priority to
        // monochrome fonts for non-genuine emojis so that UI elements which
        // used to render with them before the emojis font introduction (e.g.
        // check marks in menus, or LSL dialogs text and buttons) do render the
        // same way as they always did. HB
        std::vector<size_t> emoji_fonts_idx;
        for (size_t i = 0; i < count; ++i)
        {
            const fallback_font_t& pair = mFallbackFonts[i];
            if (pair.second)
            {
                // If this font got a functor, remember the index for later and
                // try the next fallback font. HB
                emoji_fonts_idx.push_back(i);
                continue;
            }
            glyph_index = pair.first->getCharGlyphIndex(wch);
            if (glyph_index)
            {
                return addGlyphFromFont(pair.first, wch, glyph_index,
                                        glyph_type);
            }
        }
        // Everything failed so far: this character is not a genuine emoji,
        // neither a special character known from our monochrome fallback
        // fonts: make a last try, using the emoji font(s), but ignoring the
        // functor to render using whatever (colorful) glyph that might be
        // available in such fonts for this character. HB
        for (size_t j = 0, count2 = emoji_fonts_idx.size(); j < count2; ++j)
        {
            const fallback_font_t& pair = mFallbackFonts[emoji_fonts_idx[j]];
            glyph_index = pair.first->getCharGlyphIndex(wch);
            if (glyph_index)
            {
                return addGlyphFromFont(pair.first, wch, glyph_index,
                                        glyph_type);
            }
        }
    }

    auto range_it = mCharGlyphInfoMap.equal_range(wch);
    char_glyph_info_map_t::iterator iter =
        std::find_if(range_it.first, range_it.second,
                     [&glyph_type](const char_glyph_info_map_t::value_type& entry)
                     {
                        return entry.second->mGlyphType == glyph_type;
                     });
    if (iter == range_it.second)
    {
        return addGlyphFromFont(this, wch, glyph_index, glyph_type);
    }
    return nullptr;
}

LLFontGlyphInfo* LLFontFreetype::renderAndCreateGlyph(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType requested_glyph_type, EFontGlyphType& out_bitmap_glyph_type) const
{
    LL_PROFILE_ZONE_SCOPED;
    if (getFTFace() == nullptr)
        return nullptr;

    llassert(!mIsFallback);

    // Render N subpixel-x phases for fonts that use the subpixel pen
    // accumulator, otherwise just one. Each phase shifts the outline by
    // (k / kNumPhases) px before rasterization via FT_Set_Transform; the
    // resulting bitmaps go to separate atlas slots and the renderer picks
    // one matching the fractional pen position at draw time.
    const U8 num_phases = fontp->mUseSubpixelPen ? LLFontGlyphInfo::kNumPhases : 1;

    LLFontGlyphInfo* gi = new LLFontGlyphInfo(glyph_index, requested_glyph_type);
    gi->mPhaseCount = num_phases;
    // Atlas owner: the renderer follows this to bind the right texture.
    gi->mSourceFace = fontp->mFace.get();

    EFontGlyphType bitmap_glyph_type = EFontGlyphType::Unspecified;

    for (U8 phase = 0; phase < num_phases; ++phase)
    {
        // FT_Set_Transform delta in 26.6 fixed-point. 64 == 1 px, so each
        // phase shifts by (phase * 64 / kNumPhases) units.
        FT_Vector delta = { (FT_Pos)((phase * 64) / LLFontGlyphInfo::kNumPhases), 0 };
        FT_Set_Transform(fontp->getFTFace(), nullptr, &delta);

        // wch is only meaningful for the SVG glyph hook's debug output; shaped
        // lookups don't have a single source codepoint to pass here.
        fontp->renderGlyph(requested_glyph_type, glyph_index, 0);

        EFontGlyphType phase_type = EFontGlyphType::Unspecified;
        switch (fontp->getFTFace()->glyph->bitmap.pixel_mode)
        {
            case FT_PIXEL_MODE_MONO:
            case FT_PIXEL_MODE_GRAY:
                phase_type = EFontGlyphType::Grayscale;
                break;
            case FT_PIXEL_MODE_BGRA:
                phase_type = EFontGlyphType::Color;
                break;
            default:
                llassert_always(true);
                break;
        }
        // All phases must produce the same pixel format — they're the same
        // glyph rasterized at slightly different x-offsets.
        if (phase == 0)
        {
            bitmap_glyph_type = phase_type;
        }
        else
        {
            llassert(phase_type == bitmap_glyph_type);
        }

        S32 width = fontp->getFTFace()->glyph->bitmap.width;
        S32 height = fontp->getFTFace()->glyph->bitmap.rows;

        S32 pos_x, pos_y;
        U32 bitmap_num;
        // Allocate the slot in the *source* face's atlas. Today's call site
        // wrote to the head's atlas; after the move every glyph rasterized
        // through fontp lives in fontp->mFace's shared atlas, so heads with
        // a common fallback face share these slots.
        fontp->getBitmapCache()->nextOpenPos(width, pos_x, pos_y, bitmap_glyph_type, bitmap_num);

        LLFontGlyphInfo::PhaseSlot& slot = gi->mPhaseSlots[phase];
        slot.mXBitmapOffset = pos_x;
        slot.mYBitmapOffset = pos_y;
        slot.mWidth = width;
        slot.mHeight = height;
        slot.mXBearing = fontp->getFTFace()->glyph->bitmap_left;
        slot.mYBearing = fontp->getFTFace()->glyph->bitmap_top;
        slot.mBitmapEntry = std::make_pair(bitmap_glyph_type, bitmap_num);

        // Glyph-level metrics come from phase 0. They're invariant across
        // phases (FT_Set_Transform shifts the outline before rasterization —
        // advance, lsb/rsb deltas, and overall metrics are unchanged) modulo
        // 1 px AA boundary effects. Measurement paths use these.
        if (phase == 0)
        {
            gi->mWidth = width;
            gi->mHeight = height;
            gi->mXBearing = slot.mXBearing;
            gi->mYBearing = slot.mYBearing;
            // FreeType fills these when the glyph has been auto-hinted; they describe
            // how much the hinter nudged the left/right side bearings (in 26.6 pixels).
            // Keep them so inter-glyph spacing can be corrected in getXKerning().
            gi->mLsbDelta = (S32)fontp->getFTFace()->glyph->lsb_delta;
            gi->mRsbDelta = (S32)fontp->getFTFace()->glyph->rsb_delta;
            // Convert these from 26.6 units to float pixels.
            gi->mXAdvance = fontp->getFTFace()->glyph->advance.x / 64.f;
            gi->mYAdvance = fontp->getFTFace()->glyph->advance.y / 64.f;
        }

        // Copy the rasterized bitmap into the per-phase atlas slot.
        if (fontp->getFTFace()->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO
            || fontp->getFTFace()->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
        {
            U8 *buffer_data = fontp->getFTFace()->glyph->bitmap.buffer;
            S32 buffer_row_stride = fontp->getFTFace()->glyph->bitmap.pitch;
            U8 *tmp_graydata = nullptr;

            if (fontp->getFTFace()->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
            {
                // need to expand 1-bit bitmap to 8-bit graymap.
                tmp_graydata = new U8[width * height];
                S32 xpos, ypos;
                for (ypos = 0; ypos < height; ++ypos)
                {
                    S32 bm_row_offset = buffer_row_stride * ypos;
                    for (xpos = 0; xpos < width; ++xpos)
                    {
                        U32 bm_col_offsetbyte = xpos / 8;
                        U32 bm_col_offsetbit = 7 - (xpos % 8);
                        U32 bit =
                            !!(buffer_data[bm_row_offset + bm_col_offsetbyte] & (1 << bm_col_offsetbit));
                        tmp_graydata[width * ypos + xpos] = 255 * bit;
                    }
                }
                // use newly-built graymap.
                buffer_data = tmp_graydata;
                buffer_row_stride = width;
            }

            fontp->mFace->setSubImageLuminanceAlpha(pos_x, pos_y, bitmap_num, width, height,
                                                    buffer_data, buffer_row_stride);

            if (tmp_graydata)
                delete[] tmp_graydata;
        }
        else if (fontp->getFTFace()->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
        {
            fontp->mFace->setSubImageBGRA(pos_x, pos_y, bitmap_num,
                            fontp->getFTFace()->glyph->bitmap.width,
                            fontp->getFTFace()->glyph->bitmap.rows,
                            fontp->getFTFace()->glyph->bitmap.buffer,
                            llabs(fontp->getFTFace()->glyph->bitmap.pitch));
        }
        else
        {
            llassert(false);
        }

        LLImageGL *image_gl = fontp->getBitmapCache()->getImageGL(bitmap_glyph_type, bitmap_num);
        LLImageRaw *image_raw = fontp->getBitmapCache()->getImageRaw(bitmap_glyph_type, bitmap_num);
        if (image_gl && image_raw)
        {
            image_gl->setSubImage(image_raw, 0, 0, image_gl->getWidth(), image_gl->getHeight());
        }
        else
        {
            llassert(false); //images were just inserted by nextOpenPos, they shouldn't be missing
        }
    }

    // Reset the FT face transform so subsequent non-phased renders (e.g. for
    // fallback paths or other faces sharing this FT_Library) aren't affected.
    FT_Set_Transform(fontp->getFTFace(), nullptr, nullptr);

    out_bitmap_glyph_type = bitmap_glyph_type;
    return gi;
}

LLFontGlyphInfo* LLFontFreetype::addGlyphFromFont(const LLFontFreetype *fontp, llwchar wch, U32 glyph_index, EFontGlyphType requested_glyph_type) const
{
    EFontGlyphType bitmap_glyph_type;
    LLFontGlyphInfo* gi = renderAndCreateGlyph(fontp, glyph_index, requested_glyph_type, bitmap_glyph_type);
    if (!gi)
        return nullptr;

    // Face owns the glyph info entry; head's map holds a non-owning pointer
    // for fast lookup. Insert into face first so head's pointer is to the
    // canonical entry.
    fontp->mFace->insertGlyphInfo(wch, gi);
    insertGlyphInfo(wch, gi);

    if (requested_glyph_type != bitmap_glyph_type)
    {
        LLFontGlyphInfo* gi_temp = new LLFontGlyphInfo(*gi);
        gi_temp->mGlyphType = bitmap_glyph_type;
        fontp->mFace->insertGlyphInfo(wch, gi_temp);
        insertGlyphInfo(wch, gi_temp);
    }

    return gi;
}

LLFontGlyphInfo* LLFontFreetype::addShapedGlyphFromFont(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType requested_glyph_type) const
{
    EFontGlyphType bitmap_glyph_type;
    LLFontGlyphInfo* gi = renderAndCreateGlyph(fontp, glyph_index, requested_glyph_type, bitmap_glyph_type);
    if (!gi)
        return nullptr;

    // Face owns the entry; head caches a non-owning pointer keyed on
    // (face*, glyph_index) so the next lookup short-circuits.
    fontp->mFace->insertShapedGlyphInfo(glyph_index, gi);
    insertShapedGlyphInfo(fontp, glyph_index, gi);

    if (requested_glyph_type != bitmap_glyph_type)
    {
        LLFontGlyphInfo* gi_temp = new LLFontGlyphInfo(*gi);
        gi_temp->mGlyphType = bitmap_glyph_type;
        fontp->mFace->insertShapedGlyphInfo(glyph_index, gi_temp);
        insertShapedGlyphInfo(fontp, glyph_index, gi_temp);
    }

    return gi;
}

LLFontGlyphInfo* LLFontFreetype::getGlyphInfo(llwchar wch, EFontGlyphType glyph_type) const
{
    // Fast path: head's resolution cache. Holds non-owning pointers into
    // face-owned entries.
    auto range_it = mCharGlyphInfoMap.equal_range(wch);
    auto iter = (EFontGlyphType::Unspecified != glyph_type)
        ? std::find_if(range_it.first, range_it.second,
            [&glyph_type](const char_glyph_info_map_t::value_type& entry)
            { return entry.second->mGlyphType == glyph_type; })
        : range_it.first;
    if (iter != range_it.second)
        return iter->second;

    // Head missed. The glyph might be cached on a fallback's face from a
    // sibling head's earlier rasterization — check the face caches before
    // re-rasterizing. Walk our chain in priority order, mirroring addGlyph.
    const EFontGlyphType resolve_type = (EFontGlyphType::Unspecified != glyph_type) ? glyph_type : EFontGlyphType::Grayscale;
    if (mFace)
    {
        if (LLFontGlyphInfo* gi = mFace->findGlyphInfo(wch, resolve_type))
        {
            insertGlyphInfo(wch, gi);
            return gi;
        }
    }
    for (const fallback_font_t& pair : mFallbackFonts)
    {
        if (pair.second && !pair.second(wch))
            continue; // functor gates this fallback to specific codepoints
        if (pair.first->mFace)
        {
            if (LLFontGlyphInfo* gi = pair.first->mFace->findGlyphInfo(wch, resolve_type))
            {
                insertGlyphInfo(wch, gi);
                return gi;
            }
        }
    }

    // Nothing cached anywhere — rasterize fresh through addGlyph.
    return addGlyph(wch, resolve_type);
}

LLFontGlyphInfo* LLFontFreetype::getGlyphInfoByIndex(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType glyph_type) const
{
    // Fast path: head's resolution cache.
    const ShapedGlyphKey key{fontp, glyph_index};
    auto range = mShapedGlyphInfoMap.equal_range(key);
    auto iter = (EFontGlyphType::Unspecified != glyph_type)
        ? std::find_if(range.first, range.second,
            [glyph_type](const shaped_glyph_info_map_t::value_type& entry)
            { return entry.second->mGlyphType == glyph_type; })
        : range.first;
    if (iter != range.second)
        return iter->second;

    // Head missed. Source face may have a cached entry from a sibling head.
    const EFontGlyphType resolve_type = (EFontGlyphType::Unspecified != glyph_type) ? glyph_type : EFontGlyphType::Grayscale;
    if (fontp && fontp->mFace)
    {
        if (LLFontGlyphInfo* gi = fontp->mFace->findShapedGlyphInfo(glyph_index, resolve_type))
        {
            insertShapedGlyphInfo(fontp, glyph_index, gi);
            return gi;
        }
    }
    return addShapedGlyphFromFont(fontp, glyph_index, resolve_type);
}

void LLFontFreetype::insertGlyphInfo(llwchar wch, LLFontGlyphInfo* gi) const
{
    // Head's map holds NON-OWNING pointers into face-owned entries — never
    // delete on replace. The face's cache deduplicates so an already-cached
    // (wch, type) entry reuses the same pointer.
    llassert(gi->mGlyphType < EFontGlyphType::Count);
    auto range_it = mCharGlyphInfoMap.equal_range(wch);
    auto iter = std::find_if(range_it.first, range_it.second,
        [&gi](const char_glyph_info_map_t::value_type& entry)
        { return entry.second->mGlyphType == gi->mGlyphType; });
    if (iter != range_it.second)
        iter->second = gi;
    else
        mCharGlyphInfoMap.insert(std::make_pair(wch, gi));
}

void LLFontFreetype::insertShapedGlyphInfo(const LLFontFreetype* fontp, U32 glyph_index, LLFontGlyphInfo* gi) const
{
    // Same non-owning semantics as insertGlyphInfo.
    llassert(gi->mGlyphType < EFontGlyphType::Count);
    const ShapedGlyphKey key{fontp, glyph_index};
    auto range = mShapedGlyphInfoMap.equal_range(key);
    auto iter = std::find_if(range.first, range.second,
        [gi](const shaped_glyph_info_map_t::value_type& entry)
        { return entry.second->mGlyphType == gi->mGlyphType; });
    if (iter != range.second)
        iter->second = gi;
    else
        mShapedGlyphInfoMap.insert(std::make_pair(key, gi));
}

void LLFontFreetype::renderGlyph(EFontGlyphType bitmap_type, U32 glyph_index, llwchar wch) const
{
    if (getFTFace() == nullptr)
        return;

    FT_Int32 load_flags = (FT_Int32)mHinting;
    if (EFontGlyphType::Color == bitmap_type)
    {
        // We may not actually get a color render so our caller should always examine getFTFace()->glyph->bitmap.pixel_mode
        load_flags |= FT_LOAD_COLOR;
    }

    FT_Error error = FT_Load_Glyph(getFTFace(), glyph_index, load_flags);
    if (FT_Err_Ok != error)
    {
        if (error == FT_Err_Out_Of_Memory)
        {
            LLError::LLUserWarningMsg::showOutOfMemory();
            LL_ERRS() << "Out of memory loading glyph for character " << llformat("U+%xu", U32(wch)) << LL_ENDL;
        }

        std::string message = llformat(
            "Error %d (%s) loading wchar %u glyph %u/%u: bitmap_type=%u, load_flags=%d",
            error, FT_Error_String(error), wch, glyph_index, getFTFace()->num_glyphs, bitmap_type, load_flags);
        LL_WARNS_ONCE() << message << LL_ENDL;
        error = FT_Load_Glyph(getFTFace(), glyph_index, load_flags ^ FT_LOAD_COLOR);
        if (FT_Err_Invalid_Outline == error
            || FT_Err_Invalid_Composite == error
            || (FT_Err_Ok != error && LLStringOps::isEmoji(wch)))
        {
            // value~0 always corresponds to the 'missing glyph'
            error = FT_Load_Glyph(getFTFace(), 0, FT_LOAD_DEFAULT);
            if (FT_Err_Ok != error)
            {
                LL_ERRS() << "Loading fallback for char '" << (U32)wch << "', glyph " << glyph_index << " failed with error : " << (S32)error << LL_ENDL;
            }
        }
        llassert_always_msg(FT_Err_Ok == error, message.c_str());
    }

    // FT_Render_Glyph's mode arg overrides the load_flags' target. For
    // LIGHT we want the lighter stem-weight filter to match the LIGHT
    // autohinter's no-horizontal-fit output; everything else uses NORMAL.
    const FT_Render_Mode render_mode = (mHinting == EFontHinting::LIGHT)
        ? FT_RENDER_MODE_LIGHT
        : FT_RENDER_MODE_NORMAL;
    if (FT_Render_Glyph(getFTFace()->glyph, render_mode) != 0)
    {
        LL_WARNS() << "Failed to render glyph for character " << llformat("U+%xu", U32(wch)) << " at glyph index " << glyph_index << LL_ENDL;
    }

    mRenderGlyphCount++;
}

void LLFontFreetype::resetSelf(F32 vert_dpi, F32 horz_dpi)
{
    // Reset just this instance — clear its glyph caches, drop the previous
    // face wrapper, and re-loadFace at the new DPI. Doesn't recurse into
    // mFallbackFonts because shared fallback instances should be reset once
    // by whoever owns the shared cache (LLFontRegistry::reset).
    resetBitmapCache();
    loadFace(mName, mPointSize, vert_dpi, horz_dpi, mWeight, mIsFallback, 0, mHinting, mFontFlags);
}

void LLFontFreetype::reset(F32 vert_dpi, F32 horz_dpi)
{
    // Standalone API for callers outside the registry: do the full cascade
    // (head plus its fallback chain). Inside the registry we use resetSelf
    // and drive the fallback resets ourselves — see LLFontRegistry::reset.
    resetSelf(vert_dpi, horz_dpi);
    if (!mIsFallback)
    {
        if (mFallbackFonts.empty())
        {
            LL_WARNS() << "LLFontGL::reset(), no fallback fonts present" << LL_ENDL;
        }
        else
        {
            for (fallback_font_vector_t::iterator it = mFallbackFonts.begin(); it != mFallbackFonts.end(); ++it)
            {
                it->first->resetSelf(vert_dpi, horz_dpi);
            }
        }
    }
}

void LLFontFreetype::resetBitmapCache()
{
    // Drop the head's non-owning resolution caches; face will purge owning
    // entries in resetBitmapCache below.
    mCharGlyphInfoMap.clear();
    mShapedGlyphInfoMap.clear();
    if (mFace)
        mFace->resetBitmapCache();

    if (!mIsFallback)
    {
        // Re-pre-warm notdef on the (now empty) face atlas. Skipped for
        // fallback heads — same logic as loadFace.
        addGlyphFromFont(this, 0, 0, EFontGlyphType::Grayscale);
    }
}

void LLFontFreetype::destroyGL()
{
    if (mFace)
        mFace->destroyGL();
}

const std::string &LLFontFreetype::getName() const
{
    return mName;
}

static void dumpFontBitmap(const LLImageRaw* image_raw, const std::string& file_name)
{
    LLPointer<LLImagePNG> tmpImage = new LLImagePNG();
    if ( (tmpImage->encode(image_raw, 0.0f)) && (tmpImage->save(gDirUtilp->getExpandedFilename(LL_PATH_LOGS, file_name))) )
    {
        LL_INFOS("Font") << "Successfully saved " << file_name << LL_ENDL;
    }
    else
    {
        LL_WARNS("Font") << "Failed to save " << file_name << LL_ENDL;
    }
}

void LLFontFreetype::dumpFontBitmaps() const
{
    // Dump all the regular bitmaps (if any)
    for (int idx = 0, cnt = getBitmapCache()->getNumBitmaps(EFontGlyphType::Grayscale); idx < cnt; idx++)
    {
        const LLImageRaw* raw = getBitmapCache()->getImageRaw(EFontGlyphType::Grayscale, idx);
        if (!raw) continue; // sheet was evicted
        dumpFontBitmap(raw, llformat("%s_%d_%d_%d.png", getFTFace()->family_name, (int)(mPointSize * 10), mStyle, idx));
    }

    // Dump all the color bitmaps (if any)
    for (int idx = 0, cnt = getBitmapCache()->getNumBitmaps(EFontGlyphType::Color); idx < cnt; idx++)
    {
        const LLImageRaw* raw = getBitmapCache()->getImageRaw(EFontGlyphType::Color, idx);
        if (!raw) continue; // sheet was evicted
        dumpFontBitmap(raw, llformat("%s_%d_%d_%d_clr.png", getFTFace()->family_name, (int)(mPointSize * 10), mStyle, idx));
    }
}

const LLFontBitmapCache* LLFontFreetype::getFontBitmapCache() const
{
    return getBitmapCache();
}

void LLFontFreetype::collectGarbage() const
{
    if (!getFTFace())
        return;

    // Sweep cadence: cheap enough to run at the top of every render call, with
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

    // Shaped runs in LLFontShaping's cache hold only metric/glyph_id data — no
    // atlas references — so they survive eviction; getGlyphInfoByIndex on the
    // next frame re-rasterizes whichever glyphs were dropped here. Cache
    // generation bumps inside releaseSheet so LLFontVertexBuffer rebuilds.
    for (U32 t = 0; t < static_cast<U32>(EFontGlyphType::Count); ++t)
    {
        const EFontGlyphType type = static_cast<EFontGlyphType>(t);
        const U32 sheet_count = getBitmapCache()->getNumBitmaps(type);
        for (U32 num = 0; num < sheet_count; ++num)
        {
            if (getBitmapCache()->isSheetReleased(type, num))
                continue;
            const F64 last_used = getBitmapCache()->getSheetLastUsedTime(type, num);
            // last_used == 0 means the sheet was allocated but not yet drawn
            // from — skip it for one cycle so a brand-new sheet gets at least
            // a frame to be touched before it's a candidate.
            if (last_used <= 0.0)
                continue;
            if ((now - last_used) <= IDLE_THRESHOLD_SEC)
                continue;

            // Purge entries from the face's owning caches first (deletes the
            // LLFontGlyphInfo objects), then from the head's non-owning map.
            auto matches = [&](const LLFontGlyphInfo* gi) { return glyph_uses_sheet(gi, type, num); };
            if (mFace)
            {
                mFace->erase_codepoint_entries(matches);
                mFace->erase_shaped_entries(matches);
            }
            // Head's resolution caches now hold dangling pointers — clear the
            // matching entries (no delete; face already freed them).
            for (auto it = mCharGlyphInfoMap.begin(); it != mCharGlyphInfoMap.end(); )
                it = matches(it->second) ? mCharGlyphInfoMap.erase(it) : std::next(it);
            for (auto it = mShapedGlyphInfoMap.begin(); it != mShapedGlyphInfoMap.end(); )
                it = matches(it->second) ? mShapedGlyphInfoMap.erase(it) : std::next(it);

            getBitmapCache()->releaseSheet(type, num);
        }
    }
}

void LLFontFreetype::setStyle(U8 style)
{
    mStyle = style;
}

U8 LLFontFreetype::getStyle() const
{
    return mStyle;
}

// (setSubImageBGRA / setSubImageLuminanceAlpha moved to LLFontFace —
// they operate purely on the atlas, which now lives on the face wrapper.)
// (setVariationAxis moved to LLFontFace::setVariationAxis — face state.)

namespace ll
{
    namespace fonts
    {
        class LoadedFont
        {
            public:
            LoadedFont( std::string aName , std::string const &aAddress, std::size_t aSize )
            : mAddress( aAddress )
            {
                mName = aName;
                mSize = aSize;
                mRefs = 1;
            }
            std::string mName;
            std::string mAddress;
            std::size_t mSize;
            U32  mRefs;
        };
    }
}

U8 const* LLFontManager::loadFont( std::string const &aFilename, long &a_Size)
{
    a_Size = 0;
    std::map< std::string, std::shared_ptr<ll::fonts::LoadedFont> >::iterator itr = m_LoadedFonts.find( aFilename );
    if( itr != m_LoadedFonts.end() )
    {
        ++itr->second->mRefs;
        // A possible overflow cannot happen here, as it is asserted that the size is less than std::numeric_limits<long>::max() a few lines below.
        a_Size = static_cast<long>(itr->second->mSize);
        return reinterpret_cast<U8 const*>(itr->second->mAddress.c_str());
    }

    auto strContent = LLFile::getContents(aFilename);

    if( strContent.empty() )
        return nullptr;

    // For fontconfig a type of long is required, std::string::size() returns size_t. I think it is safe to limit this to 2GiB and not support fonts that huge (can that even be a thing?)
    llassert_always( strContent.size() < std::numeric_limits<long>::max() );

    a_Size = static_cast<long>(strContent.size());

    auto pCache = std::make_shared<ll::fonts::LoadedFont>( aFilename,  strContent, a_Size );
    itr = m_LoadedFonts.insert( std::make_pair( aFilename, pCache ) ).first;

    return reinterpret_cast<U8 const*>(itr->second->mAddress.c_str());
}

LLPointer<LLFontFace> LLFontManager::getOrCreateFace(const LLFontFaceKey& key)
{
    auto it = mFaceCache.find(key);
    if (it != mFaceCache.end())
        return it->second;

    LLPointer<LLFontFace> face = new LLFontFace;
    if (!face->load(key.filename, key.face_index, key.point_size,
                    key.vert_dpi, key.horz_dpi, key.weight, key.hinting, key.flags))
    {
        // Don't cache failures — caller handles retry through search paths.
        return nullptr;
    }
    mFaceCache.emplace(key, face);
    return face;
}

void LLFontManager::unloadAllFonts()
{
    // Order matters: face wrappers hold pointers into the byte cache via
    // FT_OPEN_MEMORY. Drop the face cache first so FT_Done_Face fires while
    // the bytes are still alive, then drop the byte cache.
    mFaceCache.clear();
    m_LoadedFonts.clear();
}
