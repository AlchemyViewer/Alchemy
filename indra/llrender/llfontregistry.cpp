/**
 * @file llfontregistry.cpp
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

#include "linden_common.h"
#include "llgl.h"
#include "llfontfreetype.h"
#include "llfontgl.h"
#include "llfontregistry.h"
#include "llfontshaping.h"
#include <algorithm>
#include <set>
#include <boost/tokenizer.hpp>
#include "llcontrol.h"
#include "lldir.h"
#include "llsd.h"
#include "llwindow.h"
#include "llxmlnode.h"

extern LLControlGroup gSavedSettings;

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
        S32 weight = -1;
        bool weight_set = false;
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

void LLFontDescriptor::addFontFile(const std::string& file_name, EFontHinting hinting, S32 flags, F32 size_delta, S32 weight, const std::function<bool(llwchar)>& char_functor, bool monospace_ligatures)
{
    mFontFiles.push_back(LLFontFileInfo(file_name, hinting, flags, size_delta, weight, char_functor, monospace_ligatures));
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

bool LLFontRegistry::parseFontInfo(const std::string& xml_filename)
{
    bool success = false;  // Succeed if we find and read at least one XUI file
    const string_vec_t xml_paths = gDirUtilp->findSkinnedFilenames(LLDir::XUI, xml_filename);
    if (xml_paths.empty())
    {
        // We didn't even find one single XUI file
        return false;
    }

    for (string_vec_t::const_iterator path_it = xml_paths.begin();
         path_it != xml_paths.end();
         ++path_it)
    {
        LLXMLNodePtr root;
        bool parsed_file = LLXMLNode::parseFile(*path_it, root, NULL);

        if (!parsed_file)
            continue;

        if ( root.isNull() || ! root->hasName( "fonts" ) )
        {
            LL_WARNS() << "Bad font info file: " << *path_it << LL_ENDL;
            continue;
        }

        std::string root_name;
        root->getAttributeString("name",root_name);
        if (root->hasName("fonts"))
        {
            // Expect a collection of children consisting of "font" or "font_size" entries
            bool init_succ = init_from_xml(this, root);
            success = success || init_succ;
        }
    }

    // Resolve cross-family <use> and per-family inherit="true" once across
    // the merged registry, after all skinned fonts.xml files have been read.
    // Doing this per-file would double-append references for entries that
    // appear in multiple skin layers.
    resolveFontReferences();

    //if (success)
    //  dump();

    return success;
}

void LLFontRegistry::resolveFontReferences()
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

    // Pre-step: synthesize style descriptors for <use>-only families based
    // on which styles their referenced families actually declare. A family
    // that declared no <style> blocks gets a host descriptor for each
    // style at least one (non-self) reference supports — either at that
    // exact style or fallback-to-NORMAL.
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
            for (const std::string& used : kv.second)
            {
                if (used == family)
                    continue; // self-reference is rejected during resolution
                if (mFontMap.find(LLFontDescriptor(used, s_template_string, style)) != mFontMap.end())
                {
                    any_ref_has_style = true;
                    break;
                }
                // Non-NORMAL style absent on the reference falls back to NORMAL,
                // which means the aggregator should still expose this style as
                // long as any reference has SOME face to merge in.
                if (style != 0
                    && mFontMap.find(LLFontDescriptor(used, s_template_string, 0)) != mFontMap.end())
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

    // Snapshot pre-resolution file lists. <use> targets are looked up in
    // this snapshot rather than the live mFontMap, so resolution is order-
    // independent and cycles / self-references behave deterministically:
    //   A->B and B->A both end up as [own, other], not double-counted;
    //   <use family="self"/> still warns and is skipped below regardless.
    boost::unordered_map<LLFontDescriptor, font_file_info_vec_t> pre_use;
    pre_use.reserve(mFontMap.size());
    for (const auto& entry : mFontMap)
    {
        pre_use.emplace(entry.first, entry.first.getFontFiles());
    }

    for (const auto& kv : mFamilyUses)
    {
        const std::string& family = kv.first;
        const std::vector<std::string>& used_families = kv.second;

        for (U8 style : kStyles)
        {
            LLFontDescriptor key(family, s_template_string, style);
            auto it = mFontMap.find(key);
            if (it == mFontMap.end())
                continue;

            font_file_info_vec_t merged_files = it->first.getFontFiles();

            for (const std::string& used : used_families)
            {
                if (used == family)
                {
                    LL_WARNS() << "fonts.xml: <use family='" << used
                               << "'/> in '" << family
                               << "' is self-referential; skipping" << LL_ENDL;
                    continue;
                }
                LLFontDescriptor used_key(used, s_template_string, style);
                auto snap_it = pre_use.find(used_key);
                if (snap_it == pre_use.end() && style != 0)
                {
                    // Fall back to NORMAL of the referenced family.
                    used_key = LLFontDescriptor(used, s_template_string, 0);
                    snap_it = pre_use.find(used_key);
                }
                if (snap_it == pre_use.end())
                {
                    LL_WARNS() << "fonts.xml: <use family='" << used
                               << "'/> in '" << family
                               << "' but no such family is defined" << LL_ENDL;
                    continue;
                }
                const auto& src_files = snap_it->second;
                merged_files.insert(merged_files.end(), src_files.begin(), src_files.end());
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

    // Step 3: apply user font overrides (AlchemyUIFontOverrides) on top of
    // the fully-composed file lists. Done last so the override prepends
    // ahead of resolved <use> chains and inherited variants.
    applyFamilyOverrides(gSavedSettings.getLLSD("AlchemyUIFontOverrides"));

    // Consume both maps so a subsequent call (e.g. on dynamic skin reload)
    // doesn't double-append references to already-resolved entries.
    mFamilyUses.clear();
    mInheritFlags.clear();
}

void LLFontRegistry::applyFamilyOverrides(const LLSD& overrides)
{
    // Always clear the override-source map so a removed override stops
    // routing size lookups through the prior source. Even an empty
    // overrides map needs this — early-returning before would leave
    // stale entries from a previous parse.
    mFamilyOverrideSources.clear();

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

    for (LLSD::map_const_iterator ov_it = overrides.beginMap();
         ov_it != overrides.endMap();
         ++ov_it)
    {
        const std::string& family = ov_it->first;
        std::string override_value = ov_it->second.asString();
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

        // Record family→family override so nameToSize can route per-family
        // <size> lookups through the source family. File-name overrides
        // don't get an entry — there's no source family to inherit sizes
        // from.
        if (source_is_family)
        {
            mFamilyOverrideSources[family] = override_value;
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
                // File-name override. Inherit hinting/weight/flags/ligatures
                // defaults from the target family's first existing file so
                // the swap behaves like a drop-in replacement (e.g. a
                // monospace family's ligatures setting carries to the
                // user-supplied file).
                EFontHinting hinting = EFontHinting::FORCE_AUTOHINT;
                S32 weight = -1;
                S32 flags = 0;
                bool monospace_lig = false;
                const auto& orig_files = target_it->first.getFontFiles();
                if (!orig_files.empty())
                {
                    hinting = orig_files[0].mHinting;
                    weight = orig_files[0].mWeight;
                    flags = orig_files[0].mFlags;
                    monospace_lig = orig_files[0].mMonospaceLigatures;
                }
                if (style & LLFontGL::BOLD)
                    flags |= LLFontGL::BOLD;
                prepend_files.emplace_back(override_value, hinting, flags, 0.f, weight, std::function<bool(llwchar)>(nullptr), monospace_lig);
            }

            // Prepend source onto the target's existing chain so the
            // override wins for the head face but DejaVu/Emoji/CJK
            // fallbacks (already resolved in onto target via <use>) stay
            // available behind it.
            font_file_info_vec_t merged_files = prepend_files;
            const auto& orig_files = target_it->first.getFontFiles();
            merged_files.insert(merged_files.end(), orig_files.begin(), orig_files.end());

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

        if (node->hasAttribute("font_weight"))
        {
            S32 fam_weight = -1;
            node->getAttributeS32("font_weight", fam_weight);
            defaults.weight = fam_weight;
            defaults.weight_set = true;
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
            S32 weight = defaults.weight_set ? defaults.weight : -1;

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

            if (child->hasAttribute("font_weight"))
            {
                child->getAttributeS32("font_weight", weight);
            }

            desc.addFontFile(font_file_name, hinting, flags, size_delta, weight, inline_functor, defaults.monospace_ligatures);
        }
        else if (child->hasName("size"))
        {
            // Per-family size override. Ignored on <os> recursion (extras is
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
// Read family-level defaults (ligatures, font_hinting, font_weight) from a
// <font> node. Used by both legacy and new-format paths; mirrors the
// family-level reads done inside font_desc_init_from_xml for backward
// compatibility.
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
    if (font_node->hasAttribute("font_weight"))
    {
        S32 w = -1;
        font_node->getAttributeS32("font_weight", w);
        d.weight = w;
        d.weight_set = true;
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
        if (font_node->hasAttribute("ui_label"))
        {
            std::string ui_label;
            font_node->getAttributeString("ui_label", ui_label);
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
    }

    // First sweep: collect family-level <size>, <use>; defer <style> to
    // a second sweep so we can warn cleanly on stray children.
    family_size_overrides_t family_sizes;
    std::vector<std::string> uses;
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
                uses.push_back(used);
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
    auto try_family = [&](const std::string& fam) -> bool
    {
        family_size_map_t::const_iterator fam_it = mFamilySizes.find(fam);
        if (fam_it == mFamilySizes.end())
            return false;
        font_size_map_t::const_iterator size_it = fam_it->second.find(size_name);
        if (size_it == fam_it->second.end())
            return false;
        size = size_it->second;
        return true;
    };

    if (!family.empty())
    {
        // If `family` was overridden by a known source family via
        // AlchemyUIFontOverrides, consult the source's per-family <size>
        // table first so an override picks up the source's size scale
        // (e.g. Monospace -> SourceCode brings SourceCode's sizes).
        // Single-level only — chains aren't followed.
        auto ov_it = mFamilyOverrideSources.find(family);
        if (ov_it != mFamilyOverrideSources.end() && try_family(ov_it->second))
            return true;
        if (try_family(family))
            return true;
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
        mFontMap[desc] = font;

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
                   [](const std::string& file_name) { return LLFontFileInfo(file_name, EFontHinting::FORCE_AUTOHINT, 0, 0.f, -1); });

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
        F32 point_size_scale = extra_scale * point_size;
        F32 actual_point_size = point_size_scale + font_file_it->mSizeDelta;
        bool is_font_loaded = false;
        for(string_vec_t::iterator font_search_path_it = font_search_paths.begin();
            font_search_path_it != font_search_paths.end();
            ++font_search_path_it)
        {
            const std::string font_path = *font_search_path_it + font_file_it->FileName;

            fontp = new LLFontGL;
            // Always probe num_faces — FreeType returns 1 for single-face
            // files (.ttf, .otf, .woff2) and the actual face count for
            // collections (.ttc, .otc), so we don't need fonts.xml to flag
            // collections explicitly.
            S32 num_faces = fontp->getNumFaces(font_path);
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
                         font_file_it->mWeight, font_file_it->mHinting, font_file_it->mFlags},
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
                                 LLFontGL::sVertDPI, LLFontGL::sHorizDPI, font_file_it->mWeight, is_fallback, i, font_file_it->mHinting, font_file_it->mFlags))
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
                             font_file_it->mWeight, font_file_it->mHinting, font_file_it->mFlags},
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
            delete fontp;
            fontp = NULL;
        }
    }

    if (result)
    {
        result->mFontDescriptor = desc;
    }
    else
    {
        LL_WARNS() << "createFont failed in some way" << LL_ENDL;
    }

    mFontMap[desc] = result;
    return result;
}

void LLFontRegistry::reset()
{
    // With shared fallback LLFontFreetype instances, the legacy cascade in
    // LLFontFreetype::reset (head -> mFallbackFonts) would re-load the same
    // shared fallback once per head that lists it. Drive the reset from
    // here instead: heads use resetSelf (no cascade), then iterate the
    // fallback cache to reset each shared instance exactly once.
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
    // wrappers (via LLFontFace's destructor when the LLPointer refcount
    // hits zero through LLFontFreetype's release).
    mFallbackInstanceCache.clear();
}

bool LLFontRegistry::reload()
{
    // Pointer-stable rebuild: every existing LLFontGL* (cached by widgets
    // and by the static getters in llfontgl.cpp) stays alive. We swap each
    // head's underlying mFontFreetype for one built from a freshly-parsed
    // fonts.xml + current AlchemyUIFontOverrides.

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

    if (!parseFontInfo("fonts.xml"))
    {
        LL_WARNS() << "Font reload: parseFontInfo failed; restoring previous fallback cache" << LL_ENDL;
        mFallbackInstanceCache = std::move(pinned_old_fallbacks);
        // Re-insert the snapshot heads so widgets continue rendering
        // against their previous freetype state.
        for (const auto& [desc, head] : heads)
            mFontMap[desc] = head;
        return false;
    }

    // Drop the global shape cache: every shaped-glyph entry keys on a raw
    // LLFontFreetype* that's about to dangle. ~LLFontFreetype also calls
    // clearCache when each old freetype drops to refcount 0, but doing it
    // once up front avoids racing the destructor against new entries the
    // loop below would otherwise produce.
    LLFontShaping::clearCache();

    for (auto& [desc, head] : heads)
    {
        // createFont allocates a fresh LLFontGL with newly-loaded
        // mFontFreetype and inserts it into mFontMap[desc]. We then steal
        // its mFontFreetype LLPointer onto the existing head and replace
        // the map entry with the original pointer so widget caches stay
        // valid.
        LLFontGL* fresh = createFont(desc);
        if (fresh)
        {
            head->mFontFreetype = fresh->mFontFreetype;
            head->mFontDescriptor = desc;
            mFontMap[desc] = head;
            delete fresh;
            head->generateASCIIglyphs();
        }
        else
        {
            LL_WARNS() << "Font reload: createFont failed for "
                       << desc.getName() << " size " << desc.getSize()
                       << " style " << (S32)desc.getStyle()
                       << "; keeping previous freetype" << LL_ENDL;
            // Re-insert old head so widgets still render its previous
            // (now-orphaned) glyphs rather than nothing at all.
            mFontMap[desc] = head;
        }
    }

    // pinned_old_fallbacks goes out of scope here. Any shared fallback
    // instance no longer referenced by a (rebuilt) head's mFontFreetype
    // chain hits refcount 0; ~LLFontFace fires on its LLFontFace and
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
    //   - if filter == MONOSPACE/PROPORTIONAL, families whose `monospace`
    //     metadata flag doesn't match the requested side.
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
        if (filter == FamilyFilter::MONOSPACE)
        {
            if (!has_meta || !meta_it->second.monospace)
                continue;
        }
        else if (filter == FamilyFilter::PROPORTIONAL)
        {
            if (has_meta && meta_it->second.monospace)
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
        else
        {
            //generate glyphs for ASCII chars to avoid stalls later
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
