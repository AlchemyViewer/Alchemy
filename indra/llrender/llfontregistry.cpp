/**
 * @file llfontregistry.cpp
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

#include "linden_common.h"
#include "llgl.h"
#include "llfontfreetype.h"
#include "llfontgl.h"
#include "llfontregistry.h"
#include "alfontshaping.h"
#include <algorithm>
#include <set>
#include <boost/tokenizer.hpp>
#include "lldir.h"
#include "llsd.h"
#include "llsdutil.h"   // llsd_equals
#include "llwindow.h"
#include "llxmlnode.h"

using std::string;
using std::map;

namespace
{
    // Defaults declared at <font> level that propagate to child <file> entries
    // (and through <os> recursion) unless the child overrides them. Mirrors
    // the existing handling of the family-level "ligatures" attribute.
    struct FamilyDefaults
    {
        bool monospace_ligatures = false;
        EFontHinting hinting = EFontHinting::FORCE_AUTOHINT;
        bool hinting_set = false;
        bool load_collection = false;
        bool load_collection_set = false;
        // Family-level OpenType variation axis defaults (all five OT
        // axes the registry exposes: wght, opsz, ital, wdth, slnt).
        // Each *_set flag tracks whether the <font> declared the
        // matching attribute; child <file> entries inherit the value
        // when they don't override. font_weight is parsed as S32 from
        // XML and stored as F32 here so all five axes share one shape.
        ALFontVarAxes var_axes;
    };

    typedef boost::unordered_map<std::string, F32> family_size_overrides_t;

    // Output of parsing a single top-level <font> element beyond the
    // descriptor itself: per-family size overrides and the inherit-fallbacks
    // flag. Recursive calls (for <os>) leave these untouched.
    struct FontParseExtras
    {
        family_size_overrides_t family_sizes;
        bool inherit = false;
    };

    std::string trimSpaces(const std::string& s)
    {
        size_t start = 0;
        size_t end = s.size();
        while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r'))
            ++start;
        while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\n' || s[end-1] == '\r'))
            --end;
        return s.substr(start, end - start);
    }

    bool parseHexCodepoint(const std::string& token, llwchar& out)
    {
        std::string s = trimSpaces(token);
        if (s.size() >= 2 && (s[0] == 'U' || s[0] == 'u') && s[1] == '+')
            s = s.substr(2);
        else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            s = s.substr(2);
        if (s.empty())
            return false;
        char* end = nullptr;
        unsigned long v = std::strtoul(s.c_str(), &end, 16);
        if (!end || *end != '\0')
            return false;
        out = static_cast<llwchar>(v);
        return true;
    }

    // Build a CharFunctor that returns true when a codepoint falls inside any
    // of the supplied "U+XXXX" or "U+XXXX-U+YYYY" ranges. Captures a sorted
    // shared vector so the closure stays cheap to copy through the existing
    // CharFunctor plumbing.
    std::function<bool(llwchar)> parseUnicodeRanges(const std::string& spec)
    {
        std::vector<std::pair<llwchar, llwchar>> ranges;
        size_t pos = 0;
        while (pos <= spec.size())
        {
            size_t comma = spec.find(',', pos);
            std::string item = spec.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
            pos = (comma == std::string::npos) ? spec.size() + 1 : comma + 1;
            item = trimSpaces(item);
            if (item.empty())
                continue;
            size_t dash = item.find('-');
            llwchar lo = 0, hi = 0;
            if (dash == std::string::npos)
            {
                if (!parseHexCodepoint(item, lo))
                {
                    LL_WARNS() << "fonts.xml: bad unicode_ranges token '" << item << "'" << LL_ENDL;
                    continue;
                }
                hi = lo;
            }
            else
            {
                if (!parseHexCodepoint(item.substr(0, dash), lo) ||
                    !parseHexCodepoint(item.substr(dash + 1), hi))
                {
                    LL_WARNS() << "fonts.xml: bad unicode_ranges range '" << item << "'" << LL_ENDL;
                    continue;
                }
                if (hi < lo)
                    std::swap(lo, hi);
            }
            ranges.emplace_back(lo, hi);
        }
        if (ranges.empty())
            return nullptr;
        std::sort(ranges.begin(), ranges.end());
        auto shared = std::make_shared<std::vector<std::pair<llwchar, llwchar>>>(std::move(ranges));
        return [shared](llwchar cp) -> bool {
            auto it = std::upper_bound(shared->begin(), shared->end(), cp,
                [](llwchar c, const std::pair<llwchar, llwchar>& r) { return c < r.first; });
            if (it == shared->begin())
                return false;
            --it;
            return cp >= it->first && cp <= it->second;
        };
    }

    bool font_desc_init_from_xml(LLXMLNodePtr node,
                                 LLFontDescriptor& desc,
                                 FamilyDefaults defaults = FamilyDefaults(),
                                 FontParseExtras* extras = nullptr);
}

bool init_from_xml(LLFontRegistry* registry, LLXMLNodePtr node);

const std::string MACOSX_FONT_PATH_LIBRARY = "/Library/Fonts/";
const std::string MACOSX_FONT_SUPPLEMENTAL = "Supplemental/";

LLFontDescriptor::LLFontDescriptor():
    mStyle(0)
{
}

LLFontDescriptor::LLFontDescriptor(const std::string& name,
                                   const std::string& size,
                                   const U8 style,
                                   const font_file_info_vec_t& font_files):
    mName(name),
    mSize(size),
    mStyle(style),
    mFontFiles(font_files)
{
}

LLFontDescriptor::LLFontDescriptor(const std::string& name,
                                   const std::string& size,
                                   const U8 style):
    mName(name),
    mSize(size),
    mStyle(style)
{
}

bool LLFontDescriptor::operator<(const LLFontDescriptor& b) const
{
    if (mName < b.mName)
        return true;
    else if (mName > b.mName)
        return false;

    if (mStyle < b.mStyle)
        return true;
    else if (mStyle > b.mStyle)
        return false;

    if (mSize < b.mSize)
        return true;
    else
        return false;
}

static const std::string s_template_string("TEMPLATE");

bool LLFontDescriptor::isTemplate() const
{
    return getSize() == s_template_string;
}

// Look for substring match and remove substring if matched.
bool removeSubString(std::string& str, const std::string& substr)
{
    size_t pos = str.find(substr);
    if (pos != string::npos)
    {
        str.erase(pos, substr.size());
        return true;
    }
    return false;
}

// Check for substring match without modifying the source string.
bool findSubString(std::string& str, const std::string& substr)
{
    size_t pos = str.find(substr);
    if (pos != string::npos)
    {
        return true;
    }
    return false;
}


// Normal form is
// - raw name
// - bold, italic style info reflected in both style and font name.
// - other style info removed.
// - size info moved to mSize, defaults to Medium
// For example,
// - "SansSerifHuge" would normalize to { "SansSerif", "Huge", 0 }
// - "SansSerifBold" would normalize to { "SansSerifBold", "Medium", BOLD }
LLFontDescriptor LLFontDescriptor::normalize() const
{
    std::string new_name(mName);
    std::string new_size(mSize);
    U8 new_style(mStyle);

    // Only care about style to extent it can be picked up by font.
    new_style &= (LLFontGL::BOLD | LLFontGL::ITALIC);

    // All these transformations are to support old-style font specifications.
    if (removeSubString(new_name,"Small"))
        new_size = "Small";
    if (removeSubString(new_name,"Big"))
        new_size = "Large";
    if (removeSubString(new_name,"Medium"))
        new_size = "Medium";
    if (removeSubString(new_name,"Large"))
        new_size = "Large";
    if (removeSubString(new_name,"Huge"))
        new_size = "Huge";

    // HACK - Monospace is the only one we don't remove, so
    // name "Monospace" doesn't get taken down to ""
    // For other fonts, there's no ambiguity between font name and size specifier.
    if (new_size != s_template_string && new_size.empty() && findSubString(new_name,"Monospace"))
        new_size = "Monospace";
    if (new_size.empty())
        new_size = "Small";

    if (removeSubString(new_name,"Bold"))
        new_style |= LLFontGL::BOLD;

    if (removeSubString(new_name,"Italic"))
        new_style |= LLFontGL::ITALIC;

    return LLFontDescriptor(new_name,new_size,new_style, getFontFiles());
}

void LLFontDescriptor::addFontFile(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, const std::function<bool(llwchar)>& char_functor, bool monospace_ligatures, bool load_collection, const ALFontVarAxes& var_axes)
{
    mFontFiles.push_back(LLFontFileInfo(file_name, hinting, flags, size_delta, char_functor, monospace_ligatures, load_collection, var_axes));
}

LLFontRegistry::LLFontRegistry(bool create_gl_textures)
:   mCreateGLTextures(create_gl_textures)
{
    // This is potentially a slow directory traversal, so we want to
    // cache the result.
    mUltimateFallbackList = LLWindow::getDynamicFallbackFontList();
}

LLFontRegistry::~LLFontRegistry()
{
    clear();
}

// Parse one fonts.xml file into the registry's mFontMap. Returns true if
// init_from_xml succeeded; false on parse failure or wrong root tag.
// Cross-family resolution is deliberately deferred to the caller so a
// multi-file load (skin layering) only resolves once after every layer
// has been merged.
static bool parse_single_fonts_file(LLFontRegistry* reg, const std::string& path)
{
    LLXMLNodePtr root;
    if (!LLXMLNode::parseFile(path, root, NULL))
        return false;

    if (root.isNull() || !root->hasName("fonts"))
    {
        LL_WARNS() << "Bad font info file: " << path << LL_ENDL;
        return false;
    }

    return init_from_xml(reg, root);
}

bool LLFontRegistry::parseFontInfo(const std::string& xml_filename, const LLSD& font_overrides)
{
    bool success = false;  // Succeed if we find and read at least one XUI file
    const string_vec_t xml_paths = gDirUtilp->findSkinnedFilenames(LLDir::XUI, xml_filename);
    if (xml_paths.empty())
    {
        // We didn't even find one single XUI file
        return false;
    }

    for (const auto& path : xml_paths)
    {
        success = parse_single_fonts_file(this, path) || success;
    }

    // Resolve cross-family <use> and per-family inherit="true" once across
    // the merged registry, after all skinned fonts.xml files have been read.
    // Doing this per-file would double-append references for entries that
    // appear in multiple skin layers.
    resolveFontReferences(font_overrides);

    //if (success)
    //  dump();

    return success;
}

bool LLFontRegistry::parseFontInfoFromFile(const std::string& xml_path, const LLSD& font_overrides)
{
    const bool success = parse_single_fonts_file(this, xml_path);
    resolveFontReferences(font_overrides);
    return success;
}

void LLFontRegistry::resolveFontReferences(const LLSD& font_overrides)
{
    // Helper: replace the file list on a (family, style) entry while
    // preserving any cached LLFontGL pointer.
    auto replace_files = [this](font_reg_map_t::iterator it,
                                const font_file_info_vec_t& new_files)
    {
        LLFontDescriptor new_desc = it->first;
        new_desc.setFontFiles(new_files);
        LLFontGL* fontp = it->second;
        mFontMap.erase(it);
        mFontMap[new_desc] = fontp;
    };

    // Step 1: resolve cross-family <use> references. For each (family, style)
    // entry that names another family, append the referenced family's
    // matching-style files (or NORMAL if the style isn't defined). Single-
    // level only — referenced families' own <use> chains are not followed.
    static const U8 kStyles[4] = {
        0,
        LLFontGL::BOLD,
        LLFontGL::ITALIC,
        static_cast<U8>(LLFontGL::BOLD | LLFontGL::ITALIC)
    };

    // Helper: depth-first walk of mFamilyUses checking whether `fam` or any
    // family transitively reachable through its <use> chain explicitly
    // declares `style`. NORMAL fallback is NOT consulted here — synthesis
    // only fires for styles authored somewhere in the chain. A use-only
    // family whose chain only ever declares NORMAL therefore gets ONLY a
    // NORMAL host; lookups at BOLD/ITALIC fall through getClosestFontTemplate
    // (which already accepts NORMAL as a closest match for any style
    // request) rather than minting synthetic BOLD/ITALIC hosts that just
    // alias NORMAL's chain.
    std::function<bool(const std::string&, U8, std::set<std::string>&)> chain_has_style;
    chain_has_style = [&](const std::string& fam, U8 style, std::set<std::string>& visited) -> bool {
        if (!visited.insert(fam).second)
            return false;
        if (mFontMap.find(LLFontDescriptor(fam, s_template_string, style)) != mFontMap.end())
            return true;
        auto uses_it = mFamilyUses.find(fam);
        if (uses_it == mFamilyUses.end())
            return false;
        for (const auto& used_ref : uses_it->second)
        {
            if (chain_has_style(used_ref.family, style, visited))
                return true;
        }
        return false;
    };

    // Pre-step: synthesize style descriptors for <use>-only families based
    // on which styles their referenced families (transitively) explicitly
    // declare. A family that declared no <style> blocks gets a host
    // descriptor for exactly those styles authored somewhere in the chain.
    // Styles not authored anywhere in the chain are left unsynthesized;
    // getClosestFontTemplate's bit-mask fallback at lookup time substitutes
    // NORMAL (or any subset style) for the missing-style request.
    for (const auto& kv : mFamilyUses)
    {
        const std::string& family = kv.first;

        bool has_any_style = false;
        for (U8 style : kStyles)
        {
            if (mFontMap.find(LLFontDescriptor(family, s_template_string, style)) != mFontMap.end())
            {
                has_any_style = true;
                break;
            }
        }
        if (has_any_style)
            continue;

        for (U8 style : kStyles)
        {
            bool any_ref_has_style = false;
            for (const auto& used_ref : kv.second)
            {
                const std::string& used = used_ref.family;
                if (used == family)
                    continue; // self-reference is rejected during resolution
                std::set<std::string> visited;
                visited.insert(family);
                if (chain_has_style(used, style, visited))
                {
                    any_ref_has_style = true;
                    break;
                }
            }
            if (any_ref_has_style)
            {
                LLFontDescriptor desc;
                desc.setName(family);
                desc.setStyle(style);
                desc.setSize(s_template_string);
                mergeFontEntry(desc);
            }
        }
    }

    // Apply user-supplied family overrides BEFORE chain resolution so that
    // every family that <use>s an overridden base picks up the override
    // through its chain. The earlier ordering (apply at the end) prepended
    // override files only to the named target; dependents like SansSerif
    // and SansSerifLimitedEmoji had already baked in the pre-override base
    // files via collect_chain and were silently left untouched. Override
    // sources at this point are limited to families with direct <file>
    // declarations (the picker filters via getAvailableFamilies whose
    // user_selectable filter excludes <use>-only composites), so reading
    // the source's direct file list before its own <use> chain is resolved
    // is sound.
    applyFamilyOverrides(font_overrides);

    // Snapshot pre-resolution (post-override) file lists. <use> targets are
    // looked up in this snapshot rather than the live mFontMap, so
    // resolution is order-independent and cycles / self-references behave
    // deterministically:
    //   A->B and B->A both end up as [own, other], not double-counted;
    //   <use family="self"/> still warns and is skipped below regardless.
    boost::unordered_map<LLFontDescriptor, font_file_info_vec_t> pre_use;
    pre_use.reserve(mFontMap.size());
    for (const auto& entry : mFontMap)
    {
        pre_use.emplace(entry.first, entry.first.getFontFiles());
    }

    // Helper: depth-first walk of `fam`'s <use> chain, appending each
    // reached family's pre-resolution files to `out`. The shared `visited`
    // set prevents cycles (A->B->A terminates cleanly with each appearing
    // once) and prevents diamond-shaped graphs from including the same
    // family twice (A->B, A->C, both B and C use D — D's files appear
    // once, contributed by whichever path the walk reached first).
    // Style is honoured per family with NORMAL fallback at each level.
    //
    // `filter` is the optional CharFunctor from the <use> at the top of
    // this walk (null when the use has no unicode_ranges). It composes
    // (intersects) with each contributed file's own CharFunctor for the
    // FILES THIS FAMILY DIRECTLY CONTRIBUTES — recursive descent into
    // `fam`'s own <use> chain DROPS the filter, so the use-level range
    // gates only the source family's own files, not its transitive
    // dependencies. (e.g. `<use family="EmojiBase" unicode_ranges="..."/>`
    // filters EmojiBase's files; if EmojiBase itself <use>d another
    // family, that other family's files come through unfiltered.)
    std::function<void(const std::string&, U8, std::set<std::string>&, font_file_info_vec_t&, const std::function<bool(llwchar)>&)> collect_chain;
    collect_chain = [&](const std::string& fam, U8 style, std::set<std::string>& visited, font_file_info_vec_t& out, const std::function<bool(llwchar)>& filter) {
        if (!visited.insert(fam).second)
            return;
        LLFontDescriptor key(fam, s_template_string, style);
        auto snap_it = pre_use.find(key);
        if (snap_it == pre_use.end() && style != 0)
        {
            // Fall back to NORMAL of the referenced family.
            key = LLFontDescriptor(fam, s_template_string, 0);
            snap_it = pre_use.find(key);
        }
        if (snap_it != pre_use.end())
        {
            const auto& src_files = snap_it->second;
            if (!filter)
            {
                out.insert(out.end(), src_files.begin(), src_files.end());
            }
            else
            {
                // Compose the use-level filter with each file's existing
                // CharFunctor (intersection). Files keep their own gate;
                // the use's gate restricts further. A file with no functor
                // adopts the use's functor outright.
                for (const auto& file : src_files)
                {
                    LLFontFileInfo copy = file;
                    if (copy.CharFunctor)
                    {
                        auto a = copy.CharFunctor;
                        auto b = filter;
                        copy.CharFunctor = [a, b](llwchar c) { return a(c) && b(c); };
                    }
                    else
                    {
                        copy.CharFunctor = filter;
                    }
                    out.push_back(std::move(copy));
                }
            }
        }
        // Recurse into fam's own <use> chain even when its file list at this
        // style is missing — the chain may carry the coverage on its own.
        // The use-level filter is intentionally NOT propagated through here.
        auto uses_it = mFamilyUses.find(fam);
        if (uses_it == mFamilyUses.end())
            return;
        for (const auto& used_ref : uses_it->second)
        {
            collect_chain(used_ref.family, style, visited, out, used_ref.functor);
        }
    };

    for (const auto& kv : mFamilyUses)
    {
        const std::string& family = kv.first;
        const std::vector<FamilyUseRef>& used_families = kv.second;

        for (U8 style : kStyles)
        {
            LLFontDescriptor key(family, s_template_string, style);
            auto it = mFontMap.find(key);
            if (it == mFontMap.end())
                continue;

            font_file_info_vec_t merged_files = it->first.getFontFiles();
            std::set<std::string> visited;
            visited.insert(family); // the chain walks below must not loop back to us

            for (const auto& used_ref : used_families)
            {
                const std::string& used = used_ref.family;
                if (used == family)
                {
                    LL_WARNS() << "fonts.xml: <use family='" << used
                               << "'/> in '" << family
                               << "' is self-referential; skipping" << LL_ENDL;
                    continue;
                }
                // Existence check: warn once at the top level if a direct
                // reference is undeclared. Transitive misses (a chain
                // hitting a missing family) get caught by the same check
                // when that intermediate family is processed in its own
                // outer-loop iteration.
                if (pre_use.find(LLFontDescriptor(used, s_template_string, 0)) == pre_use.end()
                    && pre_use.find(LLFontDescriptor(used, s_template_string, LLFontGL::BOLD)) == pre_use.end()
                    && pre_use.find(LLFontDescriptor(used, s_template_string, LLFontGL::ITALIC)) == pre_use.end()
                    && pre_use.find(LLFontDescriptor(used, s_template_string, (U8)(LLFontGL::BOLD | LLFontGL::ITALIC))) == pre_use.end())
                {
                    LL_WARNS() << "fonts.xml: <use family='" << used
                               << "'/> in '" << family
                               << "' but no such family is defined" << LL_ENDL;
                    continue;
                }
                collect_chain(used, style, visited, merged_files, used_ref.functor);
            }

            replace_files(it, merged_files);
        }
    }

    // Step 2: resolve inherit="true". Done after <use> so a BOLD style that
    // inherits NORMAL picks up NORMAL's post-<use> file list.
    for (const auto& kv : mInheritFlags)
    {
        if (!kv.second)
            continue;
        const std::string& family = kv.first.first;
        U8 style = kv.first.second;
        if (style == 0)
            continue; // NORMAL has nothing to inherit from.

        LLFontDescriptor variant_key(family, s_template_string, style);
        auto variant_it = mFontMap.find(variant_key);
        if (variant_it == mFontMap.end())
        {
            LL_WARNS() << "fonts.xml: inherit='true' on " << family << " style "
                       << (S32)style << " but no matching <font>/<style> entry exists" << LL_ENDL;
            continue;
        }

        LLFontDescriptor parent_key(family, s_template_string, 0);
        auto parent_it = mFontMap.find(parent_key);
        if (parent_it == mFontMap.end())
        {
            LL_WARNS() << "fonts.xml: inherit='true' on " << family << " style "
                       << (S32)style << " but no NORMAL-style parent exists" << LL_ENDL;
            continue;
        }

        font_file_info_vec_t merged_files = variant_it->first.getFontFiles();
        const font_file_info_vec_t& parent_files = parent_it->first.getFontFiles();
        merged_files.insert(merged_files.end(), parent_files.begin(), parent_files.end());

        replace_files(variant_it, merged_files);
    }

    // Step 4: deduplicate file lists on every template. Two paths feed
    // dupes in: (a) an override prepend whose source is also reachable
    // through the family's <use> chain (e.g. override SansSerifBase to
    // DejaVu, then SansSerif <use family="DejaVu"/> appends DejaVu
    // again), and (b) a diamond <use> graph where two <use>s
    // transitively reach the same primitive. The first hit wins —
    // the renderer treats chain[0] as the head face and walks tail
    // entries in order for fallback coverage, so an earlier copy
    // already covers everything a later copy would.
    //
    // Equality here ignores CharFunctor when both are null (the common
    // case — functored entries come from <file unicode_ranges='...'>
    // and aren't typically prepended by overrides). Two functored
    // entries with the same filename are treated as distinct because
    // std::function targets can't be reliably compared and we'd rather
    // err on the side of keeping a possibly-different gate than
    // collapsing two genuinely-different functors into one.
    auto file_info_equivalent = [](const LLFontFileInfo& a, const LLFontFileInfo& b) {
        if (a.FileName != b.FileName) return false;
        if (a.mHinting != b.mHinting) return false;
        if (a.mFlags != b.mFlags) return false;
        if (!(a.mVarAxes == b.mVarAxes)) return false;
        if (a.mSizeDelta != b.mSizeDelta) return false;
        if (a.mMonospaceLigatures != b.mMonospaceLigatures) return false;
        if (a.mLoadCollection != b.mLoadCollection) return false;
        // CharFunctor: null+null == equivalent; anything else differs.
        if (static_cast<bool>(a.CharFunctor) != static_cast<bool>(b.CharFunctor)) return false;
        if (a.CharFunctor || b.CharFunctor) return false;
        return true;
    };
    // Two-phase: collect every (old descriptor, deduped files) update
    // first, then apply via replace_files. replace_files erases the
    // iterator it's passed, so doing it inline would invalidate the
    // outer iteration.
    std::vector<std::pair<LLFontDescriptor, font_file_info_vec_t>> dedup_updates;
    for (const auto& entry : mFontMap)
    {
        if (!entry.first.isTemplate())
            continue;
        const auto& orig = entry.first.getFontFiles();
        if (orig.size() < 2)
            continue;
        font_file_info_vec_t deduped;
        deduped.reserve(orig.size());
        for (const auto& f : orig)
        {
            bool already_present = false;
            for (const auto& kept : deduped)
            {
                if (file_info_equivalent(f, kept))
                {
                    already_present = true;
                    break;
                }
            }
            if (!already_present)
                deduped.push_back(f);
        }
        if (deduped.size() != orig.size())
            dedup_updates.emplace_back(entry.first, std::move(deduped));
    }
    for (auto& [old_desc, new_files] : dedup_updates)
    {
        auto it = mFontMap.find(old_desc);
        if (it != mFontMap.end())
            replace_files(it, new_files);
    }

    // Cache the just-applied overrides so a subsequent reload at the same
    // (fonts.xml, overrides) but new DPI can detect the no-content-change
    // case via overridesEqual and take the fast path.
    mLastFontOverrides = font_overrides;

    // Consume both maps so a subsequent call (e.g. on dynamic skin reload)
    // doesn't double-append references to already-resolved entries.
    // Per-family sizes (mFamilySizes) and per-file source-family tags
    // (mSourceFamily on each LLFontFileInfo) survive — those are the
    // runtime data createFont consults at load time.
    mFamilyUses.clear();
    mInheritFlags.clear();
}

void LLFontRegistry::applyFamilyOverrides(const LLSD& overrides)
{
    if (!overrides.isMap() || overrides.size() == 0)
        return;

    static const U8 kStyles[4] = {
        0,
        LLFontGL::BOLD,
        LLFontGL::ITALIC,
        static_cast<U8>(LLFontGL::BOLD | LLFontGL::ITALIC)
    };

    // Snapshot current template file lists. Overrides look up source-family
    // files in this snapshot so chains of overrides (A->B and B->C in the
    // same map) don't see each other's mid-iteration mutations — each
    // override resolves against the original post-resolution state.
    boost::unordered_map<LLFontDescriptor, font_file_info_vec_t> pre_override;
    pre_override.reserve(mFontMap.size());
    for (const auto& entry : mFontMap)
    {
        if (!entry.first.isTemplate())
            continue;
        pre_override.emplace(entry.first, entry.first.getFontFiles());
    }

    auto replace_files = [this](font_reg_map_t::iterator it,
                                const font_file_info_vec_t& new_files)
    {
        LLFontDescriptor new_desc = it->first;
        new_desc.setFontFiles(new_files);
        LLFontGL* fontp = it->second;
        mFontMap.erase(it);
        mFontMap[new_desc] = fontp;
    };

    // An override entry is either a flat string (legacy / user-facing
    // picker shape) or a map { value, replace_first } (programmatic
    // shape used when the override should drop the target's head face
    // instead of riding in front of it). Map shape is non-user-facing —
    // the Preferences picker keeps writing strings; callers that want
    // head-replace semantics build the map themselves before handing it
    // to the registry. replace_first defaults to false so an unset or
    // missing flag preserves the legacy prepend behavior.
    auto extract_override = [](const LLSD& v, std::string& out_value, bool& out_replace_first)
    {
        out_replace_first = false;
        if (v.isMap())
        {
            out_value = v.has("value") ? v["value"].asString() : std::string();
            if (v.has("replace_first"))
                out_replace_first = v["replace_first"].asBoolean();
        }
        else
        {
            out_value = v.asString();
        }
    };

    for (LLSD::map_const_iterator ov_it = overrides.beginMap();
         ov_it != overrides.endMap();
         ++ov_it)
    {
        const std::string& family = ov_it->first;
        std::string override_value;
        bool replace_first = false;
        extract_override(ov_it->second, override_value, replace_first);
        if (override_value.empty())
            continue;

        if (override_value == family)
        {
            LL_WARNS() << "AlchemyUIFontOverrides: '" << family
                       << "' overrides to itself; skipping" << LL_ENDL;
            continue;
        }

        // Verify the target family is actually declared. If not, skip with
        // a warning rather than silently creating phantom entries.
        bool target_exists = false;
        for (U8 style : kStyles)
        {
            if (mFontMap.find(LLFontDescriptor(family, s_template_string, style)) != mFontMap.end())
            {
                target_exists = true;
                break;
            }
        }
        if (!target_exists)
        {
            LL_WARNS() << "AlchemyUIFontOverrides: target family '" << family
                       << "' is not declared in fonts.xml; skipping" << LL_ENDL;
            continue;
        }

        // Try the override as a known family name first. If absent across
        // all styles in the snapshot, fall back to treating it as a font
        // file name. (File lookup happens later in createFont via the
        // font_search_paths walk; we only build the LLFontFileInfo here.)
        bool source_is_family = false;
        for (U8 style : kStyles)
        {
            if (pre_override.find(LLFontDescriptor(override_value, s_template_string, style)) != pre_override.end())
            {
                source_is_family = true;
                break;
            }
        }

        // No-op detection: when `family` is a <use>-only composite (no
        // direct <file> at any style) AND its <use> chain transitively
        // reaches `override_value`, the user's override matches what the
        // chain already pulls. Prepending the source's files anyway would
        // produce a duplicate that Step-4 dedup can't merge once a
        // consumer's <use> attaches a use-level filter to the
        // override-prepended copy but not to the recursion-contributed
        // copy. The asymmetric pair (filtered head + unfiltered tail with
        // the same file) shifts shape-itemizer priority — observable as
        // emoji rendering at different metrics through the codepoint vs
        // shape paths. Production repro: AlchemyUIFontOverrides
        // ["EmojiBase"] = "NotoEmoji" while EmojiBase already <use>s
        // NotoEmoji.
        if (source_is_family)
        {
            bool target_is_use_only = true;
            for (U8 style : kStyles)
            {
                auto pre_it = pre_override.find(
                    LLFontDescriptor(family, s_template_string, style));
                if (pre_it != pre_override.end() && !pre_it->second.empty())
                {
                    target_is_use_only = false;
                    break;
                }
            }
            if (target_is_use_only)
            {
                // Walk mFamilyUses transitively (visited set guards cycles
                // and diamond <use> graphs).
                std::set<std::string> visited;
                std::function<bool(const std::string&)> reaches;
                reaches = [&](const std::string& fam) -> bool
                {
                    if (!visited.insert(fam).second)
                        return false;
                    auto uses_it = mFamilyUses.find(fam);
                    if (uses_it == mFamilyUses.end())
                        return false;
                    for (const auto& used_ref : uses_it->second)
                    {
                        if (used_ref.family == override_value)
                            return true;
                        if (reaches(used_ref.family))
                            return true;
                    }
                    return false;
                };
                if (reaches(family))
                {
                    LL_INFOS() << "AlchemyUIFontOverrides: '" << family
                               << "' is <use>-only and already reaches source '"
                               << override_value
                               << "' via its <use> chain; override is a no-op, skipping"
                               << LL_ENDL;
                    continue;
                }
            }
        }

        for (U8 style : kStyles)
        {
            LLFontDescriptor target_key(family, s_template_string, style);
            font_reg_map_t::iterator target_it = mFontMap.find(target_key);
            if (target_it == mFontMap.end())
                continue;

            font_file_info_vec_t prepend_files;

            if (source_is_family)
            {
                LLFontDescriptor source_key(override_value, s_template_string, style);
                auto src_it = pre_override.find(source_key);
                if (src_it == pre_override.end() && style != 0)
                {
                    // Fall back to NORMAL style of the source family.
                    source_key = LLFontDescriptor(override_value, s_template_string, 0);
                    src_it = pre_override.find(source_key);
                }
                if (src_it == pre_override.end())
                    continue;
                prepend_files = src_it->second;
            }
            else
            {
                // File-name override. Inherit hinting/var_axes/flags/
                // ligatures defaults from the target family's first
                // existing file so the swap behaves like a drop-in
                // replacement (e.g. a monospace family's ligatures
                // setting carries to the user-supplied file).
                EFontHinting hinting = EFontHinting::FORCE_AUTOHINT;
                ALFontVarAxes var_axes;
                S32 flags = 0;
                bool monospace_lig = false;
                const auto& orig_files = target_it->first.getFontFiles();
                if (!orig_files.empty())
                {
                    hinting = orig_files[0].mHinting;
                    var_axes = orig_files[0].mVarAxes;
                    flags = orig_files[0].mFlags;
                    monospace_lig = orig_files[0].mMonospaceLigatures;
                }
                if (style & LLFontGL::BOLD)
                    flags |= LLFontGL::BOLD;
                prepend_files.emplace_back(override_value, hinting, flags, 0.f, std::function<bool(llwchar)>(nullptr), monospace_lig, false, var_axes);
            }

            // Default: prepend source onto the target's existing chain so
            // the override wins for the head face but DejaVu/Emoji/CJK
            // fallbacks (already resolved onto target via <use>) stay
            // available behind it.
            //
            // replace_first: drop the target's head file (orig_files[0])
            // so the override displaces it rather than sitting in front
            // of it. The rest of the chain (orig_files[1..]) is still
            // appended so glyph coverage from the inherited fallbacks
            // remains. No-op when the chain is empty.
            font_file_info_vec_t merged_files = prepend_files;
            const auto& orig_files = target_it->first.getFontFiles();
            auto tail_begin = (replace_first && !orig_files.empty())
                                  ? orig_files.begin() + 1
                                  : orig_files.begin();
            merged_files.insert(merged_files.end(), tail_begin, orig_files.end());

            replace_files(target_it, merged_files);
        }
    }
}

std::string currentOsName()
{
#if LL_WINDOWS
    return "Windows";
#elif LL_DARWIN
    return "Mac";
#elif LL_LINUX
    return "Linux";
#else
    return "";
#endif
}

namespace
{
// Parse the "font_hinting" attribute on a <file> or <font> node into an
// EFontHinting. Returns true if the attribute was present and recognized.
bool parseHintingAttr(LLXMLNodePtr node, EFontHinting& out)
{
    if (!node->hasAttribute("font_hinting"))
        return false;
    std::string s;
    node->getAttributeString("font_hinting", s);
    LLStringUtil::toLower(s);
    if (s == "default") { out = EFontHinting::DEFAULT; return true; }
    if (s == "force_auto") { out = EFontHinting::FORCE_AUTOHINT; return true; }
    if (s == "no_hinting") { out = EFontHinting::NO_HINTING; return true; }
    if (s == "light") { out = EFontHinting::LIGHT; return true; }
    return false;
}

bool font_desc_init_from_xml(LLXMLNodePtr node,
                             LLFontDescriptor& desc,
                             FamilyDefaults defaults,
                             FontParseExtras* extras)
{
    // Read family-level state from the top <font> element only. Recursive
    // calls (for <os>) inherit the caller's `defaults` unchanged so that
    // platform-specific files pick up the same family-level fallbacks.
    if (node->hasName("font"))
    {
        std::string attr_name;
        if (node->getAttributeString("name",attr_name))
        {
            desc.setName(attr_name);
        }

        std::string attr_style;
        if (node->getAttributeString("font_style",attr_style))
        {
            desc.setStyle(LLFontGL::getStyleFromString(attr_style));
        }

        if (node->hasAttribute("ligatures"))
        {
            std::string attr_ligatures;
            node->getAttributeString("ligatures", attr_ligatures);
            LLStringUtil::toLower(attr_ligatures);
            defaults.monospace_ligatures = (attr_ligatures == "on"
                                         || attr_ligatures == "true"
                                         || attr_ligatures == "1");
        }

        EFontHinting fam_hinting = EFontHinting::FORCE_AUTOHINT;
        if (parseHintingAttr(node, fam_hinting))
        {
            defaults.hinting = fam_hinting;
            defaults.hinting_set = true;
        }

        // Family-level OpenType variation axes — all five share one
        // shape. Each one cascades to child <file> entries that don't
        // override. Values are F32 design coords; setVariationAxis at
        // face-load time clamps to the axis's actual [min, max].
        // font_weight is parsed as S32 (CSS-weight idiom) but stored
        // as F32 alongside the others.
        if (node->hasAttribute("font_weight"))
        {
            S32 v = -1;
            node->getAttributeS32("font_weight", v);
            defaults.var_axes.wght = static_cast<F32>(v);
            // -1 is the legacy "use file default" sentinel — leaving
            // wght_set false skips setVariationAxis("wght", ...) at
            // face load. Matches read_family_defaults and the file-level
            // pass; the three sites must agree or the cascade leaks
            // a bogus -1 into setVariationAxis.
            defaults.var_axes.wght_set = (v >= 0);
        }
        if (node->hasAttribute("font_optical_size"))
        {
            F32 v = 0.f;
            node->getAttributeF32("font_optical_size", v);
            defaults.var_axes.opsz = v;
            defaults.var_axes.opsz_set = true;
        }
        if (node->hasAttribute("font_italic"))
        {
            F32 v = 0.f;
            node->getAttributeF32("font_italic", v);
            defaults.var_axes.ital = v;
            defaults.var_axes.ital_set = true;
        }
        if (node->hasAttribute("font_width"))
        {
            F32 v = 0.f;
            node->getAttributeF32("font_width", v);
            defaults.var_axes.wdth = v;
            defaults.var_axes.wdth_set = true;
        }
        if (node->hasAttribute("font_slant"))
        {
            F32 v = 0.f;
            node->getAttributeF32("font_slant", v);
            defaults.var_axes.slnt = v;
            defaults.var_axes.slnt_set = true;
        }

        if (node->hasAttribute("load_collection"))
        {
            bool fam_col = false;
            node->getAttributeBOOL("load_collection", fam_col);
            defaults.load_collection = fam_col;
            defaults.load_collection_set = true;
        }

        if (extras && node->hasAttribute("inherit"))
        {
            bool inh = false;
            node->getAttributeBOOL("inherit", inh);
            extras->inherit = inh;
        }

        desc.setSize(s_template_string);
    }

    LLXMLNodePtr child;
    for (child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
    {
        std::string child_name;
        child->getAttributeString("name",child_name);
        // <style> and <use> are new-format-only and handled at the
        // process_new_format_font level. Skip them here so they don't get
        // accidentally interpreted as <file> when font_desc_init_from_xml is
        // called on a <style> node directly.
        if (child->hasName("style") || child->hasName("use"))
        {
            continue;
        }
        if (child->hasName("file"))
        {
            std::string font_file_name = child->getTextContents();
            std::function<bool(llwchar)> inline_functor;
            EFontHinting hinting = defaults.hinting_set ? defaults.hinting : EFontHinting::FORCE_AUTOHINT;
            S32 flags = 0;
            bool load_collection = defaults.load_collection_set ? defaults.load_collection : false;

            if (child->hasAttribute("unicode_ranges"))
            {
                std::string spec;
                child->getAttributeString("unicode_ranges", spec);
                inline_functor = parseUnicodeRanges(spec);
            }

            EFontHinting file_hinting;
            if (parseHintingAttr(child, file_hinting))
            {
                hinting = file_hinting;
            }

            if (child->hasAttribute("flags"))
            {
                std::string attr_flags;
                child->getAttributeString("flags", attr_flags);
                LLStringUtil::toLower(attr_flags);

                if (attr_flags == "bold")
                {
                    flags |= LLFontGL::BOLD;
                }
            }

            F32 size_delta = 0.f;
            if (child->hasAttribute("size_delta"))
            {
                child->getAttributeF32("size_delta", size_delta);
            }

            if (child->hasAttribute("load_collection"))
            {
                child->getAttributeBOOL("load_collection", load_collection);
            }

            // OpenType variation axes — start from the family default
            // and allow per-file override. All five axes share the same
            // shape (cascade + override). font_weight is parsed as S32
            // (CSS-weight idiom) and stored as F32; the rest are F32.
            ALFontVarAxes var_axes = defaults.var_axes;
            if (child->hasAttribute("font_weight"))
            {
                S32 v = -1;
                child->getAttributeS32("font_weight", v);
                var_axes.wght = static_cast<F32>(v);
                var_axes.wght_set = (v >= 0);
            }
            if (child->hasAttribute("font_optical_size"))
            {
                F32 v = 0.f;
                child->getAttributeF32("font_optical_size", v);
                var_axes.opsz = v; var_axes.opsz_set = true;
            }
            if (child->hasAttribute("font_italic"))
            {
                F32 v = 0.f;
                child->getAttributeF32("font_italic", v);
                var_axes.ital = v; var_axes.ital_set = true;
            }
            if (child->hasAttribute("font_width"))
            {
                F32 v = 0.f;
                child->getAttributeF32("font_width", v);
                var_axes.wdth = v; var_axes.wdth_set = true;
            }
            if (child->hasAttribute("font_slant"))
            {
                F32 v = 0.f;
                child->getAttributeF32("font_slant", v);
                var_axes.slnt = v; var_axes.slnt_set = true;
            }

            desc.addFontFile(font_file_name, hinting, flags, size_delta, inline_functor, defaults.monospace_ligatures, load_collection, var_axes);
        }
        else if (child->hasName("size"))
        {
            // Per-family size pin. Ignored on <os> recursion (extras is
            // null there) since OS blocks shouldn't redefine sizes.
            if (extras)
            {
                std::string size_name;
                F32 size_value;
                if (child->getAttributeString("name", size_name) &&
                    child->getAttributeF32("size", size_value))
                {
                    extras->family_sizes[size_name] = size_value;
                }
                else
                {
                    LL_WARNS() << "fonts.xml: <size> inside <font name='" << desc.getName()
                               << "'> needs both name and size attributes" << LL_ENDL;
                }
            }
        }
        else if (child->hasName("os"))
        {
            if (child_name == currentOsName())
            {
                font_desc_init_from_xml(child, desc, defaults, nullptr);
            }
        }
    }
    return true;
}
// Read family-level defaults (ligatures, font_hinting, font_weight,
// load_collection) from a <font> node. Used by both legacy and new-format
// paths; mirrors the family-level reads done inside font_desc_init_from_xml
// for backward compatibility.
FamilyDefaults read_family_defaults(LLXMLNodePtr font_node)
{
    FamilyDefaults d;
    if (font_node->hasAttribute("ligatures"))
    {
        std::string s;
        font_node->getAttributeString("ligatures", s);
        LLStringUtil::toLower(s);
        d.monospace_ligatures = (s == "on" || s == "true" || s == "1");
    }
    EFontHinting h;
    if (parseHintingAttr(font_node, h))
    {
        d.hinting = h;
        d.hinting_set = true;
    }
    // OpenType variation axes — all five share one shape.
    // Cascades to child <file> entries via font_desc_init_from_xml.
    if (font_node->hasAttribute("font_weight"))
    {
        S32 v = -1;
        font_node->getAttributeS32("font_weight", v);
        d.var_axes.wght = static_cast<F32>(v);
        d.var_axes.wght_set = (v >= 0);
    }
    if (font_node->hasAttribute("font_optical_size"))
    {
        F32 v = 0.f;
        font_node->getAttributeF32("font_optical_size", v);
        d.var_axes.opsz = v;
        d.var_axes.opsz_set = true;
    }
    if (font_node->hasAttribute("font_italic"))
    {
        F32 v = 0.f;
        font_node->getAttributeF32("font_italic", v);
        d.var_axes.ital = v;
        d.var_axes.ital_set = true;
    }
    if (font_node->hasAttribute("font_width"))
    {
        F32 v = 0.f;
        font_node->getAttributeF32("font_width", v);
        d.var_axes.wdth = v;
        d.var_axes.wdth_set = true;
    }
    if (font_node->hasAttribute("font_slant"))
    {
        F32 v = 0.f;
        font_node->getAttributeF32("font_slant", v);
        d.var_axes.slnt = v;
        d.var_axes.slnt_set = true;
    }
    if (font_node->hasAttribute("load_collection"))
    {
        bool c = false;
        font_node->getAttributeBOOL("load_collection", c);
        d.load_collection = c;
        d.load_collection_set = true;
    }
    return d;
}

} // anonymous namespace

void LLFontRegistry::mergeFontEntry(const LLFontDescriptor& desc)
{
    const LLFontDescriptor* match = getMatchingFontDesc(desc);
    if (!match)
    {
        mFontMap[desc] = nullptr;
        return;
    }
    font_file_info_vec_t files = match->getFontFiles();
    files.insert(files.begin(), desc.getFontFiles().begin(), desc.getFontFiles().end());

    LLFontDescriptor merged = *match;
    merged.setFontFiles(files);
    mFontMap.erase(*match);
    mFontMap[merged] = nullptr;
}

void LLFontRegistry::processNewFormatFont(LLPointer<LLXMLNode> font_node)
{
    std::string family_name;
    if (!font_node->getAttributeString("name", family_name))
    {
        LL_WARNS() << "fonts.xml: <font> missing required name attribute" << LL_ENDL;
        return;
    }

    FamilyDefaults defaults = read_family_defaults(font_node);

    // Family-level metadata: ui_label and user_selectable. Cross-skin
    // layering merges fields individually so a later layer can override
    // just the label without disturbing user_selectable, and vice versa.
    {
        FamilyMeta& meta = mFamilyMeta[family_name];
        if (font_node->hasAttribute("label"))
        {
            std::string ui_label;
            font_node->getAttributeString("label", ui_label);
            meta.ui_label = ui_label;
        }
        if (font_node->hasAttribute("user_selectable"))
        {
            bool sel = true;
            font_node->getAttributeBOOL("user_selectable", sel);
            meta.user_selectable = sel;
        }
        if (font_node->hasAttribute("monospace"))
        {
            bool mono = false;
            font_node->getAttributeBOOL("monospace", mono);
            meta.monospace = mono;
        }
        if (font_node->hasAttribute("emoji"))
        {
            bool em = false;
            font_node->getAttributeBOOL("emoji", em);
            meta.emoji = em;
        }
    }

    // First sweep: collect family-level <size>, <use>; defer <style> to
    // a second sweep so we can warn cleanly on stray children.
    family_size_overrides_t family_sizes;
    std::vector<LLFontRegistry::FamilyUseRef> uses;
    for (LLXMLNodePtr c = font_node->getFirstChild(); c.notNull(); c = c->getNextSibling())
    {
        if (c->hasName("size"))
        {
            std::string size_name;
            F32 size_value;
            if (c->getAttributeString("name", size_name) &&
                c->getAttributeF32("size", size_value))
            {
                family_sizes[size_name] = size_value;
            }
            else
            {
                LL_WARNS() << "fonts.xml: <size> in '" << family_name
                           << "' needs both name and size attributes" << LL_ENDL;
            }
        }
        else if (c->hasName("use"))
        {
            std::string used;
            if (c->getAttributeString("family", used))
            {
                LLFontRegistry::FamilyUseRef ref;
                ref.family = used;
                // Optional use-level glyph filter. Composes with each
                // contributed file's CharFunctor at chain-resolve time
                // so a single source family can be `<use>`-d by multiple
                // consumers each applying their own range gate.
                if (c->hasAttribute("unicode_ranges"))
                {
                    std::string spec;
                    c->getAttributeString("unicode_ranges", spec);
                    ref.functor = parseUnicodeRanges(spec);
                }
                uses.push_back(std::move(ref));
            }
            else
            {
                LL_WARNS() << "fonts.xml: <use> in '" << family_name
                           << "' missing family attribute" << LL_ENDL;
            }
        }
    }

    // Second sweep: process each <style> block as its own descriptor.
    // <use>-only families (no <style> blocks) get their style descriptors
    // synthesized later in resolveFontReferences, where we can see which
    // styles the referenced families actually declare.
    for (LLXMLNodePtr c = font_node->getFirstChild(); c.notNull(); c = c->getNextSibling())
    {
        if (!c->hasName("style"))
            continue;

        std::string style_name;
        c->getAttributeString("name", style_name);
        // getStyleFromString returns 0 (NORMAL) for "NORMAL" or unrecognized
        // input — both produce the same NORMAL-style descriptor.
        U8 style_flags = LLFontGL::getStyleFromString(style_name);

        LLFontDescriptor desc;
        desc.setName(family_name);
        desc.setStyle(style_flags);
        desc.setSize(s_template_string);

        // Parse <file>/<os> inside the <style>. Pass nullptr for extras —
        // <size>/<use>/inherit at <style> level are not supported.
        font_desc_init_from_xml(c, desc, defaults, nullptr);

        // Stamp this family onto every file the <style> contributed so
        // createFont can later look up the family's per-family <size>
        // entry and pin these specific files at that point size. <use>-
        // chain copies preserve the tag, so a family's pinned-size files
        // keep their preferred point size when they appear as fallbacks
        // under a different head.
        desc.tagSourceFamily(family_name);

        bool style_inherit = false;
        if (c->hasAttribute("inherit"))
            c->getAttributeBOOL("inherit", style_inherit);

        mergeFontEntry(desc);
        if (style_inherit)
        {
            mInheritFlags[std::make_pair(family_name, style_flags)] = true;
        }
    }

    // Stash family-level metadata. Merging across skin layers: later layers
    // add to the maps without erasing earlier entries.
    if (!family_sizes.empty())
    {
        auto& dst = mFamilySizes[family_name];
        for (const auto& kv : family_sizes)
            dst[kv.first] = kv.second;
    }
    if (!uses.empty())
    {
        auto& dst = mFamilyUses[family_name];
        for (const auto& u : uses)
            dst.push_back(u);
    }
}

bool init_from_xml(LLFontRegistry* registry, LLXMLNodePtr node)
{
    LLXMLNodePtr child;

    for (child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
    {
        std::string child_name;
        child->getAttributeString("name",child_name);
        if (child->hasName("font"))
        {
            // New format detection: any <style> or <use> child marks the
            // entry as new-format. <use>-only entries are valid — the
            // family has no primary face, just declares cross-family
            // references that pull others in. processNewFormatFont
            // synthesizes an empty NORMAL descriptor in that case so the
            // resolver has a host to merge into.
            bool has_new_format_children = false;
            for (LLXMLNodePtr c = child->getFirstChild(); c.notNull(); c = c->getNextSibling())
            {
                if (c->hasName("style") || c->hasName("use"))
                {
                    has_new_format_children = true;
                    break;
                }
            }
            if (has_new_format_children)
            {
                registry->processNewFormatFont(child);
                continue;
            }

            // Legacy format: a <font> entry with <file>/<os> children directly,
            // and one (name, font_style) per entry.
            LLFontDescriptor desc;
            FontParseExtras extras;
            bool font_succ = font_desc_init_from_xml(child, desc, FamilyDefaults(), &extras);
            LLFontDescriptor norm_desc = desc.normalize();
            if (font_succ)
            {
                // Stamp the normalized family name onto every file so the
                // per-family size pin lookup at render time can route
                // through mFamilySizes (also keyed by normalized family
                // name).
                desc.tagSourceFamily(norm_desc.getName());

                // if this is the first time we've seen this font name,
                // create a new template map entry for it.
                const LLFontDescriptor *match_desc = registry->getMatchingFontDesc(desc);
                if (match_desc == NULL)
                {
                    // Create a new entry (with no corresponding font).
                    registry->mFontMap[norm_desc] = NULL;
                }
                // otherwise, find the existing entry and combine data.
                else
                {
                    // Prepend files from desc.
                    // A little roundabout because the map key is const,
                    // so we have to fetch it, make a new map key, and
                    // replace the old entry.
                    font_file_info_vec_t font_files = match_desc->getFontFiles();
                    font_files.insert(font_files.begin(),
                                      desc.getFontFiles().begin(),
                                      desc.getFontFiles().end());

                    LLFontDescriptor new_desc = *match_desc;
                    new_desc.setFontFiles(font_files);
                    registry->mFontMap.erase(*match_desc);
                    registry->mFontMap[new_desc] = NULL;
                }

                // Stash per-family size overrides and the inherit flag for
                // later runtime use. Keyed by normalized family name so that
                // size lookups use the same name the descriptor will carry.
                if (!extras.family_sizes.empty())
                {
                    auto& dst = registry->mFamilySizes[norm_desc.getName()];
                    for (const auto& kv : extras.family_sizes)
                        dst[kv.first] = kv.second;
                }
                if (extras.inherit)
                {
                    registry->mInheritFlags[std::make_pair(norm_desc.getName(), norm_desc.getStyle())] = true;
                }
            }
        }
        else if (child->hasName("font_size"))
        {
            std::string size_name;
            F32 size_value;
            if (child->getAttributeString("name",size_name) &&
                child->getAttributeF32("size",size_value))
            {
                registry->mFontSizes[size_name] = size_value;
            }

        }
    }

    return true;
}

bool LLFontRegistry::nameToSize(const std::string& family, const std::string& size_name, F32& size)
{
    // Per-family <size> is an absolute pin for THIS family only; never
    // propagates across families via <use> chains or AlchemyUIFontOverrides.
    // Each file in a chain consults its own source family's pin at
    // createFont time (see the per-file effective_point_size lookup);
    // this function only resolves the chain head's nominal pt.
    if (!family.empty())
    {
        family_size_map_t::const_iterator fam_it = mFamilySizes.find(family);
        if (fam_it != mFamilySizes.end())
        {
            font_size_map_t::const_iterator size_it = fam_it->second.find(size_name);
            if (size_it != fam_it->second.end())
            {
                size = size_it->second;
                return true;
            }
        }
    }
    font_size_map_t::iterator it = mFontSizes.find(size_name);
    if (it != mFontSizes.end())
    {
        size = it->second;
        return true;
    }
    return false;
}

bool LLFontRegistry::nameToSize(const std::string& size_name, F32& size)
{
    return nameToSize(LLStringUtil::null, size_name, size);
}


void LLFontRegistry::storeFont(const LLFontDescriptor& desc, LLFontGL* fontp)
{
    auto it = mFontMap.find(desc);
    if (it != mFontMap.end())
    {
        if (it->second != fontp)
        {
            delete it->second;
            it->second = fontp;
        }
        return;
    }
    mFontMap[desc] = fontp;
}

LLFontGL *LLFontRegistry::createFont(const LLFontDescriptor& desc)
{
    // Name should hold a font name recognized as a setting; the value
    // of the setting should be a list of font files.
    // Size should be a recognized string value
    // Style should be a set of flags including any implied by the font name.

    // First decipher the requested size.
    LLFontDescriptor norm_desc = desc.normalize();
    F32 point_size;
    bool found_size = nameToSize(norm_desc.getName(), norm_desc.getSize(), point_size);
    if (!found_size)
    {
        LL_WARNS() << "createFont unrecognized size " << norm_desc.getSize() << LL_ENDL;
        return NULL;
    }
    LL_INFOS() << "createFont " << norm_desc.getName() << " size " << norm_desc.getSize() << " style " << ((S32) norm_desc.getStyle()) << LL_ENDL;
    F32 fallback_scale = 1.0;

    // Find corresponding font template (based on same descriptor with no size specified)
    LLFontDescriptor template_desc(norm_desc);
    template_desc.setSize(s_template_string);
    const LLFontDescriptor *match_desc = getClosestFontTemplate(template_desc);
    if (!match_desc)
    {
        LL_WARNS() << "createFont failed, no template found for "
                << norm_desc.getName() << " style [" << ((S32)norm_desc.getStyle()) << "]" << LL_ENDL;
        return NULL;
    }

    // See whether this best-match font has already been instantiated in the requested size.
    LLFontDescriptor nearest_exact_desc = *match_desc;
    nearest_exact_desc.setSize(norm_desc.getSize());
    font_reg_map_t::iterator it = mFontMap.find(nearest_exact_desc);
    // If we fail to find a font in the fonts directory, it->second might be NULL.
    // We shouldn't construcnt a font with a NULL mFontFreetype.
    // This may not be the best solution, but it at least prevents a crash.
    if (it != mFontMap.end() && it->second != NULL)
    {
        LL_INFOS() << "-- matching font exists: " << nearest_exact_desc.getName() << " size " << nearest_exact_desc.getSize() << " style " << ((S32) nearest_exact_desc.getStyle()) << LL_ENDL;

        // copying underlying Freetype font, and storing in LLFontGL with requested font descriptor
        LLFontGL *font = new LLFontGL;
        font->mFontDescriptor = desc;
        font->mFontFreetype = it->second->mFontFreetype;
        storeFont(desc, font);

        return font;
    }

    // Build list of font names to look for.
    // Files specified for this font come first, followed by those from the default descriptor.
    font_file_info_vec_t font_files = match_desc->getFontFiles();
    LLFontDescriptor default_desc("default",s_template_string,0);
    const LLFontDescriptor *match_default_desc = getMatchingFontDesc(default_desc);
    if (match_default_desc)
    {
        font_files.insert(font_files.end(),
                          match_default_desc->getFontFiles().begin(),
                          match_default_desc->getFontFiles().end());
    }

    // Add ultimate fallback list - generated dynamically on linux,
    // null elsewhere.
    std::transform(getUltimateFallbackList().begin(), getUltimateFallbackList().end(), std::back_inserter(font_files),
                   [](const std::string& file_name) { return LLFontFileInfo(file_name, EFontHinting::FORCE_AUTOHINT, 0, 0.f); });

    // Load fonts based on names.
    if (font_files.empty())
    {
        LL_WARNS() << "createFont failed, no file names specified" << LL_ENDL;
        return NULL;
    }

    LLFontGL *result = NULL;

    // The first font will get pulled will be the "head" font, set to non-fallback.
    // Rest will consitute the fallback list.
    bool is_first_found = true;

    string_vec_t font_search_paths;
    font_search_paths.push_back(LLFontGL::getFontPathLocal());
    font_search_paths.push_back(LLFontGL::getFontPathSystem());
#if LL_DARWIN
    font_search_paths.push_back(MACOSX_FONT_PATH_LIBRARY);
    font_search_paths.push_back(MACOSX_FONT_PATH_LIBRARY + MACOSX_FONT_SUPPLEMENTAL);
    font_search_paths.push_back(LLFontGL::getFontPathSystem() + MACOSX_FONT_SUPPLEMENTAL);
#endif

    // The fontname string may contain multiple font file names separated by semicolons.
    // Break it apart and try loading each one, in order.
    for(font_file_info_vec_t::iterator font_file_it = font_files.begin();
        font_file_it != font_files.end();
        ++font_file_it)
    {
        LLFontGL *fontp = NULL;

        // *HACK: Fallback fonts don't render, so we can use that to suppress
        // creation of OpenGL textures for test apps. JC
        bool is_fallback = !is_first_found || !mCreateGLTextures;
        F32 extra_scale = (is_fallback) ? fallback_scale : 1.0f;
        // Per-family absolute pin: if this file's source family declares
        // a <size> for the requested size_name, that point size pins THIS
        // file regardless of the chain head's resolved pt. Lets a fallback
        // family render at its own preferred size beneath any head.
        // mSourceFamily is empty for files without an attributable family
        // (e.g. the ultimate-fallback list synthesized in createFont, or
        // bare-filename overrides) — those fall through to the chain pt.
        F32 effective_point_size = point_size;
        if (!font_file_it->mSourceFamily.empty())
        {
            family_size_map_t::const_iterator fam_it =
                mFamilySizes.find(font_file_it->mSourceFamily);
            if (fam_it != mFamilySizes.end())
            {
                font_size_map_t::const_iterator sz_it =
                    fam_it->second.find(norm_desc.getSize());
                if (sz_it != fam_it->second.end())
                {
                    effective_point_size = sz_it->second;
                }
            }
        }
        F32 point_size_scale = extra_scale * effective_point_size;
        F32 actual_point_size = point_size_scale + font_file_it->mSizeDelta;
        bool is_font_loaded = false;
        for(string_vec_t::iterator font_search_path_it = font_search_paths.begin();
            font_search_path_it != font_search_paths.end();
            ++font_search_path_it)
        {
            const std::string font_path = *font_search_path_it + font_file_it->FileName;

            // Static probe — counting collection faces needs no LLFontGL.
            // The lazy `new LLFontGL` inside the face loop below allocates
            // only when a face actually loads, so cache-hit-only files and
            // missing search paths allocate nothing.
            S32 num_faces = font_file_it->mLoadCollection ? LLFontFreetype::getNumFaces(font_path) : 1;
            for (S32 i = 0; i < num_faces; i++)
            {
                // Fallback dedup: if the same (filename, face_index, sized
                // params, monospace_ligatures) tuple has already been loaded
                // by a previous head, reuse it. The cache survives whether
                // the head that originally allocated it is still around —
                // entries are LLPointer-refcounted.
                if (!is_first_found)
                {
                    FallbackInstanceKey key{
                        {font_file_it->FileName, i, actual_point_size,
                         LLFontGL::sVertDPI, LLFontGL::sHorizDPI,
                         font_file_it->mHinting, font_file_it->mFlags,
                         font_file_it->mVarAxes},
                        font_file_it->mMonospaceLigatures
                    };
                    auto cache_it = mFallbackInstanceCache.find(key);
                    if (cache_it != mFallbackInstanceCache.end())
                    {
                        result->mFontFreetype->addFallbackFont(cache_it->second, font_file_it->CharFunctor);
                        is_font_loaded = true;
                        continue;
                    }
                }

                if (fontp == NULL)
                {
                    fontp = new LLFontGL;
                }
                if (fontp->loadFace(font_path, actual_point_size,
                                 LLFontGL::sVertDPI, LLFontGL::sHorizDPI, is_fallback, i, font_file_it->mHinting, font_file_it->mFlags, font_file_it->mVarAxes))
                {
                    is_font_loaded = true;
                    fontp->mFontFreetype->setAllowMonospaceLigatures(font_file_it->mMonospaceLigatures);
                    if (is_first_found)
                    {
                        result = fontp;
                        is_first_found = false;
                        // Detach fontp from result so the next i iteration
                        // (TTC head with num_faces > 1) allocates a fresh
                        // LLFontGL for the next face instead of reusing —
                        // and crucially, doesn't `delete fontp` (which
                        // would destroy result and dangle every cached
                        // pointer to it).
                        fontp = NULL;
                    }
                    else
                    {
                        // Insert into cache so the next head asking for
                        // matching params reuses this LLFontFreetype.
                        FallbackInstanceKey key{
                            {font_file_it->FileName, i, actual_point_size,
                             LLFontGL::sVertDPI, LLFontGL::sHorizDPI,
                             font_file_it->mHinting, font_file_it->mFlags,
                             font_file_it->mVarAxes},
                            font_file_it->mMonospaceLigatures
                        };
                        mFallbackInstanceCache.emplace(key, fontp->mFontFreetype);

                        result->mFontFreetype->addFallbackFont(fontp->mFontFreetype, font_file_it->CharFunctor);

                        delete fontp;
                        fontp = NULL;
                    }
                }
                else
                {
                    delete fontp;
                    fontp = NULL;
                }
            }
            if (is_font_loaded) break;
        }
        if(!is_font_loaded)
        {
            LL_INFOS_ONCE("LLFontRegistry") << "Couldn't load font " << font_file_it->FileName <<  LL_ENDL;
        }
        // fontp is non-NULL here only when an allocated wrapper wasn't
        // consumed — its loadFace succeeded for a cache-inserted fallback
        // but a later face of the same file hit mFallbackInstanceCache and
        // `continue`d past the delete branches. Ownership wasn't
        // transferred to result, so free it. (All-cache-hit files and
        // missing search paths never allocate now — the wrapper is created
        // lazily inside the face loop.)
        delete fontp;
        fontp = NULL;
    }

    if (result)
    {
        result->mFontDescriptor = desc;
        storeFont(desc, result);
        // Also publish the result under its canonical (template-name +
        // normalized-size) key when that differs from the requested desc
        // and isn't taken. The matching-font-exists shortcut at the top
        // probes exactly this key, so the next differently-spelled
        // descriptor that normalizes to the same font shares this
        // freetype through a thin wrapper instead of re-walking every
        // font file. The alias is a full map citizen: owned by its slot,
        // deleted in clear(), rebuilt by reload() like any head.
        if (mFontMap.find(nearest_exact_desc) == mFontMap.end())
        {
            LLFontGL* canonical = new LLFontGL;
            canonical->mFontDescriptor = nearest_exact_desc;
            canonical->mFontFreetype = result->mFontFreetype;
            mFontMap[nearest_exact_desc] = canonical;
        }
    }
    else
    {
        // Don't poison mFontMap with a NULL entry — getFont uses
        // find()-then-return-as-is, so a cached NULL would mask every future
        // request for this descriptor and silently hand widgets a NULL font
        // pointer. Leaving the slot absent lets the next getFont call retry
        // creation; if creation keeps failing, the warning below logs each
        // retry but the registry never returns a stale NULL.
        LL_WARNS() << "createFont failed in some way" << LL_ENDL;
    }
    return result;
}

void LLFontRegistry::clear()
{
    for (font_reg_map_t::iterator it = mFontMap.begin();
         it != mFontMap.end();
         ++it)
    {
        LLFontGL *fontp = it->second;
        delete fontp;
    }
    mFontMap.clear();
    // After heads are gone, the cache holds the only refs to shared
    // fallback instances. Clearing it triggers FT_Done_Face for those
    // wrappers (via ALFontFace's destructor when the LLPointer refcount
    // hits zero through LLFontFreetype's release).
    mFallbackInstanceCache.clear();
}

bool LLFontRegistry::overridesEqual(const LLSD& candidate) const
{
    return llsd_equals(mLastFontOverrides, candidate);
}

void LLFontRegistry::sweepGlyphCaches()
{
    LL_PROFILE_ZONE_SCOPED;
    for (auto& kv : mFontMap)
    {
        if (kv.second && kv.second->mFontFreetype)
            kv.second->mFontFreetype->collectGarbage();
    }
    for (auto& kv : mFallbackInstanceCache)
    {
        if (kv.second)
            kv.second->collectGarbage();
    }
}

void LLFontRegistry::reloadForDpiChange()
{
    LL_PROFILE_ZONE_SCOPED;
    // Fast path used by LLFontGL::initClass when fonts.xml content and
    // overrides have not changed. Each freetype's mFace gets re-resolved
    // through LLFontManager::getOrCreateFace at the new (sVertDPI,
    // sHorizDPI) — DPI rounded to the same integers reuses the cached
    // face wrapper; otherwise a fresh wrapper is created and the old one
    // drops to manager-only refcount. Heads use resetSelf so the head /
    // fallback cascade isn't double-walked. Shared fallback freetypes
    // are reset once via mFallbackInstanceCache.
    for (font_reg_map_t::iterator it = mFontMap.begin();
         it != mFontMap.end();
         ++it)
    {
        if (it->second && it->second->mFontFreetype)
        {
            it->second->mFontFreetype->resetSelf(LLFontGL::sVertDPI, LLFontGL::sHorizDPI);
        }
    }
    for (auto& kv : mFallbackInstanceCache)
    {
        if (kv.second)
            kv.second->resetSelf(LLFontGL::sVertDPI, LLFontGL::sHorizDPI);
    }
}

bool LLFontRegistry::reload(const LLSD& font_overrides)
{
    LL_INFOS() << "Reloading font registry" << LL_ENDL;

    // Pointer-stable rebuild: every existing LLFontGL* (cached by widgets
    // and by the static getters in llfontgl.cpp) stays alive. We swap each
    // head's underlying mFontFreetype for one built from a freshly-parsed
    // fonts.xml + caller-supplied font overrides.

    // Snapshot existing non-template heads so we can wipe mFontMap freely.
    std::vector<std::pair<LLFontDescriptor, LLFontGL*>> heads;
    heads.reserve(mFontMap.size());
    for (const auto& kv : mFontMap)
    {
        if (!kv.first.isTemplate() && kv.second)
            heads.emplace_back(kv.first, kv.second);
    }

    // Pin the old fallback cache for the duration of the rebuild. Without
    // this, the move/clear below would drop the only refs to shared
    // LLFontFreetype fallbacks while existing heads' mFontFreetype still
    // hold pointers into them — every fallback render would touch a freed
    // FT_Face. Releasing the local at the end of the function tears down
    // any old fallback that nothing holds anymore.
    auto pinned_old_fallbacks = std::move(mFallbackInstanceCache);
    mFallbackInstanceCache.clear();

    // Snapshot the full parse-time state alongside the heads. parseFontInfo
    // can fail partway (malformed fonts.xml / override file) AFTER the wipes
    // below have run; restoring only the heads would leave nameToSize,
    // getAvailableFamilies, and uncached getFont calls running against an
    // emptied registry until the next successful reload. mFontMap values are
    // owned raw pointers, but the copy is safe: a failed parse only ever
    // adds nullptr template placeholders (mergeFontEntry), so restoring the
    // snapshot over the partial map can't double-own or leak a live
    // LLFontGL, and on success the snapshot copies die without deleting.
    auto saved_font_map       = mFontMap;
    auto saved_font_sizes     = mFontSizes;
    auto saved_family_sizes   = mFamilySizes;
    auto saved_family_uses    = mFamilyUses;
    auto saved_inherit_flags  = mInheritFlags;
    auto saved_family_meta    = mFamilyMeta;
    auto saved_last_overrides = mLastFontOverrides;

    // Wipe parse-time state.
    mFontSizes.clear();
    mFamilySizes.clear();
    mFamilyUses.clear();
    mInheritFlags.clear();
    mFamilyMeta.clear();

    // Wipe ALL mFontMap entries: templates so re-parse rebuilds them
    // cleanly, non-templates so createFont's same-template shortcut at
    // lines ~1108-1119 doesn't accidentally share an old (stale) freetype
    // with a newly-built head. Pointers stay alive via the `heads`
    // snapshot.
    mFontMap.clear();

    if (!parseFontInfo("fonts.xml", font_overrides))
    {
        LL_WARNS() << "Font reload: parseFontInfo failed; restoring previous registry state" << LL_ENDL;
        mFallbackInstanceCache = std::move(pinned_old_fallbacks);
        // Restore the registry wholesale — templates + heads, size tables,
        // family metadata, and the applied-overrides snapshot — so every
        // lookup path behaves exactly as before the attempt. The partial
        // parse's nullptr placeholders in mFontMap are discarded by the
        // assignment (nothing non-null to free; see the snapshot note).
        mFontMap           = std::move(saved_font_map);
        mFontSizes         = std::move(saved_font_sizes);
        mFamilySizes       = std::move(saved_family_sizes);
        mFamilyUses        = std::move(saved_family_uses);
        mInheritFlags      = std::move(saved_inherit_flags);
        mFamilyMeta        = std::move(saved_family_meta);
        mLastFontOverrides = saved_last_overrides;
        return false;
    }

    // Drop the global shape cache: every shaped-glyph entry keys on a raw
    // LLFontFreetype* that's about to dangle. ~LLFontFreetype also calls
    // clearCacheForFace when each old freetype drops to refcount 0, but
    // here we're rebuilding every head — clearing in one shot up front is
    // cheaper than O(faces × entries) per-face sweeps and avoids racing
    // the destructor against new entries the loop below would otherwise
    // produce.
    ALFontShaping::clearCache();

    for (auto& [desc, head] : heads)
    {
        // createFont allocates a fresh LLFontGL with newly-loaded
        // mFontFreetype and stores it at mFontMap[desc]. We steal its
        // mFontFreetype LLPointer onto the existing head, then re-seat the
        // original pointer so widget caches stay valid — storeFont deletes
        // the `fresh` wrapper sitting in the slot. The snapshot can hold
        // canonical aliases too; when their turn comes, createFont's
        // matching-font-exists shortcut shares the already-rebuilt
        // freetype instead of re-walking font files.
        LLFontGL* fresh = createFont(desc);
        if (fresh)
        {
            head->mFontFreetype = fresh->mFontFreetype;
            head->mFontDescriptor = desc;
            storeFont(desc, head);
            if (mCreateGLTextures)
            {
                head->generateASCIIglyphs();
            }
        }
        else
        {
            LL_WARNS() << "Font reload: createFont failed for "
                       << desc.getName() << " size " << desc.getSize()
                       << " style " << (S32)desc.getStyle()
                       << "; keeping previous freetype" << LL_ENDL;
            // Re-seat the old head so widgets still render its previous
            // (now-orphaned) glyphs rather than nothing at all. storeFont
            // guards against an interim alias occupying the slot.
            storeFont(desc, head);
        }
    }

    // pinned_old_fallbacks goes out of scope here. Any shared fallback
    // instance no longer referenced by a (rebuilt) head's mFontFreetype
    // chain hits refcount 0; ~ALFontFace fires on its ALFontFace and
    // releases the FT_Face + atlas LLImageGL.
    return true;
}

std::vector<LLFontRegistry::FamilyInfo> LLFontRegistry::getAvailableFamilies(FamilyFilter filter) const
{
    // Templates are the canonical "<font name='X'>" entries from fonts.xml
    // after skin layering and reference resolution. Filter out:
    //   - "default" — OS-fallback plumbing, never user-selectable.
    //   - any family with user_selectable="false" attribute in fonts.xml
    //     (e.g. internal fallbacks like Emoji/CJK that exist only to be
    //     <use>'d by user-facing families).
    //   - filter-specific exclusions:
    //       MONOSPACE    — only families with monospace=true, emoji=false.
    //       PROPORTIONAL — only families with monospace=false, emoji=false.
    //       EMOJI        — only families with emoji=true.
    //     Emoji families are kept out of both MONOSPACE and PROPORTIONAL
    //     so the picker doesn't surface a color emoji font in the regular
    //     UI/Mono lists; callers wanting an emoji-font picker pass EMOJI.
    std::set<std::string> uniq;
    for (const auto& kv : mFontMap)
    {
        if (!kv.first.isTemplate())
            continue;
        const std::string& name = kv.first.getName();
        if (name == "default")
            continue;
        auto meta_it = mFamilyMeta.find(name);
        const bool has_meta = (meta_it != mFamilyMeta.end());
        if (has_meta && !meta_it->second.user_selectable)
            continue;
        const bool is_monospace = has_meta && meta_it->second.monospace;
        const bool is_emoji     = has_meta && meta_it->second.emoji;
        if (filter == FamilyFilter::MONOSPACE)
        {
            if (!is_monospace || is_emoji)
                continue;
        }
        else if (filter == FamilyFilter::PROPORTIONAL)
        {
            if (is_monospace || is_emoji)
                continue;
        }
        else if (filter == FamilyFilter::EMOJI)
        {
            if (!is_emoji)
                continue;
        }
        uniq.insert(name);
    }

    std::vector<FamilyInfo> result;
    result.reserve(uniq.size());
    for (const std::string& name : uniq)
    {
        FamilyInfo info;
        info.name = name;
        auto meta_it = mFamilyMeta.find(name);
        info.label = (meta_it != mFamilyMeta.end() && !meta_it->second.ui_label.empty())
            ? meta_it->second.ui_label
            : name;
        result.push_back(std::move(info));
    }
    // Sort by label for stable, human-friendly dropdown order. Ties (same
    // label across renamed entries) fall back to canonical name to keep
    // ordering deterministic.
    std::sort(result.begin(), result.end(),
              [](const FamilyInfo& a, const FamilyInfo& b)
              {
                  if (a.label != b.label) return a.label < b.label;
                  return a.name < b.name;
              });
    return result;
}

void LLFontRegistry::destroyGL()
{
    for (font_reg_map_t::iterator it = mFontMap.begin();
         it != mFontMap.end();
         ++it)
    {
        // Reset the corresponding font but preserve the entry.
        if (it->second)
            it->second->destroyGL();
    }
}

LLFontGL *LLFontRegistry::getFont(const LLFontDescriptor& desc)
{
    font_reg_map_t::iterator it = mFontMap.find(desc);
    if (it != mFontMap.end())
        return it->second;
    else
    {
        LLFontGL *fontp = createFont(desc);
        if (!fontp)
        {
            LL_WARNS() << "getFont failed, name " << desc.getName()
                    <<" style=[" << ((S32) desc.getStyle()) << "]"
                    << " size=[" << desc.getSize() << "]" << LL_ENDL;
        }
        else if (mCreateGLTextures)
        {
            // Generate glyphs for ASCII chars to avoid stalls later. Only
            // worth doing when there is an atlas to warm -- and only safe
            // then: createFont marks every face a fallback when
            // mCreateGLTextures is off, including the head, and asking a
            // fallback for glyph info directly is an error.
            fontp->generateASCIIglyphs();
        }
        return fontp;
    }
}

const LLFontDescriptor *LLFontRegistry::getMatchingFontDesc(const LLFontDescriptor& desc)
{
    LLFontDescriptor norm_desc = desc.normalize();

    font_reg_map_t::iterator it = mFontMap.find(norm_desc);
    if (it != mFontMap.end())
        return &(it->first);
    else
        return NULL;
}

static U32 bitCount(U8 c)
{
    U32 count = 0;
    if (c & 1)
        count++;
    if (c & 2)
        count++;
    if (c & 4)
        count++;
    if (c & 8)
        count++;
    if (c & 16)
        count++;
    if (c & 32)
        count++;
    if (c & 64)
        count++;
    if (c & 128)
        count++;
    return count;
}

// Find nearest match for the requested descriptor.
const LLFontDescriptor *LLFontRegistry::getClosestFontTemplate(const LLFontDescriptor& desc)
{
    const LLFontDescriptor *exact_match_desc = getMatchingFontDesc(desc);
    if (exact_match_desc)
    {
        return exact_match_desc;
    }

    LLFontDescriptor norm_desc = desc.normalize();

    const LLFontDescriptor *best_match_desc = NULL;
    for (font_reg_map_t::iterator it = mFontMap.begin();
         it != mFontMap.end();
         ++it)
    {
        const LLFontDescriptor* curr_desc = &(it->first);

        // Ignore if not a template.
        if (!curr_desc->isTemplate())
            continue;

        // Ignore if font name is wrong.
        if (curr_desc->getName() != norm_desc.getName())
            continue;

        // Reject font if it matches any bits we don't want
        if (curr_desc->getStyle() & ~norm_desc.getStyle())
        {
            continue;
        }

        // Take if it's the first plausible candidate we've found.
        if (!best_match_desc)
        {
            best_match_desc = curr_desc;
            continue;
        }

        // Take if it matches more bits than anything before.
        U8 best_style_match_bits =
            norm_desc.getStyle() & best_match_desc->getStyle();
        U8 curr_style_match_bits =
            norm_desc.getStyle() & curr_desc->getStyle();
        if (bitCount(curr_style_match_bits) > bitCount(best_style_match_bits))
        {
            best_match_desc = curr_desc;
            continue;
        }

        // Tie-breaker: take if it matches bold.
        if (curr_style_match_bits & LLFontGL::BOLD)  // Bold is requested and this descriptor matches it.
        {
            best_match_desc = curr_desc;
            continue;
        }
    }

    // Nothing matched.
    return best_match_desc;
}

void LLFontRegistry::dump()
{
    LL_INFOS() << "LLFontRegistry dump: " << LL_ENDL;
    for (font_size_map_t::iterator size_it = mFontSizes.begin();
         size_it != mFontSizes.end();
         ++size_it)
    {
        LL_INFOS() << "Size: " << size_it->first << " => " << size_it->second << LL_ENDL;
    }
    for (font_reg_map_t::iterator font_it = mFontMap.begin();
         font_it != mFontMap.end();
         ++font_it)
    {
        const LLFontDescriptor& desc = font_it->first;
        LL_INFOS() << "Font: name=" << desc.getName()
                << " style=[" << ((S32)desc.getStyle()) << "]"
                << " size=[" << desc.getSize() << "]"
                << " fileNames="
                << LL_ENDL;
        for (font_file_info_vec_t::const_iterator file_it=desc.getFontFiles().begin();
             file_it != desc.getFontFiles().end();
             ++file_it)
        {
            LL_INFOS() << "  file: " << file_it->FileName << LL_ENDL;
        }
    }
}

void LLFontRegistry::dumpTextures()
{
    for (const auto& fontEntry : mFontMap)
    {
        if (fontEntry.second)
        {
            fontEntry.second->dumpTextures();
        }
    }
}

const string_vec_t& LLFontRegistry::getUltimateFallbackList() const
{
    return mUltimateFallbackList;
}
