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
    // addGlyphFromFont / addShapedGlyphFromFont copies an existing entry
    // and republishes it under the actual bitmap_glyph_type, and the
    // renderer reads sfgi->mSourceFace to pick the atlas. A null here
    // produces a silent no-op render for typed lookups that hit the dup.
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
    // (LLFontFreetype*, glyph_index); ours are about to dangle. Other
    // faces' entries stay valid, so only drop ours.
    LLFontShaping::clearCacheForFace(this);

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

namespace
{
    // Walk a fallback list and return the (face, glyph_index) for the first
    // entry where `keep(functor)` returns true AND the face's charmap has
    // `wch`. Returns (nullptr, 0) when no entry hits.
    //
    // This is the iteration mechanic shared between the codepoint-path
    // (addGlyph) and shape-path (selectShapingFace) chain walks. The two
    // policies still differ — see each call site for which passes run and
    // why — but the per-pass loop is identical and lives here.
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

    llassert(!mIsFallback);

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

bool LLFontFreetype::loadFace(const std::string& filename, F32 point_size, F32 vert_dpi, F32 horz_dpi, S32 weight, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags)
{
    if (mFace)
    {
        // Reload: cached shape entries depend on the previous face state,
        // and our resolution cache holds entries from the old fallback chain.
        // Only this face's entries become stale here — siblings keep theirs.
        LLFontShaping::clearCacheForFace(this);
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

    // FreeType's size->metrics is populated by FT_Set_Char_Size (run inside
    // LLFontFace::load) with scaled, 26.6 fractional-pixel ascender /
    // descender / height values. Reading them directly matches FT's own
    // accounting exactly — re-deriving from design units would diverge by
    // sub-pixel due to FreeType's internal 16.16 y_scale rounding.
    constexpr F32 INV_64 = 1.f / 64.f;
    const FT_Size_Metrics& metrics = ft->size->metrics;
    mAscender   =  metrics.ascender  * INV_64;
    mDescender  = -metrics.descender * INV_64;  // FT descender is negative; flip to positive depth.
    mLineHeight =  metrics.height    * INV_64;

    // The atlas (LLFontBitmapCache) is owned by mFace and was initialized
    // inside LLFontFace::load; nothing to do here for atlas setup.

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

F32 LLFontFreetype::getUnderlinePosition() const
{
    LLFT_Face ft = getFTFace();
    if (!ft || ft->units_per_EM <= 0)
        return -mDescender;
    const F32 scale = (F32)ft->size->metrics.y_ppem / (F32)ft->units_per_EM;
    return (F32)ft->underline_position * scale;
}

F32 LLFontFreetype::getUnderlineThickness() const
{
    LLFT_Face ft = getFTFace();
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
    // (addGlyph routes misses through the shared notdef pre-warmed at
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
        // No glyph on this face: walk fallbacks in the codepoint-path
        // priority order, which differs from the shape-path priority in
        // selectShapingFace:
        //   1. Emoji-functor fallbacks, gated on isEmoji(wch). The
        //      isEmoji gate is stricter than the functor's own range —
        //      kept defensively in case different emoji fonts' functors
        //      partition the emoji range differently in the future.
        //   2. Monochrome fallbacks (no functor). Priority over emoji
        //      for non-genuine-emoji chars so legacy UI elements
        //      (LSL dialogs, menu checkmarks) still pick monochrome.
        //   3. Emoji-functor fallbacks ignoring the functor — last-
        //      resort coverage for codepoints that aren't isEmoji and
        //      aren't in a monochrome fallback but DO exist in an emoji
        //      font's charmap.
        //
        // The shape path skips passes 1's isEmoji gate and pass 3
        // entirely; comments at selectShapingFace explain why.

        if (LLStringOps::isEmoji(wch))
        {
            if (auto hit = find_fallback_hit(mFallbackFonts, wch,
                    [&](const char_functor_t& f) { return f && f(wch); });
                hit.first)
            {
                return addGlyphFromFont(hit.first, wch, hit.second, glyph_type);
            }
        }
        if (auto hit = find_fallback_hit(mFallbackFonts, wch,
                [](const char_functor_t& f) { return !f; });
            hit.first)
        {
            return addGlyphFromFont(const_cast<LLFontFreetype*>(hit.first),
                                    wch, hit.second, glyph_type);
        }
        if (auto hit = find_fallback_hit(mFallbackFonts, wch,
                [](const char_functor_t& f) { return (bool)f; });
            hit.first)
        {
            return addGlyphFromFont(const_cast<LLFontFreetype*>(hit.first),
                                    wch, hit.second, glyph_type);
        }
    }

    auto range_it = mCharGlyphInfoMap.equal_range(wch);
    char_glyph_info_map_t::iterator iter =
        std::find_if(range_it.first, range_it.second,
                     [&glyph_type](const char_glyph_info_map_t::value_type& entry)
                     {
                        return entry.second->mGlyphType == glyph_type;
                     });
    if (iter != range_it.second)
    {
        // Already cached. Reachable when getGlyphInfo was called with
        // Unspecified (wildcard) and routed here on a miss for the
        // strict glyph_type, but the head's map happens to have the
        // entry under a different filter path. Return the existing
        // entry rather than nullptr — returning null pushes callers
        // onto dead fallback branches and silently mis-renders.
        return iter->second;
    }

    if (glyph_index != 0)
    {
        // Real glyph in this face — render and cache by wch.
        return addGlyphFromFont(this, wch, glyph_index, glyph_type);
    }

    // No face in our chain has this codepoint. Reuse the face's pre-warmed
    // notdef entry (cached at wch=0 during loadFace) instead of rasterizing
    // a per-codepoint copy. Caching notdef under each missing wch in the
    // face's map would pollute it with entries that aren't really "this
    // face has wch", and sibling heads would then short-circuit their own
    // chain walk and pick up our notdef when their primary face actually
    // has the glyph.
    if (mFace)
    {
        if (LLFontGlyphInfo* notdef = mFace->findGlyphInfo(0, glyph_type))
        {
            // Mirror into the head's local cache for fast-path repeat lookups.
            insertGlyphInfo(wch, notdef);
            return notdef;
        }
        // Pre-warm wasn't done (e.g. on a path that loaded without it).
        // Render once at glyph_index 0 and cache under wch=0.
        if (LLFontGlyphInfo* notdef = addGlyphFromFont(this, 0, 0, glyph_type))
        {
            // addGlyphFromFont already inserted under wch=0 on the head;
            // also remember the same pointer under the requested wch.
            insertGlyphInfo(wch, notdef);
            return notdef;
        }
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

            fontp->mFace->setSubImageGrayscale(pos_x, pos_y, bitmap_num, width, height,
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
    // Cross-head dedup: a sibling head sharing fontp's face may have already
    // rasterized this glyph. Reuse the existing entry rather than allocating
    // a new atlas slot.
    //
    // The dedup gate has two halves:
    //   - `glyph_index != 0`: the codepoint genuinely lives in fontp; the
    //     face cache key (wch) is meaningful and deduping is safe.
    //   - `wch == 0`: the loadFace pre-warm sentinel for the notdef glyph,
    //     also keyed at wch=0 so heads share a single notdef render.
    // For the (glyph_index == 0, wch != 0) case — caller wanted a glyph
    // that fontp doesn't have — addGlyph routes through the shared-notdef
    // path before reaching us, so we never legitimately see this combo;
    // the gate keeps us from caching a bogus entry under wch if anyone
    // calls in directly.
    if (fontp->mFace && (glyph_index != 0 || wch == 0))
    {
        if (LLFontGlyphInfo* existing = fontp->mFace->findGlyphInfo(wch, requested_glyph_type))
        {
            insertGlyphInfo(wch, existing);
            return existing;
        }
    }

    EFontGlyphType bitmap_glyph_type;
    LLFontGlyphInfo* gi = renderAndCreateGlyph(fontp, glyph_index, requested_glyph_type, bitmap_glyph_type);
    if (!gi)
        return nullptr;

    // Face owns the glyph info entry; head's map holds a non-owning pointer
    // for fast lookup. Insert into face first so head's pointer is to the
    // canonical entry.
    fontp->mFace->insertGlyphInfo(wch, gi);
    insertGlyphInfo(wch, gi);

    // Optimization: when the rendered pixel format differs from what the
    // caller requested (e.g. Color requested but the file is monochrome),
    // also publish the entry under the bitmap_type so a future lookup that
    // asks for that type hits dedup. We must NOT replace an existing entry
    // here — sibling heads sharing this face hold non-owning pointers to
    // the existing entry in their local resolution caches, and replacing
    // would delete the entry under them. Skip the secondary publish if a
    // bitmap_type entry already exists; the existing one is correct.
    if (requested_glyph_type != bitmap_glyph_type
        && !fontp->mFace->findGlyphInfo(wch, bitmap_glyph_type))
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

    // Same dangling-pointer hazard as in addGlyphFromFont above: skip the
    // bitmap_type secondary publish if an entry already exists, otherwise
    // we'd delete an LLFontGlyphInfo that sibling heads still reference.
    if (requested_glyph_type != bitmap_glyph_type
        && !fontp->mFace->findShapedGlyphInfo(glyph_index, bitmap_glyph_type))
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

    // Head missed. Defer to addGlyph for the chain walk: the SOURCE face
    // for `wch` is determined by faceHasGlyph priority order, not by which
    // face happens to have a cached entry. Cross-head dedup is then handled
    // inside addGlyphFromFont, which checks the source face's cache before
    // rasterizing. Walking face caches here would be wrong — a sibling
    // head's earlier render through a face that's a *fallback* in our
    // chain could shadow our *primary* face's render of the same codepoint.
    return addGlyph(wch,
        (EFontGlyphType::Unspecified != glyph_type) ? glyph_type : EFontGlyphType::Grayscale);
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

    // FT_Render_Glyph's mode arg overrides the load_flags' target. For
    // LIGHT we want the lighter stem-weight filter to match the LIGHT
    // autohinter's no-horizontal-fit output; everything else uses NORMAL.
    const FT_Render_Mode render_mode = (mHinting == EFontHinting::LIGHT)
        ? FT_RENDER_MODE_LIGHT
        : FT_RENDER_MODE_NORMAL;
    if (FT_Render_Glyph(getFTFace()->glyph, render_mode) != 0)
    {
        LL_WARNS() << "Failed to render glyph for character " << llformat("U+%X", U32(wch)) << " at glyph index " << glyph_index << LL_ENDL;
    }

    mRenderGlyphCount++;
}

void LLFontFreetype::resetSelf(F32 vert_dpi, F32 horz_dpi)
{
    // Reset just this instance — clear its glyph caches, drop the previous
    // face wrapper, and re-loadFace at the new DPI. Doesn't recurse into
    // mFallbackFonts because shared fallback instances should be reset
    // once by whoever owns the shared cache
    // (LLFontRegistry::reloadForDpiChange).
    resetBitmapCache();
    loadFace(mName, mPointSize, vert_dpi, horz_dpi, mWeight, mIsFallback, 0, mHinting, mFontFlags);
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
    // Drop the head's non-owning resolution caches. Do NOT call into
    // mFace->resetBitmapCache(): that would delete glyph entries that
    // sibling heads sharing this LLFontFace still reference in their
    // own local resolution caches, leaving them with dangling pointers
    // and reading freed memory on the next render. Atlas + face-owned
    // entry lifetime is tied to LLFontFace's refcount — for DPI/scale
    // changes that drive resetSelf, loadFace below picks up a new face
    // wrapper (different LLFontFaceKey) and the old wrapper drops to
    // refcount 0 (and naturally tears down its atlas) once all heads
    // have reloaded. For same-key resets the face cache is reused and
    // any previously-rasterized glyphs remain valid.
    mCharGlyphInfoMap.clear();
    mShapedGlyphInfoMap.clear();

    if (!mIsFallback && mFace && !mFace->findGlyphInfo(0, EFontGlyphType::Grayscale))
    {
        // Pre-warm notdef only if the face cache doesn't already have it.
        // Fresh face → insert; reused face → already-pre-warmed by an
        // earlier head, skip.
        addGlyphFromFont(this, 0, 0, EFontGlyphType::Grayscale);
    }
}

void LLFontFreetype::destroyGL()
{
    // Clear our own non-owning resolution caches before the face deletes
    // the LLFontGlyphInfo entries they reference. mFace->destroyGL fully
    // resets the face (clears its owning glyph maps + atlas vectors), so
    // any pointers left here would dangle until the next resetSelf().
    // No render runs between here and the resetSelf() that follows — every
    // call site of destroyGL is immediately followed by either initClass
    // (initFonts path) or process exit (stopGL path) — but clearing now
    // keeps the invariant local rather than relying on caller ordering.
    mCharGlyphInfoMap.clear();
    mShapedGlyphInfoMap.clear();
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

            // Order matters: drop the head's non-owning pointers FIRST, while
            // the face entries they reference are still alive and the
            // predicate can read them safely. Then have the face delete the
            // matching owned entries. Doing it the other way around would
            // call the predicate on dangling pointers (UB) and could leave
            // the head's map with pointers to freed memory.
            auto matches = [&](const LLFontGlyphInfo* gi) { return glyph_uses_sheet(gi, type, num); };
            for (auto it = mCharGlyphInfoMap.begin(); it != mCharGlyphInfoMap.end(); )
                it = matches(it->second) ? mCharGlyphInfoMap.erase(it) : std::next(it);
            for (auto it = mShapedGlyphInfoMap.begin(); it != mShapedGlyphInfoMap.end(); )
                it = matches(it->second) ? mShapedGlyphInfoMap.erase(it) : std::next(it);
            if (mFace)
            {
                mFace->erase_codepoint_entries(matches);
                mFace->erase_shaped_entries(matches);
            }

            getBitmapCache()->releaseSheet(type, num);
        }
    }
}

U8 LLFontFreetype::getStyle() const
{
    return mStyle;
}

// (setSubImageBGRA / setSubImageGrayscale moved to LLFontFace —
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
            }
            std::string mName;
            std::string mAddress;
            std::size_t mSize;
        };
    }
}

U8 const* LLFontManager::loadFont( std::string const &aFilename, long &a_Size)
{
    a_Size = 0;
    std::map< std::string, std::shared_ptr<ll::fonts::LoadedFont> >::iterator itr = m_LoadedFonts.find( aFilename );
    if( itr != m_LoadedFonts.end() )
    {
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

void LLFontManager::collectGarbage()
{
    // Sweep mFaceCache: every live LLFontFreetype holds an
    // LLPointer<LLFontFace> mFace, so the map's own LLPointer is the sole
    // ref iff getNumRefs() == 1. Same-order rule as unloadAllFonts —
    // ~LLFontFace runs FT_Done_Face, which dereferences the FT_OPEN_MEMORY
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
    // LLFontFace entries via FT_OPEN_MEMORY (see LLFontFace::load).
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
