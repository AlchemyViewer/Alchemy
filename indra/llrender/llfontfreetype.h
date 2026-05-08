/**
 * @file llfontfreetype.h
 * @brief Font library wrapper
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

#ifndef LL_LLFONTFREETYPE_H
#define LL_LLFONTFREETYPE_H

#include "llpointer.h"
#include "llstl.h"

#include "llimagegl.h"
#include "llfontbitmapcache.h"
#include "llfontface.h"

#include <array>
#include <boost/functional/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered_map.hpp>

// LLFT_Face / hb_font_t / EFontHinting come in via llfontface.h.
struct FT_StreamRec_;
typedef struct FT_StreamRec_ LLFT_Stream;

namespace ll
{
    namespace fonts
    {
        class LoadedFont;
    }
}

class LLFontManager
{
public:
    static void initClass();
    static void cleanupClass();

    U8 const *loadFont( std::string const &aFilename, long &a_Size );

    // Resolve a key to a refcounted, shared LLFontFace. Loads the face on
    // miss and caches it; returns null if FreeType refuses the file or the
    // requested size. Cached entries persist until cleanupClass(), or until
    // unloadAllFonts() runs at shutdown.
    LLPointer<LLFontFace> getOrCreateFace(const LLFontFaceKey& key);

    // Drop face-cache and loaded-font entries no longer referenced by any
    // live LLFontFreetype. Intended to run after LLFontRegistry::reload()
    // has dropped its old fallback references (i.e. from the existing-
    // registry branch of LLFontGL::initClass once reload() has returned).
    void collectGarbage();

private:
    LLFontManager();
    ~LLFontManager();

    void unloadAllFonts();
    std::map< std::string, std::shared_ptr<ll::fonts::LoadedFont> > m_LoadedFonts;
    boost::unordered_map<LLFontFaceKey, LLPointer<LLFontFace>> mFaceCache;
};

struct LLFontGlyphInfo
{
    // Number of horizontal subpixel phases cached per glyph for fonts that use
    // subpixel pen position. Each phase stores a separately-rasterized bitmap
    // at a (k / kNumPhases) px x-offset, so the renderer can pick the phase
    // matching the fractional part of the pen position and avoid the visible
    // spacing inconsistency that round-half-up snapping produces on runs of
    // identical glyphs (password dots, table rules, ASCII art).
    //
    // 8 phases give 1/8 px resolution; residual quantization error is at most
    // 1/16 px (~0.06 px) — below the visual threshold for stem perception.
    static constexpr U8 kNumPhases = 8;

    // Per-phase atlas slot. Each phase is a separate atlas allocation; only
    // the offsets and per-bitmap dimensions vary across phases, glyph-level
    // metrics (advance, lsb/rsb deltas) are shared.
    struct PhaseSlot
    {
        S32 mXBitmapOffset = 0;  // x in atlas
        S32 mYBitmapOffset = 0;  // y in atlas
        S32 mWidth = 0;          // bitmap width for this phase
        S32 mHeight = 0;         // bitmap height for this phase
        S32 mXBearing = 0;       // bitmap_left (origin offset relative to pen)
        S32 mYBearing = 0;       // bitmap_top
        std::pair<EFontGlyphType, S32> mBitmapEntry =
            std::make_pair(EFontGlyphType::Unspecified, -1);
    };

    LLFontGlyphInfo(U32 index, EFontGlyphType glyph_type);
    LLFontGlyphInfo(const LLFontGlyphInfo& fgi);

    U32 mGlyphIndex;
    EFontGlyphType mGlyphType;

    // Atlas owner. mPhaseSlots[*].mBitmapEntry locates the slot within
    // mSourceFace->getBitmapCache(); the renderer follows this indirection
    // so two heads sharing a face render glyphs from the same atlas pages.
    const LLFontFace* mSourceFace = nullptr;

    // Glyph-level metrics. These are taken from phase 0 and used by the
    // measurement paths (getWidthF32, maxDrawableChars, etc.) that don't
    // need phase-specific accuracy. The renderer uses per-phase dimensions
    // from mPhaseSlots[phase] instead.
    S32 mWidth;         // In pixels
    S32 mHeight;        // In pixels
    F32 mXAdvance;      // In pixels
    F32 mYAdvance;      // In pixels
    S32 mXBearing;      // Distance from baseline to left in pixels
    S32 mYBearing;      // Distance from baseline to top in pixels
    S32 mLsbDelta;      // FreeType subpixel left side bearing delta (26.6 units)
    S32 mRsbDelta;      // FreeType subpixel right side bearing delta (26.6 units)

    // Per-phase atlas slots. mPhaseCount == 1 for native-hinted (HINTING_DEFAULT)
    // faces — single integer-pen phase, slots 1..7 are unused. Subpixel-pen
    // faces (FORCE_AUTOHINT, LIGHT, NO_HINTING) populate all kNumPhases slots.
    std::array<PhaseSlot, kNumPhases> mPhaseSlots;
    U8 mPhaseCount = 1;
};

extern LLFontManager *gFontManagerp;

class LLFontFreetype : public LLRefCount
{
public:
    LLFontFreetype();
    ~LLFontFreetype();

    // is_fallback should be true for fallback fonts that aren't used
    // to render directly (Unicode backup, primarily)
    bool loadFace(const std::string& filename, F32 point_size, F32 vert_dpi, F32 horz_dpi, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags, const LLFontVarAxes& var_axes = {});

    S32 getNumFaces(const std::string& filename);

    typedef std::function<bool(llwchar)> char_functor_t;
    void addFallbackFont(const LLPointer<LLFontFreetype>& fallback_font, const char_functor_t& functor = nullptr);
    typedef std::pair<LLPointer<LLFontFreetype>, char_functor_t> fallback_font_t;
    typedef std::vector<fallback_font_t> fallback_font_vector_t;
    const fallback_font_vector_t& getFallbackFonts() const { return mFallbackFonts; }

    // Global font metrics - in units of pixels
    F32 getLineHeight() const;
    F32 getAscenderHeight() const;
    F32 getDescenderHeight() const;

    // Underline stroke metrics, in pixels at this face's render size.
    // Position is the distance from the baseline to the top of the
    // underline (negative = below baseline, the typical case). Thickness
    // is the underline's height; floored at 1 px so small point sizes
    // still render a visible stroke. Falls back to descender depth and
    // 1 px thickness when the face has no underline metadata.
    F32 getUnderlinePosition() const;
    F32 getUnderlineThickness() const;


// For a lowercase "g":
//
//  ------------------------------
//                       ^     ^
//                       |     |
//              xxx x    |Ascender
//             x   x     v     |
//  ---------   xxxx-------------- Baseline
//  ^              x           |
//  | Descender    x           |
//  v           xxxx           |LineHeight
//  -----------------------    |
//                             v
//  ------------------------------

    enum
    {
        FIRST_CHAR = 32,
        NUM_CHARS = 127 - 32,
        LAST_CHAR_BASIC = 127,

        // Need full 8-bit ascii range for spanish
        NUM_CHARS_FULL = 255 - 32,
        LAST_CHAR_FULL = 255
    };

    F32 getXAdvance(llwchar wc) const;
    F32 getXAdvance(const LLFontGlyphInfo* glyph) const;
    F32 getXKerning(llwchar char_left, llwchar char_right) const; // Get the kerning between the two characters
    F32 getXKerning(const LLFontGlyphInfo* left_glyph_info, const LLFontGlyphInfo* right_glyph_info) const; // Get the kerning between the two characters

    LLFontGlyphInfo* getGlyphInfo(llwchar wch, EFontGlyphType glyph_type) const;

    // Look up or create a glyph via its FreeType glyph index rather than its
    // source codepoint. Used by the shaping pipeline, where one codepoint may
    // produce several glyphs (e.g. Indic clusters) or several codepoints may
    // collapse into one glyph (e.g. ZWJ emoji families). `fontp` is the face
    // that owns the glyph — usually a fallback face returned by the emoji
    // selector — and `glyph_index` is the index HarfBuzz produced.
    LLFontGlyphInfo* getGlyphInfoByIndex(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType glyph_type) const;

    // Lazily create and return a HarfBuzz font wrapping this face, keyed to
    // the current FT size. The handle is owned by the LLFontFreetype and
    // reused across shape() calls to avoid per-call setup cost.
    hb_font_t* getHbFont() const;

    // True if the underlying FT face has FT_FACE_FLAG_FIXED_WIDTH set —
    // i.e. the font is monospace. Shaping width-modifying features (kern,
    // liga, calt, etc.) on a monospace face violates column alignment, so
    // shape_sub_run disables those features for fixed-width faces.
    bool isFixedWidth() const;

    // Per-face opt-in: keep liga/clig/dlig/calt enabled on a fixed-width
    // face. For programmer fonts (Fira Code, JetBrains Mono, etc.) where
    // ligatures are width-preserving by design. kern stays disabled either
    // way — monospace + GPOS pair-kerning is fundamentally incompatible.
    // Set via <font ligatures="on"> in fonts.xml. No effect on non-fixed-
    // width faces (their full feature set runs unconditionally).
    bool getAllowMonospaceLigatures() const { return mAllowMonospaceLigatures; }
    void setAllowMonospaceLigatures(bool allow) { mAllowMonospaceLigatures = allow; }

    // True when the pen position should accumulate fractionally through a
    // glyph run, with each glyph's destination rect snapped at draw time.
    // False (HINTING_DEFAULT) keeps the legacy per-glyph round so native-
    // hinted glyphs designed for the integer pixel grid stay crisp; true
    // (FORCE_AUTOHINT, NO_HINTING) preserves subpixel kerning precision
    // from HarfBuzz's GPOS x_advance values that would otherwise be
    // crushed by per-glyph ll_round in the hot paths.
    bool useSubpixelPen() const { return mUseSubpixelPen; }

    // Pick the face in this font's fallback chain that owns a glyph for
    // `base` and return it, writing the FT glyph index into out_glyph_index.
    // Walks the same priority as the codepoint path in getGlyphInfo:
    // emoji-functor fallbacks first, then monochrome fallbacks, then emoji
    // fallbacks ignoring the functor, then `this` as a last resort. Returns
    // `this` with out_glyph_index=0 when no face has a glyph for `base`.
    const LLFontFreetype* selectShapingFace(llwchar base, U32& out_glyph_index) const;

    // True if this face's FreeType charmap directly contains a glyph for
    // `wch`. Consults the underlying charmap (via getCharGlyphIndex) and
    // works on fallback faces. Used by shape itemization to decide
    // whether a face can absorb a given codepoint without producing
    // notdef.
    bool faceHasGlyph(llwchar wch) const;

    void reset(F32 vert_dpi, F32 horz_dpi);
    // reset() body without the fallback-chain cascade. Used by
    // LLFontRegistry to reset shared fallback instances exactly once.
    void resetSelf(F32 vert_dpi, F32 horz_dpi);

    void destroyGL();

    const std::string& getName() const;

    void       dumpFontBitmaps() const;
    const LLFontBitmapCache* getFontBitmapCache() const;
    // Convenience non-const accessor — atlas now lives on the face wrapper.
    LLFontBitmapCache* getBitmapCache() const { return mFace ? mFace->getBitmapCache() : nullptr; }

    U8 getStyle() const;

    // Run a maintenance pass that releases bitmap atlas sheets which haven't
    // been read or written within the idle threshold, recovering their CPU
    // and GPU memory and dropping any LLFontGlyphInfo entries that pointed
    // into them. Self-throttled — repeat calls inside the GC interval are
    // cheap no-ops, so it's fine for every render() invocation to call this
    // unconditionally. NOT safe to call mid-render while a glyph pointer is
    // held: call only at frame boundaries / before any glyph lookups.
    void collectGarbage() const;

    // Return the FT glyph index for `wch` on this face, caching the result so
    // subsequent lookups skip the cmap binary search inside FT_Get_Char_Index.
    // Delegates to mFace's char-index cache, which is shared across every
    // LLFontFreetype that resolved to the same LLFontFace. Public so the
    // shared fallback-chain walker (free helper at file scope of the cpp)
    // can probe each fallback without needing friendship.
    U32 getCharGlyphIndex(llwchar wch) const;

    // Tests-only accessor for the shared face wrapper. Lets the consistency
    // tests reach LLFontFace's hb_font_t, FT_Face, hinting, and ppem snapshot
    // without friend declarations. No production caller — render and
    // measurement paths use mFace internally.
    const LLFontFace* getFontFace() const { return mFace.get(); }

private:
    // Convenience: dereference the shared face wrapper. Null when this
    // instance hasn't been loaded yet or was unloaded.
    LLFT_Face getFTFace() const { return mFace ? mFace->face() : nullptr; }

    void resetBitmapCache();
    // Render and cache a glyph for `glyph_index` on `fontp` (the source face,
    // which can be `this` head or a fallback). Inserts into the source face's
    // and the head's resolution caches keyed on (face, glyph_index). Used by
    // both the codepoint path (after wch->glyph_index resolution + chain
    // walk) and the shaped path (HB output already names the source face).
    LLFontGlyphInfo* addShapedGlyphFromFont(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType bitmap_type) const;
    // Runs the FreeType rasterizer, allocates an LLFontGlyphInfo and populates
    // the bitmap atlas. `out_bitmap_glyph_type` receives the pixel format
    // FreeType actually delivered (which can differ from the requested one —
    // e.g. color requested but mono returned).
    LLFontGlyphInfo* renderAndCreateGlyph(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType requested_glyph_type, EFontGlyphType& out_bitmap_glyph_type) const;
    void renderGlyph(EFontGlyphType bitmap_type, U32 glyph_index, llwchar wch) const;
    // COLRv1 paint-walker entry point. Loads the glyph in outline mode to
    // populate metrics, then runs LLFontColrV1Painter to rasterize the paint
    // tree into a staging buffer; on success, patches the FT glyph slot
    // (bitmap pointer + pixel_mode + bitmap_left/top) so the existing
    // pixel_mode switch in renderAndCreateGlyph consumes it unchanged.
    // `requested` selects between BGRA Color output (FT_PIXEL_MODE_BGRA,
    // routed to the Color atlas) and luminance-shaded Gray output
    // (FT_PIXEL_MODE_GRAY, routed to the Grayscale atlas where it tints
    // with text_color at draw time). Returns false if the glyph has no
    // paint tree, the walker hit an unsupported feature, or an allocation
    // failed — caller falls through to the normal FT path.
    bool renderColrV1Glyph(U32 glyph_index, EFontGlyphType requested) const;
    void insertGlyphInfo(const LLFontFreetype* fontp, U32 glyph_index, LLFontGlyphInfo* gi) const;

    std::string mName;

    U8 mStyle;

    F32 mPointSize;
    F32 mAscender;
    F32 mDescender;
    F32 mLineHeight;

    // Shared underlying FT_Face wrapper. Multiple LLFontFreetype instances
    // can hold pointers to the same LLFontFace when their (filename, size,
    // weight, hinting, flags) all match — see LLFontManager::getOrCreateFace.
    // Per-instance state (fallback chain, atlas, glyph caches) stays here.
    LLPointer<LLFontFace> mFace;

    bool mIsFallback;
    EFontHinting mHinting;
    S32 mFontFlags;
    // OpenType variation axes the head was loaded with. Stored on the
    // head (not just on mFace) so reset() can round-trip the same axes
    // back through loadFace when re-resolving for a DPI change.
    LLFontVarAxes mVarAxes;
    bool mAllowMonospaceLigatures = false;
    bool mUseSubpixelPen = false;
    fallback_font_vector_t mFallbackFonts; // A list of fallback fonts to look for glyphs in (for Unicode chars)

    // Per-head shaping-face resolution cache: codepoint -> (winning face,
    // glyph index in that face). Replaces an O(fallbacks)-deep walk for
    // each codepoint.
    //
    // Invalidation: cleared on loadFace() reload (which happens when the
    // freetype is rebuilt for a DPI change or fonts.xml override change)
    // and on addFallbackFont() (new fallback may win for codepoints that
    // previously resolved to a later face). NOT cleared when a fallback
    // freetype is rebuilt elsewhere — the registry's pointer-stable swap
    // preserves LLFontFreetype identity across reloads, so the cached
    // pointers stay valid. If that invariant ever changes (e.g. a code
    // path that destroys + recreates a fallback freetype rather than
    // resetting it in place), this map needs clearing too.
    //
    // Bounded by SHAPING_RESOLUTION_LIMIT entries; selectShapingFace
    // clears the map when the threshold is hit so a long session of
    // CJK-heavy chat doesn't grow this without limit.
    mutable boost::unordered_flat_map<llwchar, std::pair<const LLFontFreetype*, U32>> mShapingFaceResolution;

    // Per-head fast lookup: (source face, glyph index) -> non-owning pointer
    // into the source face's glyph map. Both the codepoint path (after wch
    // resolution + chain walk) and the shaped path (HB output) feed into this
    // single cache, so a glyph rendered by either route lives in one atlas
    // slot per (face, glyph_index, type).
    struct GlyphKey
    {
        const LLFontFreetype* face;
        U32                   glyph_index;
        bool operator==(const GlyphKey& o) const noexcept
        {
            return face == o.face && glyph_index == o.glyph_index;
        }
    };
    struct GlyphKeyHash
    {
        size_t operator()(const GlyphKey& k) const noexcept
        {
            size_t h = boost::hash<const void*>{}(k.face);
            boost::hash_combine(h, k.glyph_index);
            return h;
        }
    };
    typedef boost::unordered_multimap<GlyphKey, LLFontGlyphInfo*, GlyphKeyHash> glyph_info_map_t;
    mutable glyph_info_map_t mGlyphInfoMap;

    // (LLFontBitmapCache moved to LLFontFace — atlas storage is now shared
    // across every LLFontFreetype that wraps the same face. mCharIndexCache
    // and mHbFont also live on LLFontFace for the same reason.)

    mutable S32 mRenderGlyphCount;

    // Earliest wall-clock time (seconds) at which collectGarbage() should
    // do real work. Throttle gate so the per-render call from LLFontGL::render
    // is essentially free between sweeps.
    mutable F64 mNextGcTime = 0.0;
};

#endif // LL_FONTFREETYPE_H
