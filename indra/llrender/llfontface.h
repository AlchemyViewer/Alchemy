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
#include "llfontregistry.h"  // for EFontHinting
#include "stdtypes.h"

#include <boost/functional/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <string>

// Forward declarations — match the trick used in llfontfreetype.h to avoid
// pulling in <ft2build.h> + FT_FREETYPE_H from this header.
struct FT_FaceRec_;
typedef struct FT_FaceRec_* LLFT_Face;
struct hb_font_t;

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

private:
    LLFontFace(const LLFontFace&) = delete;
    LLFontFace& operator=(const LLFontFace&) = delete;

    // Apply a single OpenType variation axis value, if the face has one.
    bool setVariationAxis(const std::string& axis_tag, F32 value);

    LLFT_Face          mFTFace = nullptr;
    EFontHinting       mHinting;
    mutable hb_font_t* mHbFont = nullptr;
    mutable boost::unordered_flat_map<llwchar, U32> mCharIndexCache;

    bool mUseSubpixelPen = false;
    bool mHasColor       = false;
    bool mHasSvg         = false;
    bool mIsFixedWidth   = false;
    bool mWghtAxisSet    = false;
};

#endif // LL_LLFONTFACE_H
