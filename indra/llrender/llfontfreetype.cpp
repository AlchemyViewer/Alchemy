/**
 * @file llfontfreetype.cpp
 * @brief Freetype font library wrapper
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include <unordered_set>

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

#include "alfontcolrv1.h"
#include "alfontshaping.h"

#include "lldir.h"
#include "llerror.h"
#include "llimage.h"
#include "llimagepng.h"
//#include "llimagej2c.h"
#include "llmath.h" // Linden math
#include "llstring.h"
//#include "imdebug.h"
#include "llfontbitmapcache.h"
#include "llgl.h"
#include "llwindow.h"

#define ENABLE_OT_SVG_SUPPORT

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
        // LL_ERRS aborts; FT_Done_FreeType on a failed init is UB anyway.
        LL_ERRS() << "Freetype initialization failure!" << LL_ENDL;
    }

    FT_Int major, minor, patch;
    FT_Library_Version(gFTLibrary, &major, &minor, &patch);
    LL_INFOS() << "Freetype version: " << major << "." << minor << "." << patch << LL_ENDL;

#ifdef ENABLE_OT_SVG_SUPPORT
    FT_Property_Set(gFTLibrary, "ot-svg", "svg-hooks", &plutosvg_ft_hooks);
#endif

    // Disable FT's stem darkening on every hinter module. Stem darkening
    // assumes gamma-correct (linear-space) compositing — the renderer here
    // blends fonts in sRGB space (no linearization in the UI font shaders),
    // so darkened stems would over-thicken against our blend curve. The
    // autofitter is off by default in FT 2.7+, but cff/type1/t1cid all
    // default ON; a CFF font loaded with EFontHinting::DEFAULT would pick
    // up unwanted darkening without these explicit overrides.
    {
        FT_Bool no_darken = 1;
        FT_Property_Set(gFTLibrary, "autofitter", "no-stem-darkening", &no_darken);
        FT_Property_Set(gFTLibrary, "cff",        "no-stem-darkening", &no_darken);
        FT_Property_Set(gFTLibrary, "type1",      "no-stem-darkening", &no_darken);
        FT_Property_Set(gFTLibrary, "t1cid",      "no-stem-darkening", &no_darken);
    }
}

LLFontManager::~LLFontManager()
{
    // Order matters: cached ALFontFace instances hold FT_Face pointers
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
    , mSourceFace(fgi.mSourceFace)
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
    // mSourceFace MUST propagate: the secondary-publish path in
    // addShapedGlyphFromFont copies an existing entry and republishes it
    // under the actual bitmap_glyph_type, and the renderer reads
    // sfgi->mSourceFace to pick the atlas. A null here produces a silent
    // no-op render for typed lookups that hit the dup.
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
    // The shape cache holds ALShapedGlyph entries keyed on
    // (LLFontFreetype*, glyph_index); ours are about to dangle. Other
    // faces' entries stay valid, so only drop ours.
    ALFontShaping::clearCacheForFace(this);

    // mFace's FT_Face, hb_font_t, and atlas lifetime is owned by the
    // ALFontFace wrapper — releasing the LLPointer here lets the wrapper
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

bool LLFontFreetype::wghtAxisSet() const
{
    return mFace && mFace->wghtAxisSet();
}

namespace
{
    // Walk a fallback list and return the (face, glyph_index) for the first
    // entry where `keep(functor)` returns true AND the face's charmap has
    // `wch`. Returns (nullptr, 0) when no entry hits.
    //
    // This is the iteration mechanic shared between the codepoint-path
    // (getGlyphInfo) and shape-path (selectShapingFace) chain walks. The
    // two policies still differ — see each call site for which passes run
    // and why — but the per-pass loop is identical and lives here.
    template <typename Keep>
    std::pair<const LLFontFreetype*, U32>
    find_fallback_hit(const LLFontFreetype::fallback_font_vector_t& fallbacks,
                      llwchar wch, Keep keep)
    {
        for (const auto& pair : fallbacks)
        {
            if (!keep(pair.second))
                continue;
            U32 gi = pair.first->getCharGlyphIndex(wch);
            if (gi)
                return { pair.first.get(), gi };
        }
        return { nullptr, 0u };
    }
}

const LLFontFreetype* LLFontFreetype::selectShapingFace(llwchar base, U32& out_glyph_index) const
{
    out_glyph_index = 0;
    if (!getFTFace())
        return this;

    // mIsFallback is intentionally NOT asserted here. The mIsFallback flag
    // primarily controls load-time behavior (the head pre-warms a notdef
    // glyph through the atlas, which a fallback skips) — it doesn't change
    // whether the chain walk below is well-defined. Unit tests load faces
    // with is_fallback=true to bypass the gGL-touching pre-warm, then
    // attach their own fallbacks via addFallbackFont and call this method
    // to drive the shape-itemizer; the walk below handles that case
    // correctly. A genuinely-empty chain (production fallback face) just
    // resolves to {this, getCharGlyphIndex(base)} and returns harmlessly.
    if (auto it = mShapingFaceResolution.find(base); it != mShapingFaceResolution.end())
    {
        out_glyph_index = it->second.second;
        return it->second.first;
    }

    // Shape-path priority:
    //   1. Emoji-functor fallbacks (functor must accept `base`).
    //   2. Root face.
    //   3. Monochrome fallbacks (no functor).
    //
    // The functor gates entry into the emoji bypass: shape ranges can cover
    // whole strings (not just emoji clusters), so plain non-pictographic
    // chars also pass through here. Color emoji fonts like Twemoji carry
    // ASCII outlines for safety, and routing 'e' through Twemoji means
    // rasterizing a color SVG glyph as Grayscale — FreeType produces a 0×0
    // bitmap and the glyph renders invisible while HarfBuzz's advance still
    // moves the pen, leaving gaps in plain text.
    //
    // For chars the functor accepts (genuine emoji range, plus whatever BMP
    // pictographs / keycap markers the unicode_ranges entry covers), the
    // bypass still kicks in so HarfBuzz can compose ZWJ sequences and
    // keycaps via the emoji face's GSUB tables.
    //
    // No "ignoring-functor" retry like the codepoint path has — by the
    // time the shape path is reached, the functor's answer is the policy
    // we want to honor.
    auto resolve = [&]() -> std::pair<const LLFontFreetype*, U32>
    {
        if (auto hit = find_fallback_hit(mFallbackFonts, base,
                [&](const char_functor_t& f) { return f && f(base); });
            hit.first)
        {
            return hit;
        }
        if (U32 gi = getCharGlyphIndex(base); gi != 0)
            return { this, gi };
        if (auto hit = find_fallback_hit(mFallbackFonts, base,
                [](const char_functor_t& f) { return !f; });
            hit.first)
        {
            return hit;
        }
        if (auto hit = attachOsFallbackFor(base); hit.first)
        {
            return hit;
        }
        // No face has a glyph; caller will end up rendering a tofu via `this`.
        return { this, 0u };
    };

    const auto result = resolve();
    // Bound per-face memory: a long-running client with CJK-heavy chat
    // can otherwise accumulate one entry per unique codepoint forever.
    // Clear-and-rebuild rather than LRU-evict — entries are cheap to
    // recompute and the map is only an optimization. Limit chosen well
    // above realistic working sets (CJK common-use is ~7k chars).
    constexpr size_t SHAPING_RESOLUTION_LIMIT = 16384;
    if (mShapingFaceResolution.size() >= SHAPING_RESOLUTION_LIMIT)
        mShapingFaceResolution.clear();
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

bool LLFontFreetype::loadFace(const std::string& filename, F32 point_size, F32 vert_dpi, F32 horz_dpi, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags, const ALFontVarAxes& var_axes)
{
    if (mFace)
    {
        // Reload: cached shape entries depend on the previous face state.
        // Only this face's entries become stale here — siblings keep theirs.
        ALFontShaping::clearCacheForFace(this);
    }
    // Drop the per-head shaping-face resolution cache: a fresh fallback chain
    // is about to be wired up and any cached (codepoint -> winning face)
    // mappings would resolve through the wrong faces.
    mShapingFaceResolution.clear();

    // Resolve the (file, sized + variable axis) state via the manager's
    // shared face cache. Heads and fallbacks alike consult the cache; same
    // params yield the same wrapper.
    ALFontFaceKey key{filename, face_n, point_size, vert_dpi, horz_dpi, hinting, flags, var_axes};
    mFace = gFontManagerp->getOrCreateFace(key);
    if (!mFace || !mFace->isValid())
    {
        mFace = nullptr;
        return false;
    }

    ALFT_Face ft = mFace->face();

    mIsFallback = is_fallback;
    mHinting    = hinting;
    mFontFlags  = flags;
    mVarAxes    = var_axes;
    mUseSubpixelPen = mFace->useSubpixelPen();
    mFaceIndex  = face_n;
    mVertDPI    = vert_dpi;
    mHorzDPI    = horz_dpi;

    // FreeType's size->metrics is populated by FT_Set_Char_Size (run inside
    // ALFontFace::load) with scaled, 26.6 fractional-pixel ascender /
    // descender / height values. Reading them directly matches FT's own
    // accounting exactly — re-deriving from design units would diverge by
    // sub-pixel due to FreeType's internal 16.16 y_scale rounding.
    constexpr F32 INV_64 = 1.f / 64.f;
    const FT_Size_Metrics& metrics = ft->size->metrics;
    mAscender   =  metrics.ascender  * INV_64;
    mDescender  = -metrics.descender * INV_64;  // FT descender is negative; flip to positive depth.
    mLineHeight =  metrics.height    * INV_64;

    // The atlas (LLFontBitmapCache) is owned by mFace and was initialized
    // inside ALFontFace::load; nothing to do here for atlas setup.

    if (!mIsFallback)
    {
        // Pre-warm the default glyph (notdef, glyph_index=0) so misses on
        // this head route through it without an extra rasterize on first
        // miss. Goes into mFace's atlas; subsequent faces wrapping the
        // same ALFontFace as a fallback won't re-pre-warm — atlas already
        // has notdef.
        if (!mFace->findGlyphInfo(0, EFontGlyphType::Grayscale))
        {
            addShapedGlyphFromFont(this, 0, EFontGlyphType::Grayscale);
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
    else if (mFace->wghtAxisSet() && var_axes.wght >= 600.f)
    {
        // Variable face whose wght axis we set to 600+ already produced a
        // heavy face; skip the programmatic bolding pass. wghtAxisSet()
        // implies var_axes.wght_set was true at load time (ALFontFace::load
        // only flips mWghtAxisSet inside the `if (var_axes.wght_set)`
        // branch), so the value comparison alone discriminates regular
        // from bold once we know the axis fired.
        mStyle |= LLFontGL::BOLD;
    }

    if (ft->style_flags & FT_STYLE_FLAG_ITALIC)
    {
        mStyle |= LLFontGL::ITALIC;
    }
    else if (flags & LLFontGL::ITALIC)
    {
        // Programmatic italic flag from the descriptor; the renderer
        // synthesises a slant if no real italic axis is in effect.
        mStyle |= LLFontGL::ITALIC;
    }
    else if ((mFace->italAxisSet() && var_axes.ital >= 0.5f)
          || (mFace->slntAxisSet() && var_axes.slnt <= -1.f))
    {
        // Variable face whose ital axis we set to ≥0.5, or whose slnt
        // axis we set to a negative angle, already produced a slanted
        // face — flag the head as italic so consumers branch correctly
        // and the synthesised slant pass is skipped.
        mStyle |= LLFontGL::ITALIC;
    }

    return true;
}

S32 LLFontFreetype::getNumFaces(const std::string& filename)
{
    // Probe-only: open a temporary face to read num_faces and immediately
    // close it. Read the file into a local buffer instead of going through
    // gFontManagerp->loadFont — the manager caches by filename and would
    // hold the bytes alive forever for any probe of a font we never end up
    // loading as a real face.
    auto contents = LLFile::getContents(filename);
    if (contents.empty())
        return 0;

    FT_Open_Args openArgs;
    memset(&openArgs, 0, sizeof(openArgs));
    openArgs.memory_base = reinterpret_cast<const FT_Byte*>(contents.data());
    openArgs.memory_size = static_cast<FT_Long>(contents.size());
    openArgs.flags = FT_OPEN_MEMORY;

    FT_Face probe = nullptr;
    if (FT_Open_Face(gFTLibrary, &openArgs, 0, &probe) != 0)
        return 0;
    S32 num_faces = probe->num_faces;
    FT_Done_Face(probe);
    return num_faces;
}

void LLFontFreetype::addFallbackFont(const LLPointer<LLFontFreetype>& fallback_font,
                                     const char_functor_t& functor) const
{
    mFallbackFonts.emplace_back(fallback_font, functor);
    // Both caches encode the previous fallback list; the new fallback may win
    // for codepoints that previously resolved to a later face or to notdef on
    // this face. Clear both:
    //  - mShapingFaceResolution caches per-codepoint (winning face, glyph_id)
    //    decisions made by selectShapingFace.
    //  - ALFontShaping's global cache stores already-shaped runs keyed on
    //    (codepoints, root_face). Entries with root_face == this still reflect
    //    the OLD itemization (e.g. CJK codepoints rendered as the head's
    //    notdef before the CJK fallback was attached) and must be discarded
    //    so the next shape() re-itemizes through the new chain.
    // LLFontRegistry::createFont attaches the configured chain before any
    // shaping happens, so neither cache holds entries on that path — but
    // attachOsFallbackFor mutates the chain mid-run, after both caches are
    // warm, and relies on this clearing to avoid stale shape results.
    mShapingFaceResolution.clear();
    ALFontShaping::clearCacheForFace(this);
}

bool LLFontFreetype::hasFallbackPath(const std::string& path) const
{
    for (const fallback_font_t& pair : mFallbackFonts)
    {
        if (pair.first->getName() == path)
        {
            return true;
        }
    }
    return false;
}

std::pair<const LLFontFreetype*, U32> LLFontFreetype::attachOsFallbackFor(llwchar wch) const
{
    // Only the head grows a fallback chain. Fallback faces are reached
    // through their head's chain and must not sprout chains of their own;
    // selectShapingFace is also driven directly against fallback-flagged
    // faces by the unit tests, which pin the "nothing covers this, expect
    // notdef" outcome.
    if (mIsFallback)
        return { nullptr, 0u };

    // One OS query per codepoint, hit or miss. A miss is the common case
    // (genuinely uncovered codepoints, and every char on platforms with no
    // implementation) and costs as much to re-derive as a hit, so the
    // attempted-set is recorded before the outcome is known.
    if (!mAttemptedFallbackChars.insert(wch).second)
        return { nullptr, 0u };

    LLFontFallbackMatch match = LLWindow::findFallbackFontForChar(wch);
    if (match.mPath.empty() || hasFallbackPath(match.mPath))
        return { nullptr, 0u };

    // Open at this font's size and DPI so the lazily-discovered face lines
    // up with the head's metrics.
    LLPointer<LLFontFreetype> fallback = new LLFontFreetype;
    if (!fallback->loadFace(match.mPath, mPointSize, mVertDPI, mHorzDPI,
                            /*is_fallback*/ true, match.mFaceIndex,
                            mHinting, mFontFlags))
    {
        return { nullptr, 0u };
    }

    U32 glyph_index = fallback->getCharGlyphIndex(wch);
    if (!glyph_index)
    {
        // The OS matched a font that doesn't actually cover wch: discard it
        // rather than lengthening the chain with a face that can never hit.
        return { nullptr, 0u };
    }

    LL_DEBUGS("Font") << "Lazy OS fallback for U+" << std::hex << (U32)wch << std::dec
                      << ": " << match.mPath << " (face " << match.mFaceIndex << ")" << LL_ENDL;
    // Note: this clears mShapingFaceResolution, so callers must not hold a
    // cached resolution across this point.
    addFallbackFont(fallback, nullptr);
    mLazyFallbacks.push_back(fallback);
    return { fallback.get(), glyph_index };
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

