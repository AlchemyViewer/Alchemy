/**
 * @file alfontface.h
 * @brief Refcounted wrapper around an FT_Face, sharable across LLFontFreetype
 *        instances that request the same (file, sized + variable axis) state.
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

#pragma once

#include "llpointer.h"
#include "llrefcount.h"
#include "stdtypes.h"
#include "llfontbitmapcache.h"  // for LLFontBitmapCache + EFontGlyphType

#include <boost/functional/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered_map.hpp>

#include <string>

// Forward declarations — match the trick used in llfontfreetype.h to avoid
// pulling in <ft2build.h> + FT_FREETYPE_H from this header.
struct FT_FaceRec_;
typedef struct FT_FaceRec_* ALFT_Face;
struct hb_font_t;
struct LLFontGlyphInfo;
class  LLFontFreetype;
// Defined in llfontregistry.h; forward-declared here to keep this header
// independent of the registry. Translation units that need the values
// (e.g. alfontface.cpp) include llfontregistry.h directly.
enum class EFontHinting : S32;

// Optional values for the OpenType variation axes the registry exposes.
// Each axis carries a "set" flag (so 0.f isn't ambiguous with "use file
// default"). All axes are silently skipped at face-load time when the
// font doesn't expose them — setVariationAxis returns false on a face
// whose FT_MM_Var lacks the requested tag.
//   wght: 1   — 1000 (CSS weight scale; 400 regular, 700 bold)
//   opsz: 6   — 144  (point-size design adaptation; defaults to render pt)
//   ital: 0   — 1    (0 upright, 1 italic)
//   wdth: 50  — 200  (% of normal width; 100 = unchanged)
//   slnt: -90 — 0    (degrees; 0 upright, negative slants forward)
struct ALFontVarAxes
{
    F32  wght     = 0.f;
    F32  opsz     = 0.f;
    F32  ital     = 0.f;
    F32  wdth     = 0.f;
    F32  slnt     = 0.f;
    bool wght_set = false;
    bool opsz_set = false;
    bool ital_set = false;
    bool wdth_set = false;
    bool slnt_set = false;

    bool operator==(const ALFontVarAxes& o) const noexcept
    {
        return wght_set == o.wght_set && opsz_set == o.opsz_set
            && ital_set == o.ital_set && wdth_set == o.wdth_set && slnt_set == o.slnt_set
            && (!wght_set || wght == o.wght)
            && (!opsz_set || opsz == o.opsz)
            && (!ital_set || ital == o.ital)
            && (!wdth_set || wdth == o.wdth)
            && (!slnt_set || slnt == o.slnt);
    }
};

// Identity key for an FT_Face. Two LLFontFreetype instances that resolve to
// the same key share the underlying face (via LLFontManager's cache). The
// key fields are exactly the inputs to FT_Open_Face / FT_Set_Char_Size /
// FT_Set_Var_Design_Coordinates that determine the face's observable state.
struct ALFontFaceKey
{
    std::string   filename;
    S32           face_index;
    F32           point_size;
    F32           vert_dpi;
    F32           horz_dpi;
    EFontHinting  hinting;
    S32           flags;
    ALFontVarAxes var_axes;     // wght/opsz/ital/wdth/slnt (each independently optional)

    bool operator==(const ALFontFaceKey& o) const noexcept
    {
        return filename == o.filename
            && face_index == o.face_index
            && point_size == o.point_size
            && vert_dpi == o.vert_dpi
            && horz_dpi == o.horz_dpi
            && hinting == o.hinting
            && flags == o.flags
            && var_axes == o.var_axes;
    }

    friend std::size_t hash_value(const ALFontFaceKey& k) noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, k.filename);
        boost::hash_combine(seed, k.face_index);
        boost::hash_combine(seed, k.point_size);
        boost::hash_combine(seed, k.vert_dpi);
        boost::hash_combine(seed, k.horz_dpi);
        boost::hash_combine(seed, static_cast<S32>(k.hinting));
        boost::hash_combine(seed, k.flags);
        // Axis values participate in the hash only when set; an unset
        // axis must not perturb the bucket from a key with no axes set
        // (so the common no-axes-configured path keeps a stable hash).
        boost::hash_combine(seed, k.var_axes.wght_set);
        boost::hash_combine(seed, k.var_axes.opsz_set);
        boost::hash_combine(seed, k.var_axes.ital_set);
        boost::hash_combine(seed, k.var_axes.wdth_set);
        boost::hash_combine(seed, k.var_axes.slnt_set);
        if (k.var_axes.wght_set) boost::hash_combine(seed, k.var_axes.wght);
        if (k.var_axes.opsz_set) boost::hash_combine(seed, k.var_axes.opsz);
        if (k.var_axes.ital_set) boost::hash_combine(seed, k.var_axes.ital);
        if (k.var_axes.wdth_set) boost::hash_combine(seed, k.var_axes.wdth);
        if (k.var_axes.slnt_set) boost::hash_combine(seed, k.var_axes.slnt);
        return seed;
    }
};

class ALFontFace : public LLRefCount
{
public:
    ALFontFace();
    ~ALFontFace();

    // Open the underlying FT_Face, set its size/DPI, and apply variable-axis
    // values if applicable. Returns false if FreeType can't open the file or
    // can't set the requested size; in that case the ALFontFace is left in a
    // dead-but-harmless state (isValid() returns false).
    bool load(const std::string& filename, S32 face_index,
              F32 point_size, F32 vert_dpi, F32 horz_dpi,
              EFontHinting hinting, S32 flags,
              const ALFontVarAxes& var_axes = {});

    ALFT_Face face() const { return mFTFace; }
    bool      isValid() const { return mFTFace != nullptr; }
    EFontHinting hinting() const { return mHinting; }

    // Codepoint -> FT glyph index, cached. Equivalent to FT_Get_Char_Index
    // but skips the cmap binary search on repeated lookups. A cached value
    // of 0 is meaningful — it means the face has no glyph for `wch` — and
    // prevents re-querying on every miss.
    U32 getCharGlyphIndex(llwchar wch) const;

    // HarfBuzz handle wrapping mFTFace. Lazily created on first call. Tied
    // to this wrapper's lifetime; destroyed in ~ALFontFace.
    hb_font_t* getHbFont() const;

    // Pure functions of (file, hinting). Stored at load time so callers
    // don't re-deref FT_Face flags on every check.
    bool useSubpixelPen() const { return mUseSubpixelPen; }
    bool hasColor() const       { return mHasColor; }
    bool hasSvg() const         { return mHasSvg; }
    // True iff the face carries a COLR table whose version >= 1. FT_HAS_COLOR
    // is true for any color table (sbix / CBDT / COLRv0 / COLRv1 / SVG); only
    // COLRv1 needs the hb-raster paint walker — FT itself rasterizes the
    // others via FT_LOAD_COLOR. Used by renderGlyph to decide whether to
    // route a Color request through the COLRv1 painter.
    bool hasColrV1() const      { return mHasColrV1; }
    // CPAL palette index passed to hb_font_paint_glyph for COLRv1 rasterization.
    // Computed at load time: defaults to 0 unless LLFontGL::sUseDarkEmojiPalette
    // is set AND the face's CPAL has a palette flagged for dark backgrounds —
    // in which case we use the first matching palette.
    U32  paletteIndex() const   { return mPaletteIndex; }
    bool isFixedWidth() const   { return mIsFixedWidth; }
    // True iff load() successfully applied a "wght" variation axis value.
    // Used by LLFontFreetype's BOLD-style synthesis to decide whether a
    // weight>=600 request already produced a heavy face on its own (in
    // which case programmatic bolding should be suppressed).
    bool wghtAxisSet() const    { return mWghtAxisSet; }
    // True iff load() successfully applied the matching variation axis.
    // opszAxisSet exposes whether the face carries an opsz axis at all
    // (load() always tries opsz — explicitly when var_axes.opsz_set,
    // otherwise from the rendered point_size).
    // italAxisSet feeds the equivalent ITALIC-style synthesis check
    // (skip programmatic italic when a real ital >= 0.5 is in effect).
    // wdth/slnt have no synthesis interaction today; expose them anyway
    // so downstream code can probe whether the file actually carries
    // the axis (vs. silently skipped because the font lacks it).
    bool opszAxisSet() const    { return mOpszAxisSet; }
    bool italAxisSet() const    { return mItalAxisSet; }
    bool wdthAxisSet() const    { return mWdthAxisSet; }
    bool slntAxisSet() const    { return mSlntAxisSet; }

    // Per-face bitmap atlas for glyphs rasterized through this face. Shared
    // across every LLFontFreetype that wraps this face — heads sharing a
    // primary face share the atlas; the same Twemoji face used as a fallback
    // by N heads writes its emoji glyphs into ONE atlas instead of N.
    LLFontBitmapCache* getBitmapCache() const { return mFontBitmapCachep; }

    // Per-face glyph cache, keyed on FT glyph index. Used by both the
    // codepoint path (which resolves wch -> glyph_index before the lookup)
    // and the shaped path (which gets glyph_index directly from HarfBuzz).
    // One slot per (glyph_index, type), so the same glyph rasterized via
    // either path lives in one atlas slot. LLFontGlyphInfo entries are
    // owned here and deleted in ~ALFontFace.
    LLFontGlyphInfo* findGlyphInfo(U32 glyph_index, EFontGlyphType type) const;
    // Publish `gi` (ownership transfers to the cache) and return the
    // published entry. If a (glyph_index, type) entry already exists, the
    // EXISTING one is kept and returned and `gi` is deleted — published
    // pointers may be held up the stack (render loop, kerning prefetch),
    // so replacing in place would free memory in active use. Callers must
    // continue with the return value, never with `gi`. A duplicate publish
    // means an upstream dedup probe was skipped (addShapedGlyphFromFont
    // checks findGlyphInfo first) and asserts in debug; the duplicate's
    // atlas slots are orphaned, not reclaimed.
    LLFontGlyphInfo* insertGlyphInfo(U32 glyph_index, LLFontGlyphInfo* gi) const;

    // Iterate and conditionally erase entries. Used by collectGarbage to
    // purge entries that referenced an evicted atlas sheet.
    template<typename Pred>
    void erase_glyph_entries(Pred should_erase) const;

    // Release atlas sheets that haven't been read or written within the
    // idle threshold, dropping the glyph entries that pointed into them
    // first. Self-throttled — repeat calls inside the GC interval are
    // cheap no-ops. Lives on the face because the atlas and glyph map do:
    // N LLFontFreetype heads sharing this face cost one sweep per
    // interval, not N (the throttle used to sit per-head, so siblings
    // re-swept the same shared atlas). NOT safe to call mid-render while
    // a glyph pointer is held: call only at frame boundaries / before any
    // glyph lookups (LLFontGL::sweepGlyphCaches does).
    void collectGarbage() const;

    // Drop all rasterized glyphs and reset the atlas. Used by the registry
    // when DPI changes and the wrapper survives but its atlas state needs
    // to be rebuilt.
    void resetBitmapCache();

    // Free GL textures while keeping the wrapper alive (used at shutdown
    // when teardown order requires GL state release before destruction).
    void destroyGL();

    // Copy a rasterized bitmap into this face's atlas at slot
    // (type=Grayscale, bitmap_num) at offset (x, y). Returns false on
    // missing atlas / image data; non-fatal so callers can recover.
    void setSubImageLuminanceAlpha(U32 x, U32 y, U32 bitmap_num,
                                   U32 width, U32 height,
                                   U8* data, S32 stride = 0) const;
    // Same for the Color (BGRA) atlas. `stride` is FT's signed bitmap.pitch:
    // negative when the source buffer is bottom-up (data points to the row
    // at the highest address; rows step backward through memory).
    bool setSubImageBGRA(U32 x, U32 y, U32 bitmap_num,
                         U16 width, U16 height,
                         const U8* data, S32 stride) const;

private:
    ALFontFace(const ALFontFace&) = delete;
    ALFontFace& operator=(const ALFontFace&) = delete;

    // Apply a single OpenType variation axis value, if the face has one.
    bool setVariationAxis(const std::string& axis_tag, F32 value);

    // Out-of-line delete of an LLFontGlyphInfo*. Defined in alfontface.cpp,
    // where llfontfreetype.h provides the complete type — lets the inline
    // template below stay in the header without triggering
    // -Wdelete-incomplete (a hard error under C++26).
    static void destroyGlyphInfo(LLFontGlyphInfo* gi);

    typedef boost::unordered_multimap<U32, LLFontGlyphInfo*> glyph_info_map_t;

    ALFT_Face          mFTFace = nullptr;
    // Single source for both HB load flags (set once in getHbFont via
    // hb_ft_font_set_load_flags) and FT load flags (read by
    // LLFontFreetype::renderGlyph as (FT_Int32)mHinting). Set in load() and
    // never rewritten — divergence between the two paths would break advance
    // consistency between shaped and codepoint runs. The casts to int /
    // FT_Int32 rely on EFontHinting's bit pattern matching FT_LOAD_* (see
    // llfontregistry.h:48-56).
    EFontHinting       mHinting;
    mutable hb_font_t* mHbFont = nullptr;
    mutable boost::unordered_flat_map<llwchar, U32> mCharIndexCache;

    // Snapshot of the FT face's pixel-per-em at the moment FT_Set_Char_Size
    // ran in load(). The lazily-created hb_font_t (getHbFont) snapshots
    // size->metrics at creation and uses ppem/scale from it; if anything
    // resizes the face after load() without calling hb_ft_font_changed,
    // these stay at the load-time values and the assert in getHbFont fires.
    U16                mLoadedXPpem = 0;
    U16                mLoadedYPpem = 0;

    LLFontBitmapCache* mFontBitmapCachep = nullptr;
    mutable glyph_info_map_t mGlyphInfoMap;

    // Earliest wall-clock time (seconds) at which collectGarbage() should
    // do real work. Throttle gate so the per-frame sweep is essentially
    // free between intervals. Per-face (not per-head) so siblings sharing
    // this face share one cadence.
    mutable F64 mNextGcTime = 0.0;

    bool mUseSubpixelPen = false;
    bool mHasColor       = false;
    bool mHasSvg         = false;
    bool mHasColrV1      = false;
    bool mIsFixedWidth   = false;
    bool mWghtAxisSet    = false;
    bool mOpszAxisSet    = false;
    bool mItalAxisSet    = false;
    bool mWdthAxisSet    = false;
    bool mSlntAxisSet    = false;
    U32  mPaletteIndex   = 0;
};

// Inline template definitions — kept in the header so callers in
// llfontfreetype.cpp can instantiate with their own predicates.
template<typename Pred>
void ALFontFace::erase_glyph_entries(Pred should_erase) const
{
    for (auto it = mGlyphInfoMap.begin(); it != mGlyphInfoMap.end(); )
    {
        if (should_erase(it->second))
        {
            destroyGlyphInfo(it->second);
            it = mGlyphInfoMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
