/**
 * @file llfontregistry.h
 * @author Brad Payne
 * @brief Storage for fonts.
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

#ifndef LL_LLFONTREGISTRY_H
#define LL_LLFONTREGISTRY_H

#include "llpointer.h"
#include "alfontface.h"  // for ALFontFaceKey
#include "llsd.h"        // mLastFontOverrides stored by value

#include <boost/unordered_map.hpp>

class LLFontGL;
class LLFontFreetype;

// Forward-declare the test fixtures so LLFontRegistry's friend declarations
// can reference them. Defined inside namespace tut by
// indra/llrender/tests/llfontregistry_test.cpp.
namespace tut { struct llfontregistry_data; }
namespace tut { struct llfontregistry_gl_data; }

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

// ALFontVarAxes is defined in alfontface.h (so ALFontFaceKey can carry
// it) and re-used here without redefinition.

struct LLFontFileInfo
{
    LLFontFileInfo(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, const std::function<bool(llwchar)>& char_functor = nullptr, bool monospace_ligatures = false, bool load_collection = false, const ALFontVarAxes& var_axes = {})
        : FileName(file_name)
        , CharFunctor(char_functor)
        , mHinting(hinting)
        , mFlags(flags)
        , mVarAxes(var_axes)
        , mSizeDelta(size_delta)
        , mMonospaceLigatures(monospace_ligatures)
        , mLoadCollection(load_collection)
    {
    }

    std::string FileName;
    std::function<bool(llwchar)> CharFunctor;
    EFontHinting mHinting;
    S32 mFlags;
    // OpenType variation axes (wght, opsz, ital, wdth, slnt). Each axis
    // is independently gated by its matching *_set flag — unset = no
    // FT_Set_Var call for that axis = file default carries through.
    // The legacy `weight` field is gone; var_axes.wght carries it now.
    ALFontVarAxes mVarAxes;

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

    // Opt-in: this file is a TTC/OTC collection — createFont probes
    // FT's num_faces and creates one LLFontGL per face. Off by default
    // so we don't pay the open/probe cost for plain <ttf/otf/woff2>.
    // Set via <file load_collection="true"> in fonts.xml.
    bool mLoadCollection;

    // The font family that contributed this file via its <style> block.
    // Stamped at parse time and preserved across <use>-chain copies and
    // override prepends, so createFont can look up the source family's
    // per-family <size force="true"> entry and override this file's
    // point size when rendering. Empty for files without an attributable
    // family (e.g. ultimate-fallback list, bare-filename overrides).
    std::string mSourceFamily;
};
typedef std::vector<LLFontFileInfo> font_file_info_vec_t;

class LLFontDescriptor
{
public:
    LLFontDescriptor();
    LLFontDescriptor(const std::string& name, const std::string& size, const U8 style);
    LLFontDescriptor(const std::string& name, const std::string& size, const U8 style, const font_file_info_vec_t& font_list);
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

    void addFontFile(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, const std::function<bool(llwchar)>& char_functor = nullptr, bool monospace_ligatures = false, bool load_collection = false, const ALFontVarAxes& var_axes = {});
    const font_file_info_vec_t & getFontFiles() const { return mFontFiles; }
    void setFontFiles(const font_file_info_vec_t& font_files) { mFontFiles = font_files; }
    // Stamp `family` onto every file in this descriptor whose mSourceFamily
    // is currently empty. Caller invokes this right after parsing so files
    // contributed by this <font>'s own <style> blocks carry the family
    // tag while files copied later from <use> chains retain their
    // already-set source. Used by createFont to look up forced per-family
    // sizes that should override the chain's point size.
    void tagSourceFamily(const std::string& family)
    {
        for (auto& f : mFontFiles)
        {
            if (f.mSourceFamily.empty())
                f.mSourceFamily = family;
        }
    }

    const U8 getStyle() const { return mStyle; }
    void setStyle(U8 style) { mStyle = style; }

private:
    std::string mName;
    std::string mSize;
    font_file_info_vec_t mFontFiles;
    U8 mStyle;
};

class LLFontRegistry
{
public:
    friend bool init_from_xml(LLFontRegistry*, LLPointer<class LLXMLNode>);
    // Test fixture in indra/llrender/tests/llfontregistry_test.cpp drives
    // resolveFontReferences / applyFamilyOverrides directly so the resolver
    // unit tests don't have to set up gDirUtilp + a temp skin tree just to
    // reach parseFontInfo. Lives in `namespace tut`, where TUT requires
    // the fixture struct.
    friend struct ::tut::llfontregistry_data;
    // GL-fixture sibling: needs the same private-member access to drive
    // simulateReload-style state wipes in tests that exercise actual
    // freetype loading + size-change reloads.
    friend struct ::tut::llfontregistry_gl_data;
    // create_gl_textures - set to false for test apps with no OpenGL window,
    // such as llui_libtest
    LLFontRegistry(bool create_gl_textures);
    ~LLFontRegistry();

    // Load standard font info from XML file(s). `font_overrides` is an LLSD
    // map of family-name → file/family override (caller supplies; the
    // registry no longer reads gSavedSettings directly).
    bool parseFontInfo(const std::string& xml_filename, const LLSD& font_overrides);

    // Parse a single fonts.xml at an explicit path, bypassing
    // gDirUtilp->findSkinnedFilenames. For headless test bring-up where
    // the skin tree isn't staged. Resolves <use> / inherit / overrides
    // exactly like parseFontInfo does after its per-file pass — single
    // file only, no cross-skin layering.
    bool parseFontInfoFromFile(const std::string& xml_path, const LLSD& font_overrides);

    // Re-parse fonts.xml and re-apply caller-supplied font overrides.
    // Existing LLFontGL* pointers stay valid — each head's underlying
    // mFontFreetype is swapped in place. Returns false on parse failure
    // (registry left untouched).
    bool reload(const LLSD& font_overrides);

    // Once-per-frame glyph cache sweep. Walks every head and shared
    // fallback freetype and calls its collectGarbage() — each freetype
    // self-throttles via its own mNextGcTime, so most calls are no-ops.
    // Centralized here so the render hot path doesn't pay the
    // throttle-check cost on every render() call.
    void sweepGlyphCaches();

    // Fast path for DPI/scale changes when fonts.xml content and
    // overrides are unchanged. Walks heads + shared fallback freetypes
    // and re-loads each at the current sVertDPI/sHorizDPI. Skips the
    // expensive parseFontInfo + createFont + generateASCIIglyphs cycle
    // — atlas glyphs lazily re-rasterize on first render through
    // each new face wrapper. Caller must have verified that overrides
    // match (compare via overridesEqual) and fonts.xml hasn't been
    // marked dirty since the last full parse.
    void reloadForDpiChange();

    // True iff `candidate` is byte-equal to the LLSD overrides currently
    // applied to the registry (cached on the last parseFontInfo /
    // applyFamilyOverrides call). Used by LLFontGL::initClass to decide
    // between full reload and fast DPI-only reload.
    bool overridesEqual(const LLSD& candidate) const;

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

    // Filter for getAvailableFamilies. Backed by per-family
    // `monospace="true|false"` and `emoji="true|false"` attributes in
    // fonts.xml (both default to false). The three categories are
    // treated as mutually exclusive at filter time so the picker
    // doesn't show e.g. an emoji family in the proportional list:
    //   PROPORTIONAL: !monospace && !emoji
    //   MONOSPACE:     monospace && !emoji
    //   EMOJI:         emoji      (regardless of monospace)
    enum class FamilyFilter
    {
        ANY,           // any user-selectable family
        MONOSPACE,     // monospace="true" AND emoji is unset
        PROPORTIONAL,  // monospace is unset AND emoji is unset
        EMOJI,         // emoji="true"
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
    // plus the LLFontFreetype-level flags that don't go on ALFontFace.
    struct FallbackInstanceKey
    {
        ALFontFaceKey face_key;
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

private:
    LLFontRegistry(const LLFontRegistry& other); // no-copy
    LLFontGL *createFont(const LLFontDescriptor& desc);
    // Assign mFontMap[desc] = fontp, deleting any different LLFontGL the
    // slot already owns. Map values are owned raw pointers, so plain
    // assignment on an occupied slot leaks; occupied slots happen when
    // reload() drives createFont directly over a map that already holds
    // interim wrappers (canonical aliases, fresh heads from earlier
    // iterations). Null-safe on both the incumbent and `fontp`.
    void storeFont(const LLFontDescriptor& desc, LLFontGL* fontp);
    // Resolve cross-family <use> references, then per-family inherit="true"
    // style variants, in that order. Idempotent — consumes mFamilyUses and
    // mInheritFlags so re-running over a partially-resolved registry is safe.
    void resolveFontReferences(const LLSD& font_overrides);
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
    // all skin layers have loaded. Each ref carries an optional CharFunctor
    // built from a `<use unicode_ranges="...">` attribute; when present, the
    // functor composes (intersects) with each contributed file's own
    // CharFunctor so a single source family can serve multiple consumers
    // each applying their own glyph filter at the use site (e.g. EmojiBase
    // shared between SansSerifEmoji's broad UTS#51 set and
    // SansSerifLimitedEmoji's narrower set without duplicating the file).
    struct FamilyUseRef
    {
        std::string                  family;
        std::function<bool(llwchar)> functor;  // null = no use-level filter
    };
    typedef std::map<std::string, std::vector<FamilyUseRef>> family_uses_map_t;
    // Family-level metadata read from <font> attributes. All four fields
    // are optional: ui_label defaults to the family name, user_selectable
    // defaults to true (preserving pre-attribute behavior), monospace and
    // emoji default to false. monospace and emoji classify the family
    // for the Preferences "UI Font" / "Mono Font" / "Emoji Font" pickers
    // — the data path itself doesn't care about either flag at the
    // family level (per-file `ligatures` / `font_weight` etc. handle the
    // rendering side; emoji-ness is enforced via per-file unicode_ranges
    // gates on the actual COLRv1 file).
    struct FamilyMeta
    {
        std::string ui_label;
        bool        user_selectable = true;
        bool        monospace = false;
        bool        emoji = false;
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
    // `family` during resolveFontReferences(). Consumed at the end of
    // resolveFontReferences so a re-call doesn't double-append.
    family_uses_map_t mFamilyUses;
    // Per-family ui_label / user_selectable from fonts.xml. Absent entries
    // mean defaults: label = family name, selectable = true.
    family_meta_map_t mFamilyMeta;
    // Cache of fallback LLFontFreetype instances keyed by face params +
    // monospace_ligatures. Heads always create fresh (their fallback chain
    // and atlas are head-specific); fallback instances dedup. Shared across
    // every head that lists the same fallback file at matching params.
    boost::unordered_map<FallbackInstanceKey, LLPointer<class LLFontFreetype>> mFallbackInstanceCache;

    string_vec_t mUltimateFallbackList;
    bool mCreateGLTextures;

    // Snapshot of the LLSD overrides last applied via parseFontInfo /
    // applyFamilyOverrides. Compared (via llsd_equals) against the next
    // call's overrides to decide whether the registry can take the fast
    // DPI-only reload path or needs a full re-parse.
    LLSD mLastFontOverrides;
};

#endif // LL_LLFONTREGISTRY_H