F32 LLFontFreetype::getUnderlinePosition() const
{
    ALFT_Face ft = getFTFace();
    if (!ft || ft->units_per_EM <= 0)
        return -mDescender;
    const F32 scale = (F32)ft->size->metrics.y_ppem / (F32)ft->units_per_EM;
    return (F32)ft->underline_position * scale;
}

F32 LLFontFreetype::getUnderlineThickness() const
{
    ALFT_Face ft = getFTFace();
    if (!ft || ft->units_per_EM <= 0)
        return 1.f;
    const F32 scale = (F32)ft->size->metrics.y_ppem / (F32)ft->units_per_EM;
    return llmax(1.f, (F32)ft->underline_thickness * scale);
}

F32 LLFontFreetype::getXAdvance(llwchar wch) const
{
    if (getFTFace() == nullptr)
        return 0.0;

    // getGlyphInfo always returns a non-null entry once the face is loaded
    // (chain-walk misses route through the shared notdef pre-warmed at
    // load time), so the `gi != nullptr` guard plus a getMaxCharWidth
    // fallback that used to live here are dead in practice. Defensive
    // null check kept since a notdef render failure could theoretically
    // surface here.
    LLFontGlyphInfo* gi = getGlyphInfo(wch, EFontGlyphType::Unspecified);
    return gi ? gi->mXAdvance : 0.f;
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

    // Kerning is defined within one face: the legacy 'kern' table maps
    // pairs of THAT face's glyph indices. Probe the table of the face that
    // owns both glyphs — historically this always probed the head's table,
    // which fed it foreign indices whenever the glyphs came from a fallback
    // (benign for GPOS-era fonts, which ship no legacy kern table, but
    // wrong in principle and garbage-prone on old fonts — and it silently
    // dropped real kerning for pairs that DO share a fallback face, e.g.
    // two CJK glyphs). A mixed-face pair has no defined kerning at all.
    if (!left_glyph_info || !right_glyph_info)
        return 0.f;
    const ALFontFace* source_face = left_glyph_info->mSourceFace;
    if (!source_face || source_face != right_glyph_info->mSourceFace)
        return 0.f;
    ALFT_Face kern_face = source_face->face();
    if (!kern_face)
        return 0.f;

    U32 left_glyph = left_glyph_info->mGlyphIndex;
    U32 right_glyph = right_glyph_info->mGlyphIndex;

    FT_Vector  delta;

    // UNFITTED gives subpixel-precise kerning when callers maintain a
    // fractional pen accumulator (mUseSubpixelPen — autohinted/unhinted
    // faces). DEFAULT grid-fits to integer pixels, which is what callers
    // want when they round per glyph (native-hinted faces). The pen policy
    // is the head's: it owns the layout loop the result feeds.
    const FT_UInt kern_mode = mUseSubpixelPen ? FT_KERNING_UNFITTED : FT_KERNING_DEFAULT;
    llverify(!FT_Get_Kerning(kern_face, left_glyph, right_glyph, kern_mode, &delta));

    // Apply the FreeType auto-hinter's subpixel side-bearing correction between
    // adjacent glyphs. The lsb/rsb deltas are populated only when the autohinter
    // ran; for native-hinted (DEFAULT) and unhinted (NO_HINTING) loads they're
    // always zero and the correction is meaningless.
    F32 delta_correction = 0.0f;
    if (mHinting == EFontHinting::FORCE_AUTOHINT)
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
                // Unusual modes (LCD / LCD_V / GRAY2 / GRAY4) shouldn't
                // arrive here — our render configuration only requests
                // NORMAL/LIGHT/COLOR — but a single misbehaving system
                // font is enough to surface them. Warn and treat as
                // Grayscale; the bitmap-copy block below only handles
                // MONO/GRAY/BGRA so we'll skip the data copy too, which
                // means the slot stays zero (notdef-shaped quad with no
                // alpha). Better than aborting startup.
                LL_WARNS_ONCE("Font") << "Unsupported FT pixel_mode "
                    << (S32)fontp->getFTFace()->glyph->bitmap.pixel_mode
                    << " for glyph " << glyph_index
                    << "; treating as Grayscale" << LL_ENDL;
                phase_type = EFontGlyphType::Grayscale;
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
        // a common fallback face share these slots. Pass height so the
        // allocator can advance Y past tall color-emoji bitmaps even when
        // mMaxCharHeight (derived from the outline bbox) underestimates them.
        fontp->getBitmapCache()->nextOpenPos(width, height, pos_x, pos_y, bitmap_glyph_type, bitmap_num);

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
            // Pass the signed pitch through — setSubImageBGRA walks rows
            // with signed arithmetic so a negative-pitch (bottom-up) source
            // lands on the right bytes.
            fontp->mFace->setSubImageBGRA(pos_x, pos_y, bitmap_num,
                            fontp->getFTFace()->glyph->bitmap.width,
                            fontp->getFTFace()->glyph->bitmap.rows,
                            fontp->getFTFace()->glyph->bitmap.buffer,
                            fontp->getFTFace()->glyph->bitmap.pitch);
        }
        else
        {
            // Mirrors the soft fallback in the pixel_mode switch above:
            // unusual modes (LCD / LCD_V / GRAY2 / GRAY4) leave the slot
            // zero rather than crashing debug builds.
        }

        LLImageGL *image_gl = fontp->getBitmapCache()->getImageGL(bitmap_glyph_type, bitmap_num);
        LLImageRaw *image_raw = fontp->getBitmapCache()->getImageRaw(bitmap_glyph_type, bitmap_num);
        if (image_gl && image_raw)
        {
            // Upload only the dirty glyph rect — passing full atlas dimensions
            // routes through setImage() and re-uploads the entire 1024×1024 page
            // (2 MB grayscale / 4 MB BGRA) for every new glyph.
            image_gl->setSubImage(image_raw, pos_x, pos_y, width, height, /*force_fast_update=*/true, 0, true);
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

LLFontGlyphInfo* LLFontFreetype::addShapedGlyphFromFont(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType requested_glyph_type) const
{
    // Cross-head dedup: a sibling head sharing fontp's face may have already
    // rasterized this glyph. Reuse the face's cached entry rather than
    // allocating a new atlas slot.
    if (fontp && fontp->mFace)
    {
        if (LLFontGlyphInfo* existing = fontp->mFace->findGlyphInfo(glyph_index, requested_glyph_type))
            return existing;
    }

    EFontGlyphType bitmap_glyph_type;
    LLFontGlyphInfo* gi = renderAndCreateGlyph(fontp, glyph_index, requested_glyph_type, bitmap_glyph_type);
    if (!gi)
        return nullptr;

    // insertGlyphInfo keeps an already-published entry over the incoming
    // one (deleting the duplicate), so continue with its return value.
    gi = fontp->mFace->insertGlyphInfo(glyph_index, gi);

    // Optimization: when the rendered pixel format differs from what the
    // caller requested (e.g. Color requested but the file is monochrome),
    // also publish the entry under the bitmap_type so a future lookup that
    // asks for that type hits dedup. Skip the secondary publish if a
    // bitmap_type entry already exists; the existing one is correct.
    if (requested_glyph_type != bitmap_glyph_type
        && !fontp->mFace->findGlyphInfo(glyph_index, bitmap_glyph_type))
    {
        LLFontGlyphInfo* gi_temp = new LLFontGlyphInfo(*gi);
        gi_temp->mGlyphType = bitmap_glyph_type;
        fontp->mFace->insertGlyphInfo(glyph_index, gi_temp);
    }

    return gi;
}

LLFontGlyphInfo* LLFontFreetype::getGlyphInfo(llwchar wch, EFontGlyphType glyph_type) const
{
    if (!getFTFace())
        return nullptr;
    llassert(!mIsFallback);

    // glyph_type passes through unresolved: getGlyphInfoByIndex treats
    // Unspecified as match-any at lookup and only pins a concrete type
    // when it has to rasterize.

    // Hot path: codepoint exists on this head's face. One cmap lookup
    // (cached) + one (face, glyph_index) hash lookup.
    U32 glyph_index = getCharGlyphIndex(wch);
    if (glyph_index != 0)
        return getGlyphInfoByIndex(this, glyph_index, glyph_type);

    // Cold path: walk fallbacks in codepoint-priority order, which
    // differs from the shape-path priority in selectShapingFace:
    //   1. Emoji-functor fallbacks gated on isPictographBase(wch). Same
    //      predicate the shape itemizer uses — codepoint-path and
    //      shape-path agree on which characters are emoji-eligible, so
    //      BMP pictographs (heart etc.) route to the colour face here
    //      just as they do during shaping.
    //   2. Monochrome fallbacks (no functor). Priority over emoji
    //      fallbacks for non-pictograph chars so legacy UI elements
    //      (LSL dialogs, menu checkmarks at U+2611 etc.) still pick
    //      monochrome.
    //   3. Emoji-functor fallbacks ignoring the functor — last-resort
    //      coverage for codepoints that aren't pictographs and aren't
    //      in a monochrome fallback but DO exist in an emoji font's
    //      charmap.
    if (LLStringOps::isPictographBase(wch))
    {
        if (auto hit = find_fallback_hit(mFallbackFonts, wch,
                [&](const char_functor_t& f) { return f && f(wch); });
            hit.first)
        {
            return getGlyphInfoByIndex(hit.first, hit.second, glyph_type);
        }
    }
    if (auto hit = find_fallback_hit(mFallbackFonts, wch,
            [](const char_functor_t& f) { return !f; });
        hit.first)
    {
        return getGlyphInfoByIndex(hit.first, hit.second, glyph_type);
    }
    if (auto hit = find_fallback_hit(mFallbackFonts, wch,
            [](const char_functor_t& f) { return (bool)f; });
        hit.first)
    {
        return getGlyphInfoByIndex(hit.first, hit.second, glyph_type);
    }

    // Nothing in the configured chain covers this codepoint: ask the OS for
    // a face that does and attach it. Keeps scripts the static fallback list
    // never named (CJK, Thai, Indic) from rendering as tofu.
    if (auto hit = attachOsFallbackFor(wch); hit.first)
    {
        return getGlyphInfoByIndex(hit.first, hit.second, glyph_type);
    }

    // No face in our chain has this codepoint. Use the head face's
    // notdef (glyph_index=0) — pre-warmed during loadFace.
    return getGlyphInfoByIndex(this, 0, glyph_type);
}

LLFontGlyphInfo* LLFontFreetype::getGlyphInfoByIndex(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType glyph_type) const
{
    // Single source of truth: the source face's owning glyph cache. The head
    // used to memoize this lookup in its own (fontp, glyph_index) map, but
    // that introduced a dangling-pointer hazard on cross-face GC — the face
    // would delete an entry in collectGarbage while sibling heads' maps
    // still held non-owning copies of the pointer, and the next render
    // would either dereference freed memory or short-circuit on a stuck
    // bitmap_entry that pointed at a released sheet (manifesting as glyphs
    // that "appear unloaded and never reload" after long idle periods).
    // Routing every lookup through the face cache means atlas eviction
    // is observed consistently by every freetype that ever rendered the
    // glyph: the face's findGlyphInfo returns null after eviction, so we
    // fall through to addShapedGlyphFromFont and re-rasterize.
    //
    // Unspecified probes the cache as match-any (findGlyphInfo returns the
    // first entry of either type) and only resolves to Grayscale when the
    // glyph has to be rasterized. Measurement paths pass Unspecified — they
    // need metrics, not a particular atlas — and resolving before the probe
    // forced a duplicate Grayscale rasterization (a full COLRv1 paint walk
    // plus a Grayscale atlas slot) for every color glyph that had already
    // been rendered through the Color atlas.
    //
    // A face with neither a color table (CBDT/sbix/COLR) nor OT-SVG can
    // never produce a Color bitmap — FT_LOAD_COLOR on it rasterizes the
    // same grayscale outline. Degrade the request up front so Color
    // lookups on plain text faces (use_color=true is the UI default for
    // every glyph, not just emoji) hit the existing Grayscale entry
    // instead of missing and rasterizing a duplicate gray bitmap into
    // fresh atlas slots published under a Color-typed entry. Reload paths
    // that pre-warm ASCII as Grayscale made that the common case: the
    // whole ASCII set stored twice per font, x8 phase slots on subpixel
    // faces.
    if (glyph_type == EFontGlyphType::Color
        && fontp && fontp->mFace
        && !fontp->mFace->hasColor()
        && !fontp->mFace->hasSvg())
    {
        glyph_type = EFontGlyphType::Grayscale;
    }
    if (fontp && fontp->mFace)
    {
        if (LLFontGlyphInfo* gi = fontp->mFace->findGlyphInfo(glyph_index, glyph_type))
            return gi;
    }
    const EFontGlyphType resolve_type = (EFontGlyphType::Unspecified != glyph_type) ? glyph_type : EFontGlyphType::Grayscale;
    return addShapedGlyphFromFont(fontp, glyph_index, resolve_type);
}

void LLFontFreetype::renderGlyph(EFontGlyphType bitmap_type, U32 glyph_index, llwchar wch) const
{
    if (getFTFace() == nullptr)
        return;

    // COLRv1 fast path. FreeType's FT_LOAD_COLOR + FT_Render_Glyph rasterize
    // COLRv0 / sbix / CBDT / OT-SVG natively but explicitly NOT COLRv1; for
    // those faces we hand the paint walk off to HarfBuzz hb-raster. Runs
    // for both Color and Grayscale requests:
    // Color rasterizes BGRA into the Color atlas, Grayscale folds the paint
    // tree into a luminance-shaded mask that the Grayscale atlas tints with
    // text_color at draw time. Either bitmap_type request hits the COLRv1
    // path on COLRv1 faces; the FT outline fallback below handles glyphs
    // whose paint tree the walker can't process.
    if (mFace && mFace->hasColrV1()
        && (bitmap_type == EFontGlyphType::Color || bitmap_type == EFontGlyphType::Grayscale))
    {
        if (renderColrV1Glyph(glyph_index, bitmap_type))
        {
            mRenderGlyphCount++;
            return;
        }
        // Fallthrough: walker bailed (unsupported paint feature / alloc / no
        // paint tree on this glyph). The standard FT path runs next; its
        // existing retry block downgrades a failed color render to grayscale,
        // so glyphs without a paint tree still produce a usable bitmap.
    }

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
            LL_ERRS() << "Out of memory loading glyph for character " << llformat("U+%X", U32(wch)) << LL_ENDL;
        }

        std::string message = llformat(
            "Error %d (%s) loading wchar %u glyph %u/%u: bitmap_type=%u, load_flags=%d",
            error, FT_Error_String(error), wch, glyph_index, getFTFace()->num_glyphs, bitmap_type, load_flags);
        LL_WARNS_ONCE() << message << LL_ENDL;
        // Retry without color rendering — most common cause of failure on
        // marginal color/SVG glyphs. Use & ~FT_LOAD_COLOR to clear the bit;
        // an XOR would *add* it on a Grayscale request.
        error = FT_Load_Glyph(getFTFace(), glyph_index, load_flags & ~FT_LOAD_COLOR);
        if (FT_Err_Ok != error)
        {
            // value~0 always corresponds to the 'missing glyph'. Unconditional
            // last-resort fallback so any retry failure (not just Invalid_*
            // outlines or emoji codepoints) renders the notdef box rather than
            // hitting the assert below.
            error = FT_Load_Glyph(getFTFace(), 0, FT_LOAD_DEFAULT);
            if (FT_Err_Ok != error)
            {
                LL_ERRS() << "Loading fallback for char '" << (U32)wch << "', glyph " << glyph_index << " failed with error : " << (S32)error << LL_ENDL;
            }
        }
        llassert_always_msg(FT_Err_Ok == error, message.c_str());
    }

    // An embedded bitmap strike and sbix / CBDT colour glyphs arrive already
    // rasterized, with the slot's format left as FT_GLYPH_FORMAT_BITMAP.
    // Rendering those again would throw the bitmap away. FT_Load_Glyph clears
    // the slot on entry, so a buffer present here always belongs to this load.
    if (!getFTFace()->glyph->bitmap.buffer)
    {
        // FT_Render_Glyph's mode arg overrides the load_flags' target. For
        // LIGHT we want the lighter stem-weight filter to match the LIGHT
        // autohinter's no-horizontal-fit output; everything else uses NORMAL.
        const FT_Render_Mode render_mode = (mHinting == EFontHinting::LIGHT)
            ? FT_RENDER_MODE_LIGHT
            : FT_RENDER_MODE_NORMAL;
        if (FT_Render_Glyph(getFTFace()->glyph, render_mode) != 0)
        {
            LL_WARNS() << "Failed to render glyph for character " << llformat("U+%X", U32(wch))
                       << " at glyph index " << glyph_index << LL_ENDL;
            // LIGHT is the only non-default target we ask for; fall back to
            // NORMAL rather than leaving the slot without a bitmap.
            if (render_mode != FT_RENDER_MODE_NORMAL
                && FT_Render_Glyph(getFTFace()->glyph, FT_RENDER_MODE_NORMAL) != 0)
            {
                LL_WARNS() << "Fallback to FT_RENDER_MODE_NORMAL also failed for character "
                           << llformat("U+%X", U32(wch)) << LL_ENDL;
            }
        }
    }

    mRenderGlyphCount++;
}

