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

#include <boost/functional/hash.hpp>
#include <boost/unordered_map.hpp>

// Hack.  FT_Face is just a typedef for a pointer to a struct,
// but there's no simple forward declarations file for FreeType,
// and the main include file is 200K.
// We'll forward declare the struct here.  JC
struct FT_FaceRec_;
typedef struct FT_FaceRec_* LLFT_Face;
struct FT_StreamRec_;
typedef struct FT_StreamRec_ LLFT_Stream;
enum class EFontHinting : S32;
// Forward-declare HarfBuzz's opaque font handle so consumers of this header
// (including the viewer's PCH) don't have to drag in <hb.h>.
struct hb_font_t;

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

private:
    LLFontManager();
    ~LLFontManager();

    void unloadAllFonts();
    std::map< std::string, std::shared_ptr<ll::fonts::LoadedFont> > m_LoadedFonts;
};

struct LLFontGlyphInfo
{
    LLFontGlyphInfo(U32 index, EFontGlyphType glyph_type);
    LLFontGlyphInfo(const LLFontGlyphInfo& fgi);

    U32 mGlyphIndex;
    EFontGlyphType mGlyphType;

    // Metrics
    S32 mWidth;         // In pixels
    S32 mHeight;        // In pixels
    F32 mXAdvance;      // In pixels
    F32 mYAdvance;      // In pixels

    // Information for actually rendering
    S32 mXBitmapOffset; // Offset to the origin in the bitmap
    S32 mYBitmapOffset; // Offset to the origin in the bitmap
    S32 mXBearing;  // Distance from baseline to left in pixels
    S32 mYBearing;  // Distance from baseline to top in pixels
    S32 mLsbDelta;  // FreeType subpixel left side bearing delta (26.6 units)
    S32 mRsbDelta;  // FreeType subpixel right side bearing delta (26.6 units)
    std::pair<EFontGlyphType, S32> mBitmapEntry; // Which bitmap in the bitmap cache contains this glyph
};

extern LLFontManager *gFontManagerp;

class LLFontFreetype : public LLRefCount
{
public:
    LLFontFreetype();
    ~LLFontFreetype();

    // is_fallback should be true for fallback fonts that aren't used
    // to render directly (Unicode backup, primarily)
    bool loadFace(const std::string& filename, F32 point_size, F32 vert_dpi, F32 horz_dpi, S32 weight, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags);

    S32 getNumFaces(const std::string& filename);

    typedef std::function<bool(llwchar)> char_functor_t;
    void addFallbackFont(const LLPointer<LLFontFreetype>& fallback_font, const char_functor_t& functor = nullptr);

    // Global font metrics - in units of pixels
    F32 getLineHeight() const;
    F32 getAscenderHeight() const;
    F32 getDescenderHeight() const;


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
    // Walks the same priority as addGlyph: emoji-functor fallbacks first,
    // then monochrome fallbacks, then emoji fallbacks ignoring the functor,
    // then `this` as a last resort. Returns `this` with out_glyph_index=0
    // when no face has a glyph for `base`.
    const LLFontFreetype* selectShapingFace(llwchar base, U32& out_glyph_index) const;

    void reset(F32 vert_dpi, F32 horz_dpi);

    void destroyGL();

    const std::string& getName() const;

    void       dumpFontBitmaps() const;
    const LLFontBitmapCache* getFontBitmapCache() const;

