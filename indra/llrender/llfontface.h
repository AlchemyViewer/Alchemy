/**
 * @file llfontface.h
 * @brief Refcounted wrapper around an FT_Face, sharable across LLFontFreetype
 *        instances that request the same (file, sized + variable axis) state.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef LL_LLFONTFACE_H
#define LL_LLFONTFACE_H

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
typedef struct FT_FaceRec_* LLFT_Face;
struct hb_font_t;
struct LLFontGlyphInfo;
class  LLFontFreetype;
// Defined in llfontregistry.h; forward-declared here to keep this header
// independent of the registry. Translation units that need the values
// (e.g. llfontface.cpp) include llfontregistry.h directly.
enum class EFontHinting : S32;

// Identity key for an FT_Face. Two LLFontFreetype instances that resolve to
// the same key share the underlying face (via LLFontManager's cache). The
// key fields are exactly the inputs to FT_Open_Face / FT_Set_Char_Size /
// FT_Set_Var_Design_Coordinates that determine the face's observable state.
struct LLFontFaceKey
{
    std::string  filename;
    S32          face_index;
    F32          point_size;
    F32          vert_dpi;
    F32          horz_dpi;
    S32          weight;       // -1 = use the file's default
    EFontHinting hinting;
    S32          flags;

    bool operator==(const LLFontFaceKey& o) const noexcept
    {
        return filename == o.filename
            && face_index == o.face_index
            && point_size == o.point_size
            && vert_dpi == o.vert_dpi
            && horz_dpi == o.horz_dpi
            && weight == o.weight
            && hinting == o.hinting
            && flags == o.flags;
    }

    friend std::size_t hash_value(const LLFontFaceKey& k) noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, k.filename);
        boost::hash_combine(seed, k.face_index);
        boost::hash_combine(seed, k.point_size);
        boost::hash_combine(seed, k.vert_dpi);
        boost::hash_combine(seed, k.horz_dpi);
        boost::hash_combine(seed, k.weight);
        boost::hash_combine(seed, static_cast<S32>(k.hinting));
        boost::hash_combine(seed, k.flags);
        return seed;
    }
};

class LLFontFace : public LLRefCount
{
public:
    LLFontFace();
    ~LLFontFace();

    // Open the underlying FT_Face, set its size/DPI, and apply variable-axis
    // values if applicable. Returns false if FreeType can't open the file or
    // can't set the requested size; in that case the LLFontFace is left in a
    // dead-but-harmless state (isValid() returns false).
    bool load(const std::string& filename, S32 face_index,
              F32 point_size, F32 vert_dpi, F32 horz_dpi,
              S32 weight, EFontHinting hinting, S32 flags);

    LLFT_Face face() const { return mFTFace; }
    bool      isValid() const { return mFTFace != nullptr; }
    EFontHinting hinting() const { return mHinting; }

    // Codepoint -> FT glyph index, cached. Equivalent to FT_Get_Char_Index
    // but skips the cmap binary search on repeated lookups. A cached value
    // of 0 is meaningful — it means the face has no glyph for `wch` — and
    // prevents re-querying on every miss.
    U32 getCharGlyphIndex(llwchar wch) const;

    // HarfBuzz handle wrapping mFTFace. Lazily created on first call. Tied
    // to this wrapper's lifetime; destroyed in ~LLFontFace.
    hb_font_t* getHbFont() const;

    // Pure functions of (file, hinting). Stored at load time so callers
    // don't re-deref FT_Face flags on every check.
    bool useSubpixelPen() const { return mUseSubpixelPen; }
    bool hasColor() const       { return mHasColor; }
    bool hasSvg() const         { return mHasSvg; }
    bool isFixedWidth() const   { return mIsFixedWidth; }
    // True iff load() successfully applied a "wght" variation axis value.
    // Used by LLFontFreetype's BOLD-style synthesis to decide whether a
    // weight>=600 request already produced a heavy face on its own (in
    // which case programmatic bolding should be suppressed).
    bool wghtAxisSet() const    { return mWghtAxisSet; }

    // Per-face bitmap atlas for glyphs rasterized through this face. Shared
    // across every LLFontFreetype that wraps this face — heads sharing a
    // primary face share the atlas; the same Twemoji face used as a fallback
    // by N heads writes its emoji glyphs into ONE atlas instead of N.
    LLFontBitmapCache* getBitmapCache() const { return mFontBitmapCachep; }

    // Per-face glyph cache. Keyed on codepoint (llwchar) for the codepoint
    // path, and on FT glyph index for the shaped path. LLFontGlyphInfo
    // entries are owned here and deleted in ~LLFontFace.
    LLFontGlyphInfo* findGlyphInfo(llwchar wch, EFontGlyphType type) const;
    void             insertGlyphInfo(llwchar wch, LLFontGlyphInfo* gi) const;

    LLFontGlyphInfo* findShapedGlyphInfo(U32 glyph_index, EFontGlyphType type) const;
    void             insertShapedGlyphInfo(U32 glyph_index, LLFontGlyphInfo* gi) const;

    // Iterate and conditionally erase entries. Used by collectGarbage to
    // purge entries that referenced an evicted atlas sheet.
    template<typename Pred>
    void erase_codepoint_entries(Pred should_erase) const;
    template<typename Pred>
    void erase_shaped_entries(Pred should_erase) const;

    // Drop all rasterized glyphs and reset the atlas. Used by the registry
    // when DPI changes and the wrapper survives but its atlas state needs
    // to be rebuilt.
    void resetBitmapCache();

    // Free GL textures while keeping the wrapper alive (used at shutdown
    // when teardown order requires GL state release before destruction).
    void destroyGL();

    // Copy a rasterized 8-bit grayscale bitmap into this face's atlas at slot
    // (type=Grayscale, bitmap_num) at offset (x, y). The atlas is RGBA8 with
    // RGB pre-cleared to 255; this function only writes the .a (coverage)
    // byte per pixel.
    void setSubImageGrayscale(U32 x, U32 y, U32 bitmap_num,
                              U32 width, U32 height,
                              U8* data, S32 stride = 0) const;
    // Same for the Color (BGRA) atlas.
    bool setSubImageBGRA(U32 x, U32 y, U32 bitmap_num,
                         U16 width, U16 height,
                         const U8* data, U32 stride) const;

private:
    LLFontFace(const LLFontFace&) = delete;
    LLFontFace& operator=(const LLFontFace&) = delete;

    // Apply a single OpenType variation axis value, if the face has one.
    bool setVariationAxis(const std::string& axis_tag, F32 value);

    typedef boost::unordered_multimap<llwchar, LLFontGlyphInfo*> char_glyph_info_map_t;
    typedef boost::unordered_multimap<U32, LLFontGlyphInfo*> shaped_glyph_info_map_t;

    LLFT_Face          mFTFace = nullptr;
    EFontHinting       mHinting;
    mutable hb_font_t* mHbFont = nullptr;
    mutable boost::unordered_flat_map<llwchar, U32> mCharIndexCache;

    LLFontBitmapCache* mFontBitmapCachep = nullptr;
    mutable char_glyph_info_map_t   mCharGlyphInfoMap;
    mutable shaped_glyph_info_map_t mShapedGlyphInfoMap;

    bool mUseSubpixelPen = false;
    bool mHasColor       = false;
    bool mHasSvg         = false;
    bool mIsFixedWidth   = false;
    bool mWghtAxisSet    = false;
};

// Inline template definitions — kept in the header so callers in
// llfontfreetype.cpp can instantiate with their own predicates.
template<typename Pred>
void LLFontFace::erase_codepoint_entries(Pred should_erase) const
{
    for (auto it = mCharGlyphInfoMap.begin(); it != mCharGlyphInfoMap.end(); )
    {
        if (should_erase(it->second))
        {
            delete it->second;
            it = mCharGlyphInfoMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

template<typename Pred>
void LLFontFace::erase_shaped_entries(Pred should_erase) const
{
    for (auto it = mShapedGlyphInfoMap.begin(); it != mShapedGlyphInfoMap.end(); )
    {
        if (should_erase(it->second))
        {
            delete it->second;
            it = mShapedGlyphInfoMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

#endif // LL_LLFONTFACE_H