bool LLFontFreetype::renderColrV1Glyph(U32 glyph_index, EFontGlyphType requested) const
{
    ALFT_Face ft = getFTFace();
    if (!ft || !mFace)
        return false;

    // Outline-mode load just for metrics (advance, lsb/rsb deltas). NO_BITMAP
    // skips FT producing a bitmap we'd immediately discard. NO_COLOR makes
    // sure we don't get FT's COLRv0 fallback by accident.
    const FT_Int32 metrics_flags = ((FT_Int32)mHinting & ~FT_LOAD_COLOR) | FT_LOAD_NO_BITMAP;
    if (FT_Load_Glyph(ft, glyph_index, metrics_flags) != FT_Err_Ok)
        return false;

    // Reuse the painter (and its scratch buffers) across all renderGlyph
    // calls on this thread. The painter holds no per-face state; declaring
    // it static thread_local amortizes the small amount of construction cost
    // and keeps the staging buffer sized to the running max glyph.
    static thread_local ALFontColrV1Painter s_painter;
    ALFontColrV1Painter::Result result;
    // Foreground color: white for both BGRA and Gray paths. The BGRA atlas
    // tints with white at draw time so CPAL palette colors come through
    // verbatim; the Gray path needs white for the
    // luminance-collapses-to-alpha invariant (max(Y, a*floor) == a holds
    // iff R=G=B=A) so the rendered silhouette tints cleanly with
    // text_color. Mixed CPAL+foreground glyphs render their foreground
    // regions in white — threading the live text color through here is a
    // follow-up.
    const LLColor4U emoji_fg(255, 255, 255, 255);
    const ALFontColrV1Painter::OutputFormat format =
        (requested == EFontGlyphType::Grayscale)
            ? ALFontColrV1Painter::OutputFormat::Gray
            : ALFontColrV1Painter::OutputFormat::BGRA;
    if (!s_painter.paintGlyph(mFace->getHbFont(), glyph_index, mPointSize,
                              emoji_fg, mFace->paletteIndex(), format, result))
    {
        return false;
    }
    if (!result.mBitmap || result.mWidth <= 0 || result.mHeight <= 0)
        return false;

    // Patch FT's glyph slot in place. The existing pixel_mode switch in
    // renderAndCreateGlyph reads exactly these fields and is oblivious to
    // whether FT or our painter populated them. The slot resets on the next
    // FT_Load_Glyph call, so the stale buffer pointer doesn't outlive its
    // owner (the painter's mStaging vector, valid until next paintGlyph).
    FT_Bitmap& bm = ft->glyph->bitmap;
    bm.width      = static_cast<unsigned>(result.mWidth);
    bm.rows       = static_cast<unsigned>(result.mHeight);
    bm.pitch      = result.mPitch;
    bm.buffer     = const_cast<unsigned char*>(result.mBitmap);
    bm.pixel_mode = (format == ALFontColrV1Painter::OutputFormat::Gray)
        ? (unsigned char)FT_PIXEL_MODE_GRAY
        : (unsigned char)FT_PIXEL_MODE_BGRA;
    bm.num_grays  = 256;
    ft->glyph->bitmap_left = result.mLeft;
    ft->glyph->bitmap_top  = result.mTop;
    return true;
}