    void setStyle(U8 style);
    U8 getStyle() const;

private:
    void resetBitmapCache();
    void destroyHbFont();
    void setSubImageLuminanceAlpha(U32 x, U32 y, U32 bitmap_num, U32 width, U32 height, U8 *data, S32 stride = 0) const;
    bool setSubImageBGRA(U32 x, U32 y, U32 bitmap_num, U16 width, U16 height, const U8* data, U32 stride) const;
    bool setVariationAxis(const std::string& axis_tag, F32 value);
    bool hasGlyph(llwchar wch) const;       // Has a glyph for this character
    LLFontGlyphInfo* addGlyph(llwchar wch, EFontGlyphType glyph_type) const;        // Add a new character to the font if necessary
    LLFontGlyphInfo* addGlyphFromFont(
        const LLFontFreetype *fontp,
        llwchar wch,
        U32 glyph_index,
        EFontGlyphType bitmap_type) const; // Add a glyph from this font to the other (returns the glyph_index, 0 if not found)
    // Same as addGlyphFromFont but inserts into the glyph-id-keyed shaped
    // cache, for glyphs chosen by the HarfBuzz shaper rather than by codepoint.
    LLFontGlyphInfo* addShapedGlyphFromFont(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType bitmap_type) const;
    // Shared body for both addGlyphFromFont and addShapedGlyphFromFont — runs
    // the FreeType rasterizer, allocates an LLFontGlyphInfo and populates the
    // bitmap atlas. The caller is responsible for inserting the returned gi
    // into whichever cache it owns. `out_bitmap_glyph_type` receives the pixel
    // format FreeType actually delivered (which can differ from the requested
    // one — e.g. color requested but mono returned).
    LLFontGlyphInfo* renderAndCreateGlyph(const LLFontFreetype* fontp, U32 glyph_index, EFontGlyphType requested_glyph_type, EFontGlyphType& out_bitmap_glyph_type) const;
    void renderGlyph(EFontGlyphType bitmap_type, U32 glyph_index, llwchar wch) const;
    void insertGlyphInfo(llwchar wch, LLFontGlyphInfo* gi) const;
    void insertShapedGlyphInfo(const LLFontFreetype* fontp, U32 glyph_index, LLFontGlyphInfo* gi) const;

    std::string mName;

    U8 mStyle;

    F32 mPointSize;
    F32 mAscender;
    F32 mDescender;
    F32 mLineHeight;

    LLFT_Face mFTFace;

    bool mIsFallback;
    EFontHinting mHinting;
    S32 mFontFlags;
    S32 mWeight = -1;
    bool mAllowMonospaceLigatures = false;
    bool mUseSubpixelPen = false;
    typedef std::pair<LLPointer<LLFontFreetype>, char_functor_t> fallback_font_t;
    typedef std::vector<fallback_font_t> fallback_font_vector_t;
    fallback_font_vector_t mFallbackFonts; // A list of fallback fonts to look for glyphs in (for Unicode chars)

    // *NOTE: the same glyph can be present with multiple representations (but the pointer is always unique)
    typedef boost::unordered_multimap<llwchar, LLFontGlyphInfo*> char_glyph_info_map_t;
    mutable char_glyph_info_map_t mCharGlyphInfoMap; // Information about glyph location in bitmap

    // Shaped-glyph cache, used only for glyphs looked up via HarfBuzz glyph
    // indices. Kept separate from mCharGlyphInfoMap so the existing 1:1
    // codepoint->glyph hot path stays untouched. The key carries the source
    // face pointer so glyph indices from different fallback fonts don't
    // collide when they happen to numerically match.
    struct ShapedGlyphKey
    {
        const LLFontFreetype* face;
        U32                   glyph_index;
        bool operator==(const ShapedGlyphKey& o) const noexcept
        {
            return face == o.face && glyph_index == o.glyph_index;
        }
    };
    struct ShapedGlyphKeyHash
    {
        size_t operator()(const ShapedGlyphKey& k) const noexcept
        {
            size_t h = boost::hash<const void*>{}(k.face);
            boost::hash_combine(h, k.glyph_index);
            return h;
        }
    };
    typedef boost::unordered_multimap<ShapedGlyphKey, LLFontGlyphInfo*, ShapedGlyphKeyHash> shaped_glyph_info_map_t;
    mutable shaped_glyph_info_map_t mShapedGlyphInfoMap;

    mutable LLFontBitmapCache* mFontBitmapCachep;

    // HarfBuzz handle wrapping mFTFace. Lazily created on first getHbFont()
    // call and destroyed whenever mFTFace is replaced or the font is torn
    // down (see destroyHbFont()).
    mutable hb_font_t* mHbFont;

    mutable S32 mRenderGlyphCount;
};

#endif // LL_FONTFREETYPE_H
