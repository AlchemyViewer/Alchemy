/**
 * @file llfontregistry.h
 * @author Brad Payne
 * @brief Storage for fonts.
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

#ifndef LL_LLFONTREGISTRY_H
#define LL_LLFONTREGISTRY_H

#include "llpointer.h"
#include "llfontface.h"  // for LLFontFaceKey

#include <boost/unordered_map.hpp>

class LLFontGL;
class LLFontFreetype;
class LLSD;

typedef std::vector<std::string> string_vec_t;

enum class EFontHinting : S32
{
    DEFAULT = 0,
    NO_HINTING = 0x8000U,
    FORCE_AUTOHINT = 0x20,
    // Light autohinter: vertical hinting only, no horizontal grid-fitting.
    // Pairs with subpixel pen position (mUseSubpixelPen) to give smooth,
    // weight-stable text at the cost of slightly softer stems vs DEFAULT.
    LIGHT = 0x10020, // FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT
};

struct LLFontFileInfo
{
    LLFontFileInfo(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, S32 weight, const std::function<bool(llwchar)>& char_functor = nullptr, bool monospace_ligatures = false)
        : FileName(file_name)
        , CharFunctor(char_functor)
        , mHinting(hinting)
        , mFlags(flags)
        , mWeight(weight)
        , mSizeDelta(size_delta)
        , mMonospaceLigatures(monospace_ligatures)
    {
    }

    LLFontFileInfo(const LLFontFileInfo& ffi, EFontHinting hinting, S32 flags, F32 size_delta, S32 weight)
        : FileName(ffi.FileName)
        , CharFunctor(ffi.CharFunctor)
        , mHinting(hinting)
        , mFlags(flags)
        , mWeight(weight)
        , mSizeDelta(size_delta)
        , mMonospaceLigatures(ffi.mMonospaceLigatures)
    {
    }

    std::string FileName;
    std::function<bool(llwchar)> CharFunctor;
    EFontHinting mHinting;
    S32 mFlags;
    S32 mWeight; // -1 - default, whatever is in the file.

    // Not all fonts are the same size, Ex: dejavu is bigger than inter,
    // so in some cases we want to adjust relative sizes to make characters
    // from different files match.
    F32 mSizeDelta;

    // Opt-in: keep liga/clig/dlig/calt features enabled even when the face
    // is fixed-width. For programmer fonts (Fira Code, JetBrains Mono,
    // Cascadia Code, Iosevka) where ligatures are width-preserving by design
    // and intrinsic to the font's purpose. Set via <font ligatures="on"> in
    // fonts.xml. Has no effect on non-fixed-width faces.
    bool mMonospaceLigatures;
};
typedef std::vector<LLFontFileInfo> font_file_info_vec_t;

class LLFontDescriptor
{
public:
    LLFontDescriptor();
    LLFontDescriptor(const std::string& name, const std::string& size, const U8 style);
    LLFontDescriptor(const std::string& name, const std::string& size, const U8 style, const font_file_info_vec_t& font_list);
    LLFontDescriptor(const std::string& name, const std::string& size, const U8 style, const font_file_info_vec_t& font_list, const font_file_info_vec_t& font_collection_list);
    LLFontDescriptor normalize() const;

    bool operator<(const LLFontDescriptor& b) const;

    bool operator==(const LLFontDescriptor& rhs) const
    {
        return mName == rhs.mName && mStyle == rhs.mStyle && mSize == rhs.mSize;
    }

    friend std::size_t hash_value(LLFontDescriptor const& font)
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, font.mName);
        boost::hash_combine(seed, font.mStyle);
        boost::hash_combine(seed, font.mSize);
        return seed;
    }


    bool isTemplate() const;

    const std::string& getName() const { return mName; }
    void setName(const std::string& name) { mName = name; }
    const std::string& getSize() const { return mSize; }
    void setSize(const std::string& size) { mSize = size; }

    void addFontFile(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, S32 weight, const std::string& char_functor = LLStringUtil::null, bool monospace_ligatures = false);
    void addFontFile(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, S32 weight, const std::function<bool(llwchar)>& char_functor, bool monospace_ligatures = false);
    const font_file_info_vec_t & getFontFiles() const { return mFontFiles; }
    void setFontFiles(const font_file_info_vec_t& font_files) { mFontFiles = font_files; }
    void addFontCollectionFile(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, S32 weight, const std::string& char_functor = LLStringUtil::null, bool monospace_ligatures = false);
    void addFontCollectionFile(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, S32 weight, const std::function<bool(llwchar)>& char_functor, bool monospace_ligatures = false);
    const font_file_info_vec_t& getFontCollectionFiles() const { return mFontCollectionFiles; }
    void setFontCollectionFiles(const font_file_info_vec_t& font_collection_files) { mFontCollectionFiles = font_collection_files; }

    const U8 getStyle() const { return mStyle; }
    void setStyle(U8 style) { mStyle = style; }

private:
    std::string mName;
    std::string mSize;
    font_file_info_vec_t mFontFiles;
    font_file_info_vec_t mFontCollectionFiles;
    U8 mStyle;

    typedef std::map<std::string, std::function<bool(llwchar)>> char_functor_map_t;
    static char_functor_map_t mCharFunctors;
};

class LLFontRegistry
{
public:
    friend bool init_from_xml(LLFontRegistry*, LLPointer<class LLXMLNode>);
    // create_gl_textures - set to false for test apps with no OpenGL window,
    // such as llui_libtest
    LLFontRegistry(bool create_gl_textures);
    ~LLFontRegistry();

    // Load standard font info from XML file(s).
    bool parseFontInfo(const std::string& xml_filename);

    // Clear cached glyphs for all fonts.
    void reset();

    // Re-parse fonts.xml and re-apply user font overrides
    // (AlchemyUIFontOverrides). Existing LLFontGL* pointers stay valid —
    // each head's underlying mFontFreetype is swapped in place. Returns
    // false on parse failure (registry left untouched).
    bool reload();

    // Destroy all fonts.
    void clear();

    // GL cleanup
    void destroyGL();

    LLFontGL *getFont(const LLFontDescriptor& desc);
    const LLFontDescriptor *getMatchingFontDesc(const LLFontDescriptor& desc);
    const LLFontDescriptor *getClosestFontTemplate(const LLFontDescriptor& desc);

    // (name, ui_label) for one <font> entry. `name` is the canonical
    // family key (matches `<font name="...">` and the override-map key);
    // `label` is the friendly text shown in the Preferences dropdown,
    // taken from the `ui_label` attribute or defaulting to `name`.
    struct FamilyInfo
    {
        std::string name;
        std::string label;
    };

    // Filter for getAvailableFamilies. Backed by the per-family
    // `monospace="true|false"` attribute in fonts.xml (defaults to false).
    enum class FamilyFilter
    {
        ANY,           // any user-selectable family
        MONOSPACE,     // only families with monospace="true"
        PROPORTIONAL,  // only families WITHOUT monospace="true"
    };

    // Families declared in fonts.xml (after skin layering and reference
    // resolution) that are user-selectable: the implicit "default" entry
    // and any family marked `user_selectable="false"` are filtered out;
    // `filter` further constrains the result by the family's monospace
    // attribute. Sorted by label. Used by the Preferences UI to populate
    // font-override dropdowns.
    std::vector<FamilyInfo> getAvailableFamilies(FamilyFilter filter = FamilyFilter::ANY) const;

    // Look up the point size for a size name, optionally honoring per-family
    // overrides (<size> children of <font> in fonts.xml). Pass an empty family
    // to skip the per-family lookup and consult only the global table.
    bool nameToSize(const std::string& family, const std::string& size_name, F32& size);
    bool nameToSize(const std::string& size_name, F32& size);

    void dump();
    void dumpTextures();

    const string_vec_t& getUltimateFallbackList() const;

    // Identity key for a fallback LLFontFreetype instance — face params
    // plus the LLFontFreetype-level flags that don't go on LLFontFace.
    struct FallbackInstanceKey
    {
        LLFontFaceKey face_key;
        bool          monospace_ligatures;

        bool operator==(const FallbackInstanceKey& o) const noexcept
        {
            return face_key == o.face_key && monospace_ligatures == o.monospace_ligatures;
        }
        friend std::size_t hash_value(const FallbackInstanceKey& k) noexcept
        {
            std::size_t seed = hash_value(k.face_key);
            boost::hash_combine(seed, k.monospace_ligatures);
            return seed;
        }
    };

    // Look up or create a fallback LLFontFreetype with the supplied params.
    // Returns null on load failure. The returned instance is registered in
    // mFallbackInstanceCache and shared across every head that asks for
    // matching params.
    LLPointer<class LLFontFreetype> getOrCreateFallbackFont(
        const std::string& font_path,
        const LLFontFileInfo& file_info,
        F32 point_size, F32 vert_dpi, F32 horz_dpi, S32 face_index,
        EFontHinting hinting, S32 flags);

private:
    LLFontRegistry(const LLFontRegistry& other); // no-copy
    LLFontGL *createFont(const LLFontDescriptor& desc);
    // Resolve cross-family <use> references, then per-family inherit="true"
    // style variants, in that order. Idempotent — consumes mFamilyUses and
    // mInheritFlags so re-running over a partially-resolved registry is safe.
    void resolveFontReferences();
    // Apply per-family user overrides from AlchemyUIFontOverrides setting.
    // Each override prepends source files (another family's resolved files,
    // or a single user-supplied font file) ahead of the target family's
    // existing chain; the original chain stays as fallback so DejaVu/Emoji/
    // CJK coverage is preserved. Called from resolveFontReferences after the
    // <use> and inherit="true" resolution so overrides see fully composed
    // file lists.
    void applyFamilyOverrides(const LLSD& overrides);
    // Insert a fresh descriptor, or merge it into an existing same-key entry
    // by prepending its files (used for cross-skin layering).
    void mergeFontEntry(const LLFontDescriptor& desc);
    // Process a new-format <font> block (one with <style> children) into
    // separate (family, style) descriptors plus family-level metadata.
    void processNewFormatFont(LLPointer<class LLXMLNode> font_node);
    typedef boost::unordered_map<LLFontDescriptor,LLFontGL*> font_reg_map_t;
    typedef boost::unordered_map<std::string,F32> font_size_map_t;
    typedef boost::unordered_map<std::string, font_size_map_t> family_size_map_t;
    // Key: (family name, style flags). Stores the inherit="true" intent for
    // a style variant; expanded at the end of parseFontInfo.
    typedef std::map<std::pair<std::string, U8>, bool> inherit_map_t;
    // Per-family <use family="X"/> references, resolved at parse-time after
    // all skin layers have loaded.
    typedef std::map<std::string, std::vector<std::string>> family_uses_map_t;
    // Family-level metadata read from <font> attributes. All three fields
    // are optional: ui_label defaults to the family name, user_selectable
    // defaults to true (preserving pre-attribute behavior), monospace
    // defaults to false. monospace classifies the family for the
    // Preferences "UI Font" vs "Mono Font" pickers — the data path itself
    // doesn't care about monospace at the family level (per-file
    // `ligatures` / `font_weight` etc. handle the rendering side).
    struct FamilyMeta
    {
        std::string ui_label;
        bool        user_selectable = true;
        bool        monospace = false;
    };
    typedef std::map<std::string, FamilyMeta> family_meta_map_t;

    // Given a descriptor, look up specific font instantiation.
    font_reg_map_t mFontMap;
    // Given a size name, look up the point size.
    font_size_map_t mFontSizes;
    // Per-family size overrides: mFamilySizes[family][size_name] = pt size.
    // Consulted before mFontSizes when a family declares its own <size>.
    family_size_map_t mFamilySizes;
    // Style variants that requested inherit="true" — append the parent
    // NORMAL-style entry's resolved files after parsing.
    inherit_map_t mInheritFlags;
    // mFamilyUses[family] = [other_family, ...] — append each referenced
    // family's matching-style files (or NORMAL fallback) to every style of
    // `family` during resolveFontReferences().
    family_uses_map_t mFamilyUses;
    // Per-family ui_label / user_selectable from fonts.xml. Absent entries
    // mean defaults: label = family name, selectable = true.
    family_meta_map_t mFamilyMeta;
    // target_family -> source_family for family-name overrides applied via
    // AlchemyUIFontOverrides. nameToSize consults the source family's
    // per-family <size> table before the target's, so a Monospace ->
    // SourceCode override picks up SourceCode's own size scale (if any).
    // Only family-name overrides populate this; file-name overrides
    // don't, since there's no source family to draw size metadata from.
    std::map<std::string, std::string> mFamilyOverrideSources;
    // Cache of fallback LLFontFreetype instances keyed by face params +
    // monospace_ligatures. Heads always create fresh (their fallback chain
    // and atlas are head-specific); fallback instances dedup. Shared across
    // every head that lists the same fallback file at matching params.
    boost::unordered_map<FallbackInstanceKey, LLPointer<class LLFontFreetype>> mFallbackInstanceCache;

    string_vec_t mUltimateFallbackList;
    bool mCreateGLTextures;
};

#endif // LL_LLFONTREGISTRY_H