void LLFontFreetype::resetSelf(F32 vert_dpi, F32 horz_dpi)
{
    // Reset just this instance — clear its glyph caches, drop the previous
    // face wrapper, and re-loadFace at the new DPI. Doesn't recurse into
    // mFallbackFonts because shared fallback instances should be reset
    // once by whoever owns the shared cache
    // (LLFontRegistry::reloadForDpiChange).
    resetBitmapCache();
    loadFace(mName, mPointSize, vert_dpi, horz_dpi, mIsFallback, mFaceIndex, mHinting, mFontFlags, mVarAxes);

    // Fallbacks we discovered through the OS are owned solely by this head,
    // so the registry's cache-driven pass never reaches them. Without this
    // they keep the old DPI while the head re-resolves, and
    // mAttemptedFallbackChars stops them from ever being re-queried.
    for (LLPointer<LLFontFreetype>& lazy : mLazyFallbacks)
    {
        lazy->resetSelf(vert_dpi, horz_dpi);
    }
}

void LLFontFreetype::reset(F32 vert_dpi, F32 horz_dpi)
{
    // Standalone API for callers outside the registry: do the full cascade
    // (head plus its fallback chain). Inside the registry we use resetSelf
    // and drive the fallback resets ourselves — see
    // LLFontRegistry::reloadForDpiChange.
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
    // No-op for the head's own state — atlas + glyph map are owned by
    // ALFontFace and shared across freetypes that resolve to the same
    // key. For DPI/scale changes that drive resetSelf, loadFace picks up
    // a new face wrapper (different ALFontFaceKey) and the old wrapper
    // drops to refcount 0 (naturally tearing down its atlas) once all
    // heads have reloaded. For same-key resets the face cache is reused
    // and any previously-rasterized glyphs remain valid.
    if (!mIsFallback && mFace && !mFace->findGlyphInfo(0, EFontGlyphType::Grayscale))
    {
        // Pre-warm notdef only if the face cache doesn't already have it.
        // Fresh face → insert; reused face → already-pre-warmed by an
        // earlier head, skip.
        addShapedGlyphFromFont(this, 0, EFontGlyphType::Grayscale);
    }
}

void LLFontFreetype::destroyGL()
{
    if (mFace)
        mFace->destroyGL();

    // Recurse into the fallback chain so each fallback face's atlas is
    // cleared too. The registry's destroyGL iterates mFontMap (heads
    // only), so without this recursion fallback faces — only reachable
    // through the head's mFallbackFonts — would survive a teardown with
    // stale atlas content. After a font reload that lands a new freetype
    // on the SAME ALFontFaceKey (e.g. point size unchanged for a
    // particular descriptor), getOrCreateFace returns the cached face
    // with whatever glyphs the previous run rasterized into it.
    // Idempotent: multiple heads may share a fallback face via
    // mFallbackInstanceCache; calling destroyGL on an already-cleared
    // face is a no-op. mIsFallback gate avoids descending into a
    // fallback's own (empty) mFallbackFonts vector — only heads carry
    // a populated chain.
    if (!mIsFallback)
    {
        for (auto& fb : mFallbackFonts)
        {
            if (fb.first)
                fb.first->destroyGL();
        }
    }
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
    // The sweep (and its throttle) lives on the shared face wrapper — the
    // atlas and glyph map are face state, and siblings wrapping the same
    // face should cost one sweep per interval, not one per head.
    if (mFace)
        mFace->collectGarbage();
}

U8 LLFontFreetype::getStyle() const
{
    return mStyle;
}

// (setSubImageBGRA / setSubImageLuminanceAlpha moved to ALFontFace —
// they operate purely on the atlas, which now lives on the face wrapper.)
// (setVariationAxis moved to ALFontFace::setVariationAxis — face state.)

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
            }
            std::string mName;
            std::string mAddress;
            std::size_t mSize;
        };
    }
}

U8 const* LLFontManager::loadFont( std::string const &aFilename, long &a_Size)
{
    try
    {
        a_Size = 0;
        std::map< std::string, std::shared_ptr<ll::fonts::LoadedFont> >::iterator itr = m_LoadedFonts.find(aFilename);
        if (itr != m_LoadedFonts.end())
        {
            // A possible overflow cannot happen here, as it is asserted that the size is less than std::numeric_limits<long>::max() a few lines below.
            a_Size = static_cast<long>(itr->second->mSize);
            return reinterpret_cast<U8 const*>(itr->second->mAddress.c_str());
        }

        auto strContent = LLFile::getContents(aFilename);

        if (strContent.empty())
            return nullptr;

        // For fontconfig a type of long is required, std::string::size() returns size_t. I think it is safe to limit this to 2GiB and not support fonts that huge (can that even be a thing?)
        llassert_always(strContent.size() < std::numeric_limits<long>::max());

        a_Size = static_cast<long>(strContent.size());

        auto pCache = std::make_shared<ll::fonts::LoadedFont>(aFilename, strContent, a_Size);
        itr = m_LoadedFonts.insert(std::make_pair(aFilename, pCache)).first;

        return reinterpret_cast<U8 const*>(itr->second->mAddress.c_str());
    }
    catch (const std::bad_alloc&)
    {
        LLError::LLUserWarningMsg::showOutOfMemory();
        LL_ERRS() << "Failed to load font. Out of memory." << LL_ENDL;
    }
    return nullptr;
}

LLPointer<ALFontFace> LLFontManager::getOrCreateFace(const ALFontFaceKey& key)
{
    auto it = mFaceCache.find(key);
    if (it != mFaceCache.end())
        return it->second;

    LLPointer<ALFontFace> face = new ALFontFace;
    if (!face->load(key.filename, key.face_index, key.point_size,
                    key.vert_dpi, key.horz_dpi, key.hinting, key.flags,
                    key.var_axes))
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

void LLFontManager::collectGarbage()
{
    // Sweep mFaceCache: every live LLFontFreetype holds an
    // LLPointer<ALFontFace> mFace, so the map's own LLPointer is the sole
    // ref iff getNumRefs() == 1. Same-order rule as unloadAllFonts —
    // ~ALFontFace runs FT_Done_Face, which dereferences the FT_OPEN_MEMORY
    // bytes still in m_LoadedFonts.
    std::size_t faces_trimmed = 0;
    for (auto it = mFaceCache.begin(); it != mFaceCache.end(); )
    {
        if (it->second->getNumRefs() == 1)
        {
            it = mFaceCache.erase(it);
            ++faces_trimmed;
        }
        else
        {
            ++it;
        }
    }

    // Sweep m_LoadedFonts: byte buffers are consumed only by surviving
    // ALFontFace entries via FT_OPEN_MEMORY (see ALFontFace::load).
    // Anything whose filename no longer keys any surviving face is dead.
    std::unordered_set<std::string> live_filenames;
    live_filenames.reserve(mFaceCache.size());
    for (const auto& kv : mFaceCache)
        live_filenames.insert(kv.first.filename);

    std::size_t loaded_trimmed = 0;
    for (auto it = m_LoadedFonts.begin(); it != m_LoadedFonts.end(); )
    {
        if (live_filenames.find(it->first) == live_filenames.end())
        {
            it = m_LoadedFonts.erase(it);
            ++loaded_trimmed;
        }
        else
        {
            ++it;
        }
    }

    if (faces_trimmed || loaded_trimmed)
    {
        LL_INFOS() << "LLFontManager::collectGarbage: trimmed "
                   << faces_trimmed << " faces, "
                   << loaded_trimmed << " loaded fonts" << LL_ENDL;
    }
}
